import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  const url = new URL(req.url)
  const code = url.searchParams.get('code')
  const stateStr = url.searchParams.get('state')
  const error = url.searchParams.get('error')

  if (error) {
    console.error(`[TIKTOK_CALLBACK] OAuth Error: ${error}`)
    let isMobile = false;
    try {
      const stateObj = JSON.parse(decodeURIComponent(stateStr || ''));
      if (stateObj?.source === 'mobile') isMobile = true;
    } catch (e) { }
    const redirectUrl = isMobile
      ? `socialpulse://auth-success?success=false&error=${encodeURIComponent(error)}`
      : `https://velo.tedoraltd.com/auth-success?success=false&error=${encodeURIComponent(error)}`
    return Response.redirect(redirectUrl, 302)
  }

  if (!code || !stateStr) {
    return new Response('Missing code or state', { status: 400 })
  }

  try {
    const state = JSON.parse(decodeURIComponent(stateStr))
    const { user_id, source } = state

    const clientKey = Deno.env.get('TIKTOK_CLIENT_KEY')
    const clientSecret = Deno.env.get('TIKTOK_CLIENT_SECRET')
    const REDIRECT_URI = 'https://velo.tedoraltd.com/callback/tiktok'

    console.log(`[TIKTOK_CALLBACK] Finalizing for user: ${user_id}`)

    // 1. Exchange code for Access Token and Refresh Token
    const exchangeRes = await fetch('https://open.tiktokapis.com/v2/oauth/token/', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
      },
      body: new URLSearchParams({
        client_key: clientKey!,
        client_secret: clientSecret!,
        code,
        grant_type: 'authorization_code',
        redirect_uri: REDIRECT_URI,
      }),
    })

    const exchangeData = await exchangeRes.json()
    if (exchangeData.error) throw new Error(exchangeData.error_description || exchangeData.error)

    const accessToken = exchangeData.access_token
    const refreshToken = exchangeData.refresh_token

    // 2. Fetch User Profile Info
    const profileRes = await fetch('https://open.tiktokapis.com/v2/user/info/?fields=open_id,union_id,avatar_url,display_name', {
      headers: {
        'Authorization': `Bearer ${accessToken}`
      }
    })
    const profileData = await profileRes.json()
    const userData = profileData.data?.user || {}

    // 3. Persist to Database
    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    console.log(`[TIKTOK_CALLBACK] Persisting for user: ${user_id} (${userData.display_name})`)

    // Store both access_token and refresh_token for background scheduling
    const { error: dbError } = await supabaseClient
      .from('linked_accounts')
      .upsert({
        user_id: user_id,
        platform: 'tiktok',
        connected: true,
        username: userData.display_name || 'TikTok User',
        external_id: userData.open_id,
        access_token: accessToken,
        refresh_token: refreshToken, // CRITICAL for background posting
        avatar_url: userData.avatar_url,
        updated_at: new Date().toISOString()
      }, { onConflict: 'user_id,platform' })

    if (dbError) throw dbError

    // 4. Final Jump back to the success page
    const finalSuccessUrl = source === 'mobile'
      ? `socialpulse://auth-success?platform=tiktok&success=true`
      : `https://velo.tedoraltd.com/auth-success?platform=tiktok&success=true&user_id=${user_id}`

    return new Response(null, {
      status: 302,
      headers: {
        'Location': finalSuccessUrl,
        'Access-Control-Allow-Origin': '*',
      },
    })

  } catch (err: any) {
    let isMobile = false;
    try {
      const stateObj = JSON.parse(decodeURIComponent(stateStr || ''));
      if (stateObj?.source === 'mobile') isMobile = true;
    } catch (e) { }

    const errorMessage = err?.message || 'Internal Callback Error';
    console.error('[TIKTOK_CALLBACK] Failure:', errorMessage, err);

    const errorUrl = isMobile
      ? `socialpulse://auth-success?platform=tiktok&success=false&error=${encodeURIComponent(errorMessage)}`
      : `https://velo.tedoraltd.com/auth-success?platform=tiktok&success=false&error=${encodeURIComponent(errorMessage)}`;
    return new Response(null, {
      status: 302,
      headers: {
        'Location': errorUrl,
        'Access-Control-Allow-Origin': '*',
      },
    });
  }
})
