import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.38.4"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

// Helper to extract user ID from Authorization header JWT by decoding it directly
function getUserIdFromToken(authHeader: string): string {
  const token = authHeader.replace('Bearer ', '').trim()
  
  // Decode JWT - format is header.payload.signature
  const parts = token.split('.')
  if (parts.length !== 3) {
    throw new Error('Invalid token format: expected 3 parts')
  }
  
  // Decode the payload (second part)
  // Convert URL-safe base64 to standard base64
  let payload = parts[1]
  payload = payload.replace(/-/g, '+').replace(/_/g, '/')
  // Add padding if needed
  while (payload.length % 4) {
    payload += '='
  }
  
  try {
    const decoded = JSON.parse(atob(payload))
    if (!decoded.sub) {
      throw new Error('User ID (sub) not found in token payload')
    }
    console.log('[YOUTUBE_PUBLISH] JWT decoded successfully, user:', decoded.sub)
    return decoded.sub
  } catch (err: any) {
    console.log('[YOUTUBE_PUBLISH] JWT decode error:', err.message)
    throw new Error(`Failed to decode JWT: ${err.message}`)
  }
}

const YOUTUBE_AUTH_EXPIRED_NOTIFICATION = 'ACCOUNT_AUTH_EXPIRED'

async function createInAppNotificationIfAllowed(
  supabaseClient: any,
  userId: string,
  title: string,
  message: string,
  data: Record<string, unknown> = {}
) {
  try {
    const { data: prefs, error: prefsError } = await supabaseClient
      .from('notification_preferences')
      .select('in_app_enabled, preferences')
      .eq('user_id', userId)
      .maybeSingle()

    if (prefsError) {
      console.log('[YOUTUBE_PUBLISH] Notification preferences lookup failed:', prefsError.message)
    }

    if (prefs?.in_app_enabled === false) {
      console.log('[YOUTUBE_PUBLISH] Skipping in-app notification: in_app notifications disabled')
      return
    }

    const typePrefs = prefs?.preferences?.[YOUTUBE_AUTH_EXPIRED_NOTIFICATION]
    if (typePrefs && typePrefs.in_app === false) {
      console.log('[YOUTUBE_PUBLISH] Skipping in-app notification: ACCOUNT_AUTH_EXPIRED disabled')
      return
    }

    const { error: insertError } = await supabaseClient
      .from('notifications')
      .insert({
        user_id: userId,
        notification_type_id: YOUTUBE_AUTH_EXPIRED_NOTIFICATION,
        title,
        message,
        data: {
          platform: 'youtube',
          reconnectUrl: '/dashboard/settings/accounts',
          ...data,
        },
      })

    if (insertError) {
      console.log('[YOUTUBE_PUBLISH] Notification insert failed:', insertError.message)
    } else {
      console.log('[YOUTUBE_PUBLISH] Created auth expiry notification for user:', userId)
    }
  } catch (err: any) {
    console.log('[YOUTUBE_PUBLISH] Notification creation failed:', err.message)
  }
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const body = await req.json()
    const { media_url, video_url, content, title, privacy_status, publish_at } = body || {}

    console.log('[YOUTUBE_PUBLISH] Request body:', { media_url: media_url ? 'present' : 'missing', video_url: video_url ? 'present' : 'missing', content, title })

    if (!media_url && !video_url) {
      throw new Error('media_url or video_url is required')
    }

    const authHeader = req.headers.get('Authorization')
    console.log('[YOUTUBE_PUBLISH] Auth header present:', !!authHeader)
    if (!authHeader) {
      throw new Error('Authorization required')
    }

    const supabaseUrl = Deno.env.get('SUPABASE_URL') ?? ''
    const supabaseAnonKey = Deno.env.get('SUPABASE_ANON_KEY') ?? ''

    if (!supabaseUrl) throw new Error('SUPABASE_URL not configured')
    if (!supabaseAnonKey) throw new Error('SUPABASE_ANON_KEY not configured')

    // Get user ID from JWT token or handle Service Role bypass
    const isServiceRole = authHeader.replace('Bearer ', '').trim() === Deno.env.get('SUPABASE_SERVICE_ROLE_KEY');
    let userId: string;
    let userClient: any;

    if (isServiceRole) {
      if (!body.user_id) {
        throw new Error('user_id is required in request body when invoking via Service Role');
      }
      userId = body.user_id;
      console.log('[YOUTUBE_PUBLISH] Invoked via Service Role. Acting as user:', userId);
      userClient = createClient(supabaseUrl, Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? '');
    } else {
      try {
        userId = getUserIdFromToken(authHeader);
        console.log('[YOUTUBE_PUBLISH] Authenticated user:', userId);
      } catch (tokenErr: any) {
        console.log('[YOUTUBE_PUBLISH] Token validation failed:', tokenErr.message);
        throw new Error(`Token validation failed: ${tokenErr.message}`);
      }
      userClient = createClient(supabaseUrl, supabaseAnonKey, {
        global: { headers: { Authorization: authHeader } }
      });
    }

    const { data: account, error: accountError } = await userClient
      .from('linked_accounts')
      .select('access_token, token_expires_at, refresh_token')
      .eq('platform', 'youtube')
      .eq('user_id', userId)
      .single()

    console.log('[YOUTUBE_PUBLISH] Account lookup:', { found: !!account, error: accountError?.message })
    if (accountError || !account?.access_token) {
      throw new Error(`Connected YouTube account not found: ${accountError?.message || 'no account'}`)
    }

    let accessToken = account.access_token

    // Helper function to refresh YouTube token using refresh_token
    async function refreshYouTubeToken(): Promise<string> {
      if (!account?.refresh_token) {
        throw new Error('No refresh token found in database for YouTube integration')
      }
      console.log('[YOUTUBE_PUBLISH] Attempting to refresh Google OAuth token using refresh_token...')
      const GOOGLE_CLIENT_ID = Deno.env.get('GOOGLE_CLIENT_ID')
      const GOOGLE_CLIENT_SECRET = Deno.env.get('GOOGLE_CLIENT_SECRET')
      
      if (!GOOGLE_CLIENT_ID || !GOOGLE_CLIENT_SECRET) {
        throw new Error('GOOGLE_CLIENT_ID or GOOGLE_CLIENT_SECRET not configured')
      }

      const tokenRes = await fetch('https://oauth2.googleapis.com/token', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded'
        },
        body: new URLSearchParams({
          client_id: GOOGLE_CLIENT_ID,
          client_secret: GOOGLE_CLIENT_SECRET,
          refresh_token: account.refresh_token,
          grant_type: 'refresh_token'
        })
      })

      const tokenData = await tokenRes.json()
      if (tokenData.error) {
        throw new Error(tokenData.error_description || tokenData.error)
      }

      const newAccessToken = tokenData.access_token
      const supabaseServiceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
      const systemClient = createClient(supabaseUrl, supabaseServiceRoleKey)
      
      const expiresAt = tokenData.expires_in
        ? new Date(Date.now() + Number(tokenData.expires_in) * 1000).toISOString()
        : new Date(Date.now() + 3600 * 1000).toISOString()

      const { error: updateError } = await systemClient
        .from('linked_accounts')
        .update({
          access_token: newAccessToken,
          token_expires_at: expiresAt,
          updated_at: new Date().toISOString()
        })
        .eq('platform', 'youtube')
        .eq('user_id', userId)

      if (updateError) {
        console.error('[YOUTUBE_PUBLISH] Failed to update linked_accounts with refreshed token:', updateError.message)
      } else {
        console.log('[YOUTUBE_PUBLISH] Successfully updated linked_accounts with refreshed token')
      }

      return newAccessToken
    }

    let tokenExpired = false
    if (account.token_expires_at) {
      const tokenExpiry = new Date(account.token_expires_at)
      if (!isNaN(tokenExpiry.getTime()) && tokenExpiry <= new Date()) {
        tokenExpired = true
      }
    }

    if (tokenExpired) {
      try {
        accessToken = await refreshYouTubeToken()
      } catch (refreshErr: any) {
        console.error('[YOUTUBE_PUBLISH] Initial token refresh failed:', refreshErr.message)
        await createInAppNotificationIfAllowed(
          userClient,
          userId,
          'YouTube account authorization expired',
          'Your YouTube connection token has expired and refresh failed. Reconnect your account in settings.'
        )
        throw new Error(`YouTube token expired & refresh failed: ${refreshErr.message}`)
      }
    }

    const sourceUrl = video_url || media_url

    console.log('[YOUTUBE_PUBLISH] Fetching media from:', sourceUrl)
    const mediaResponse = await fetch(sourceUrl)
    console.log('[YOUTUBE_PUBLISH] Media fetch status:', mediaResponse.status)
    if (!mediaResponse.ok) {
      throw new Error(`Failed to fetch media for upload: ${mediaResponse.status} ${mediaResponse.statusText}`)
    }

    const mediaBuffer = new Uint8Array(await mediaResponse.arrayBuffer())
    console.log('[YOUTUBE_PUBLISH] Media buffer size:', mediaBuffer.length)

    let initResponse = await fetch(
      'https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status',
      {
        method: 'POST',
        headers: {
          Authorization: `Bearer ${accessToken}`,
          'Content-Type': 'application/json; charset=UTF-8',
          'X-Upload-Content-Type': 'video/*',
        },
        body: JSON.stringify({
          snippet: {
            title: title || content || 'Posted via SocialPulse',
            description: content || '',
            categoryId: '22',
          },
          status: {
            privacyStatus: privacy_status || 'public',
            publishAt: publish_at || null,
            selfDeclaredMadeForKids: false,
          },
        }),
      }
    )

    console.log('[YOUTUBE_PUBLISH] YouTube init response status:', initResponse.status)
    
    // If we get a 401 Unauthorized, try to refresh the token on the fly and retry once
    if (initResponse.status === 401) {
      console.log('[YOUTUBE_PUBLISH] Received 401 on init. Attempting on-the-fly token refresh...')
      try {
        accessToken = await refreshYouTubeToken()
        console.log('[YOUTUBE_PUBLISH] Token refreshed. Retrying YouTube init...')
        initResponse = await fetch(
          'https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status',
          {
            method: 'POST',
            headers: {
              Authorization: `Bearer ${accessToken}`,
              'Content-Type': 'application/json; charset=UTF-8',
              'X-Upload-Content-Type': 'video/*',
            },
            body: JSON.stringify({
              snippet: {
                title: title || content || 'Posted via SocialPulse',
                description: content || '',
                categoryId: '22',
              },
              status: {
                privacyStatus: privacy_status || 'public',
                publishAt: publish_at || null,
                selfDeclaredMadeForKids: false,
              },
            }),
          }
        )
        console.log('[YOUTUBE_PUBLISH] Retried YouTube init response status:', initResponse.status)
      } catch (refreshErr: any) {
        console.error('[YOUTUBE_PUBLISH] On-the-fly token refresh/retry failed:', refreshErr.message)
      }
    }

    if (!initResponse.ok) {
      const errorBody = await initResponse.text()
      console.log('[YOUTUBE_PUBLISH] YouTube init error:', errorBody)
      if (initResponse.status === 401 || errorBody.toLowerCase().includes('invalid credentials')) {
        await createInAppNotificationIfAllowed(
          userClient,
          userId,
          'YouTube account authorization expired',
          'Your YouTube access token is invalid or expired. Reconnect your account in dashboard settings to continue publishing.'
        )
      }
      throw new Error(`YouTube init failed: ${initResponse.status} - ${errorBody}`)
    }

    const uploadUrl = initResponse.headers.get('Location')
    console.log('[YOUTUBE_PUBLISH] Upload URL obtained:', !!uploadUrl)
    if (!uploadUrl) {
      throw new Error('Missing YouTube upload location')
    }

    const uploadResponse = await fetch(uploadUrl, {
      method: 'PUT',
      headers: {
        Authorization: `Bearer ${accessToken}`,
        'Content-Type': 'video/*',
        'Content-Length': String(mediaBuffer.length),
      },
      body: mediaBuffer,
    })

    console.log('[YOUTUBE_PUBLISH] YouTube upload response status:', uploadResponse.status)
    if (!uploadResponse.ok) {
      const uploadError = await uploadResponse.text()
      console.log('[YOUTUBE_PUBLISH] YouTube upload error:', uploadError)
      if (uploadResponse.status === 401 || uploadError.toLowerCase().includes('invalid credentials')) {
        await createInAppNotificationIfAllowed(
          userClient,
          userId,
          'YouTube account authorization expired',
          'Your YouTube access token is invalid or expired. Reconnect your account in dashboard settings to continue publishing.'
        )
      }
      throw new Error(`YouTube upload failed: ${uploadResponse.status} - ${uploadError}`)
    }

    const result = await uploadResponse.json()
    console.log('[YOUTUBE_PUBLISH] YouTube response video_id:', result?.id)
    if (!result?.id) {
      throw new Error('YouTube upload did not return a video id')
    }

    return new Response(
      JSON.stringify({
        success: true,
        video_id: result.id,
        url: `https://www.youtube.com/watch?v=${result.id}`,
      }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' } }
    )
  } catch (error: any) {
    console.error('[YOUTUBE_PUBLISH] Error:', error)
    
    // Determine appropriate status code based on error type
    let statusCode = 500
    const errorMsg = error.message || 'YouTube publish failed'
    
    if (errorMsg.includes('required') || 
        errorMsg.includes('not found') || 
        errorMsg.includes('Unable to authenticate') ||
        errorMsg.includes('authorization')) {
      statusCode = 400  // Client error for missing credentials/auth
    }
    
    return new Response(
      JSON.stringify({ success: false, error: errorMsg }),
      { status: statusCode, headers: { ...corsHeaders, 'Content-Type': 'application/json' } }
    )
  }
})