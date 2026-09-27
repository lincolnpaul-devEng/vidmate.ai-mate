import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.38.4'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const url = new URL(req.url)
    const clientKey = Deno.env.get('TIKTOK_CLIENT_KEY')
    const clientSecret = Deno.env.get('TIKTOK_CLIENT_SECRET')
    const REDIRECT_URI = 'https://velo.tedoraltd.com/callback/tiktok'

    // --- MODE A: INITIATE (GET) ---
    if (req.method === 'GET') {
      const user_id = url.searchParams.get('user_id')
      if (!user_id) throw new Error('Missing user_id')

      const scopes = ['user.info.basic', 'video.upload', 'video.list', 'video.publish', 'ads.manage', 'ads.read'].join(',')
      const state = encodeURIComponent(JSON.stringify({ user_id }))

      const authUrl = new URL('https://www.tiktok.com/v2/auth/authorize/')
      authUrl.searchParams.append('client_key', clientKey!)
      authUrl.searchParams.append('scope', scopes)
      authUrl.searchParams.append('response_type', 'code')
      authUrl.searchParams.append('redirect_uri', REDIRECT_URI)
      authUrl.searchParams.append('state', state)

      console.log(`[TIKTOK_CONNECT] Initiating for user: ${user_id}`)
      return new Response(null, {
        status: 302,
        headers: { 'Location': authUrl.toString(), ...corsHeaders },
      })
    }

    // --- MODE B: EXCHANGE (POST) ---
    if (req.method === 'POST') {
      const { code, code_verifier, user_id: bodyUserId } = await req.json()
      const authHeader = req.headers.get('Authorization')

      let supabaseClient;
      let userId;

      let userFromToken = null;
      if (authHeader) {
        const tempClient = createClient(
          Deno.env.get('SUPABASE_URL') ?? '',
          Deno.env.get('SUPABASE_ANON_KEY') ?? '',
          { global: { headers: { Authorization: authHeader } } }
        );
        const { data: { user } } = await tempClient.auth.getUser();
        userFromToken = user;
      }

      if (userFromToken) {
        userId = userFromToken.id;
        supabaseClient = createClient(
          Deno.env.get('SUPABASE_URL') ?? '',
          Deno.env.get('SUPABASE_ANON_KEY') ?? '',
          { global: { headers: { Authorization: authHeader } } }
        );
      } else if (bodyUserId) {
        console.log(`[TIKTOK_EXCHANGE] Using user_id fallback: ${bodyUserId}`);
        supabaseClient = createClient(
          Deno.env.get('SUPABASE_URL') ?? '',
          Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
        );
        userId = bodyUserId;
      } else {
        throw new Error('Authorization required or valid user_id missing');
      }

      console.log(`[TIKTOK_EXCHANGE] Finalizing link for user: ${userId}`)

      // 1. Exchange code for tokens
      const exchangeRes = await fetch('https://open.tiktokapis.com/v2/oauth/token/', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
          client_key: clientKey!,
          client_secret: clientSecret!,
          code: code,
          grant_type: 'authorization_code',
          redirect_uri: REDIRECT_URI,
          // code_verifier // TikTok v2 PKCE is optional but recommended if implemented
        }),
      })

      const exchangeData = await exchangeRes.json()
      if (exchangeData.error) {
        throw new Error(`TikTok Exchange Error: ${exchangeData.error_description || exchangeData.error}`)
      }

      // 2. Fetch User Profile to get username/external_id
      const profileRes = await fetch('https://open.tiktokapis.com/v2/user/info/?fields=open_id,display_name,avatar_url', {
        headers: { 'Authorization': `Bearer ${exchangeData.access_token}` }
      })
      const profileData = await profileRes.json()
      const userData = profileData.data?.user || {}

      // 3. Save to cloud Keyring
      const { error: dbError } = await supabaseClient
        .from('linked_accounts')
        .upsert({
          user_id: userId,
          platform: 'tiktok',
          connected: true,
          username: userData.display_name || 'TikTok User',
          external_id: userData.open_id,
          access_token: exchangeData.access_token,
          refresh_token: exchangeData.refresh_token,
          avatar_url: userData.avatar_url,
          updated_at: new Date().toISOString()
        }, { onConflict: 'user_id,platform' })

      if (dbError) throw dbError

      return new Response(JSON.stringify({
        success: true,
        profile: userData
      }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 200
      })
    }

    throw new Error(`Method ${req.method} not allowed`)

  } catch (error: any) {
    console.error('[TIKTOK_AUTH] failure:', error.message)
    return new Response(JSON.stringify({ success: false, error: error.message }), {
      status: 200, // Return 200 so client can read error
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }
})
