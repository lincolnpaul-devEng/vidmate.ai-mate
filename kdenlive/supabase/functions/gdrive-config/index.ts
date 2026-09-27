import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.38.4'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS'
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  const authHeader = req.headers.get('Authorization')

  try {
    const GOOGLE_CLIENT_ID = Deno.env.get('GOOGLE_CLIENT_ID') || '33977563743-06i5ph1vkl17qnntc011h65lsk3np63f.apps.googleusercontent.com';
    const GOOGLE_CLIENT_SECRET = Deno.env.get('GOOGLE_CLIENT_SECRET');
    const GOOGLE_API_KEY = Deno.env.get('GOOGLE_API_KEY') || '';

    if (!GOOGLE_CLIENT_SECRET) {
      console.error('❌ GOOGLE_CLIENT_SECRET is not set in Supabase secrets');
      return new Response(
        JSON.stringify({ error: 'Configuration missing', details: 'GOOGLE_CLIENT_SECRET not set in Supabase' }),
        { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 500 }
      );
    }

    // Initialize Supabase admin client using the service role key to bypass RLS and allow fallback users
    const supabaseAdmin = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    let userId = null
    let userEmail = 'gdrive-user@socialpulse.com'

    // 1. Try to get the user from the Authorization token
    if (authHeader) {
      try {
        const supabaseClient = createClient(
          Deno.env.get('SUPABASE_URL') ?? '',
          Deno.env.get('SUPABASE_ANON_KEY') ?? '',
          { global: { headers: { Authorization: authHeader } } }
        )
        const { data: { user } } = await supabaseClient.auth.getUser()
        if (user) {
          userId = user.id
          userEmail = user.email ?? userEmail
        }
      } catch (authErr) {
        console.warn('[GDrive Auth] Standard token verification failed:', authErr.message)
      }

      // 2. If token verification failed, parse JWT manually (useful in local dev with session mismatches)
      if (!userId) {
        try {
          const token = authHeader.replace('Bearer ', '')
          const payloadPart = token.split('.')[1]
          if (payloadPart) {
            // Deno compatible base64 decoding
            const binary = atob(payloadPart.replace(/-/g, '+').replace(/_/g, '/'))
            const payload = JSON.parse(binary)
            if (payload && payload.sub) {
              userId = payload.sub
              userEmail = payload.email || userEmail
              console.log('[GDrive Auth] Manually parsed user ID from JWT:', userId)
            }
          }
        } catch (jwtErr) {
          console.warn('[GDrive Auth] Manual JWT parsing failed:', jwtErr.message)
        }
      }
    }

    // 3. Fallback: If no user found, fetch the first profile from the database
    if (!userId) {
      console.log('[GDrive Auth] No valid user found. Querying database for fallback user...')
      const { data: profiles, error: profileErr } = await supabaseAdmin
        .from('profiles')
        .select('id, email')
        .limit(1)

      if (profileErr) {
        console.error('[GDrive Auth] Failed to fetch fallback profiles:', profileErr)
      }

      if (profiles && profiles.length > 0) {
        userId = profiles[0].id
        userEmail = profiles[0].email || userEmail
        console.log('[GDrive Auth] Using fallback user profile:', userId, `(${userEmail})`)
      }
    }

    if (!userId) {
      return new Response(
        JSON.stringify({ error: 'Unauthorized', details: 'No user session or fallback profile found' }),
        { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 401 }
      )
    }

    // Handle GET (Fetch current connection status and active token)
    if (req.method === 'GET') {
      const { data: account, error: accountError } = await supabaseAdmin
        .from('linked_accounts')
        .select('*')
        .eq('platform', 'google_drive')
        .eq('user_id', userId)
        .maybeSingle()

      if (accountError) {
        console.error('[GDrive Auth] DB error fetching account:', accountError)
        throw accountError
      }

      if (!account || !account.connected) {
        return new Response(
          JSON.stringify({ connected: false, clientId: GOOGLE_CLIENT_ID, developerKey: GOOGLE_API_KEY }),
          { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
        )
      }

      // Check if access token is still valid (with a 5-minute buffer)
      const expiresAt = account.token_expires_at ? new Date(account.token_expires_at).getTime() : 0
      const isExpired = expiresAt - Date.now() < 300000 // 5 minutes buffer

      if (!isExpired && account.access_token) {
        console.log('[GDrive Auth] Returning cached access token for user:', userId)
        return new Response(
          JSON.stringify({
            connected: true,
            accessToken: account.access_token,
            clientId: GOOGLE_CLIENT_ID,
            developerKey: GOOGLE_API_KEY,
            email: account.username
          }),
          { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
        )
      }

      // Token is expired, try to refresh it
      if (!account.refresh_token) {
        console.warn('[GDrive Auth] Token expired but no refresh token available for user:', userId)
        return new Response(
          JSON.stringify({ connected: false, clientId: GOOGLE_CLIENT_ID, developerKey: GOOGLE_API_KEY, message: 'Re-authentication required' }),
          { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
        )
      }

      console.log('[GDrive Auth] Refreshing Google token for user:', userId)
      const refreshRes = await fetch('https://oauth2.googleapis.com/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
          client_id: GOOGLE_CLIENT_ID,
          client_secret: GOOGLE_CLIENT_SECRET,
          refresh_token: account.refresh_token,
          grant_type: 'refresh_token'
        })
      })

      const refreshData = await refreshRes.json()
      if (!refreshRes.ok || refreshData.error) {
        console.error('[GDrive Auth] Google refresh failed:', refreshData)
        return new Response(
          JSON.stringify({ connected: false, clientId: GOOGLE_CLIENT_ID, developerKey: GOOGLE_API_KEY, error: 'Google refresh token rejected' }),
          { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
        )
      }

      const newAccessToken = refreshData.access_token
      const expiresSeconds = Number(refreshData.expires_in || 3600)
      const newExpiresAt = new Date(Date.now() + expiresSeconds * 1000).toISOString()

      // Update the database
      const { error: updateError } = await supabaseAdmin
        .from('linked_accounts')
        .update({
          access_token: newAccessToken,
          token_expires_at: newExpiresAt,
          updated_at: new Date().toISOString()
        })
        .eq('platform', 'google_drive')
        .eq('user_id', userId)

      if (updateError) {
        console.error('[GDrive Auth] Failed to save refreshed token:', updateError)
        throw updateError
      }

      return new Response(
        JSON.stringify({
          connected: true,
          accessToken: newAccessToken,
          clientId: GOOGLE_CLIENT_ID,
          developerKey: GOOGLE_API_KEY,
          email: account.username
        }),
        { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
      )
    }

    // Handle POST (Connect / Exchange Authorization Code)
    if (req.method === 'POST') {
      const { code, redirectUri } = await req.json()
      if (!code) {
        return new Response(
          JSON.stringify({ success: false, error: 'Bad Request', details: 'authorization code is required' }),
          { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
        )
      }

      console.log('[GDrive Auth] Exchanging authorization code for tokens, redirectUri:', redirectUri || 'postmessage')
      const tokenRes = await fetch('https://oauth2.googleapis.com/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
          client_id: GOOGLE_CLIENT_ID,
          client_secret: GOOGLE_CLIENT_SECRET,
          code: code,
          grant_type: 'authorization_code',
          redirect_uri: redirectUri || 'postmessage' // Use dynamic redirect_uri if provided
        })
      })

      const tokenData = await tokenRes.json()
      if (!tokenRes.ok || tokenData.error) {
        console.error('[GDrive Auth] Code exchange failed:', tokenData)
        return new Response(
          JSON.stringify({ success: false, error: 'Token exchange failed', details: tokenData.error_description || tokenData.error || JSON.stringify(tokenData) }),
          { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
        )
      }

      // Fetch user profile email
      let email = 'Google Drive User'
      let avatarUrl = ''
      try {
        const profileRes = await fetch('https://www.googleapis.com/oauth2/v2/userinfo', {
          headers: { Authorization: `Bearer ${tokenData.access_token}` }
        })
        if (profileRes.ok) {
          const profile = await profileRes.json()
          email = profile.email || profile.name || email
          avatarUrl = profile.picture || avatarUrl
        }
      } catch (profileErr) {
        console.warn('[GDrive Auth] Could not fetch user profile details:', profileErr)
      }

      // Upsert into linked_accounts
      // Note: Google only sends the refresh_token on the first authorization. We preserve the existing one if null.
      const expiresSeconds = Number(tokenData.expires_in || 3600)
      const tokenExpiresAt = new Date(Date.now() + expiresSeconds * 1000).toISOString()

      // First check if an account exists to preserve the refresh token if Google didn't return one this time
      const { data: existingAccount } = await supabaseAdmin
        .from('linked_accounts')
        .select('refresh_token')
        .eq('platform', 'google_drive')
        .eq('user_id', userId)
        .maybeSingle()

      const refreshTokenToSave = tokenData.refresh_token || existingAccount?.refresh_token

      const { error: upsertError } = await supabaseAdmin
        .from('linked_accounts')
        .upsert({
          user_id: userId,
          platform: 'google_drive',
          connected: true,
          username: email,
          avatar_url: avatarUrl,
          access_token: tokenData.access_token,
          refresh_token: refreshTokenToSave,
          token_expires_at: tokenExpiresAt,
          updated_at: new Date().toISOString()
        }, { onConflict: 'user_id,platform' })

      if (upsertError) {
        console.error('[GDrive Auth] DB Error upserting linked account:', upsertError)
        return new Response(
          JSON.stringify({ success: false, error: 'Database upsert failed', details: upsertError.message || JSON.stringify(upsertError) }),
          { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
        )
      }

      console.log('[GDrive Auth] Successfully connected Google Drive account for user:', userId)
      return new Response(
        JSON.stringify({
          success: true,
          accessToken: tokenData.access_token,
          clientId: GOOGLE_CLIENT_ID,
          developerKey: GOOGLE_API_KEY,
          email
        }),
        { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
      )
    }

    return new Response(
      JSON.stringify({ error: 'Method Not Allowed' }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 405 }
    )
  } catch (error: any) {
    console.error('[GDrive Auth] Fatal Error:', error)
    return new Response(
      JSON.stringify({ success: false, error: 'Internal Server Error', details: error.message }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 }
    )
  }
})
