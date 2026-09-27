import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.38.4'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') return new Response('ok', { headers: corsHeaders })

  const url = new URL(req.url)
  const code = url.searchParams.get('code')
  const stateStr = url.searchParams.get('state')

  console.log('[CALLBACK] Received code and state');

  if (!code || !stateStr) return new Response('Missing code or state', { status: 400, headers: corsHeaders })

  try {
    const state = JSON.parse(decodeURIComponent(stateStr));
    const { user_id, origin } = state;
    console.log('[CALLBACK] Parsed user_id:', user_id, 'origin:', origin);

    const GOOGLE_CLIENT_ID = Deno.env.get('GOOGLE_CLIENT_ID')
    const GOOGLE_CLIENT_SECRET = Deno.env.get('GOOGLE_CLIENT_SECRET')
    const REDIRECT_URI = 'https://mgurbmoubtqzsgfdlgur.supabase.co/functions/v1/youtube-callback'

    // 1. Exchange code for tokens
    console.log('[CALLBACK] Exchanging code for tokens...');
    const tokenRes = await fetch('https://oauth2.googleapis.com/token', {
      method: 'POST',
      body: new URLSearchParams({
        client_id: GOOGLE_CLIENT_ID!,
        client_secret: GOOGLE_CLIENT_SECRET!,
        code,
        grant_type: 'authorization_code',
        redirect_uri: REDIRECT_URI
      })
    })
    const tokenData = await tokenRes.json()
    if (tokenData.error) {
      console.error('[CALLBACK] Token Error:', tokenData);
      throw new Error(tokenData.error_description || tokenData.error)
    }
    console.log('[CALLBACK] Tokens received');

    // 2. Fetch Channel Info
    console.log('[CALLBACK] Fetching channel info...');
    const channelRes = await fetch('https://www.googleapis.com/youtube/v3/channels?part=snippet&mine=true', {
      headers: { Authorization: `Bearer ${tokenData.access_token}` }
    })
    const channelData = await channelRes.json()
    console.log('[CALLBACK] Channel data response:', JSON.stringify(channelData));
    const channel = channelData.items?.[0]
    if (!channel) throw new Error('No YouTube channel found')

    // 3. Upsert to Database
    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    const { error: upsertError } = await supabaseClient
      .from('linked_accounts')
      .upsert({
        user_id,
        platform: 'youtube',
        connected: true,
        username: channel.snippet.title,
        external_id: channel.id,
        access_token: tokenData.access_token,
        refresh_token: tokenData.refresh_token,
        token_expires_at: tokenData.expires_in
          ? new Date(Date.now() + Number(tokenData.expires_in) * 1000).toISOString()
          : undefined,
        avatar_url: channel.snippet.thumbnails.default.url,
        updated_at: new Date().toISOString()
      }, { onConflict: 'user_id,platform' })

    if (upsertError) throw upsertError

    console.log('[CALLBACK] Successfully upserted to database');
    return Response.redirect(`${origin}/dashboard/settings/accounts?success=true&platform=youtube`, 302)
  } catch (err: any) {
    console.error('[CALLBACK] Fatal Error:', err)
    // Attempt to parse origin for error redirect, fallback to production if parsing fails
    let errorOrigin = 'https://velo.tedoraltd.com';
    try {
      const state = JSON.parse(decodeURIComponent(stateStr));
      if (state.origin) errorOrigin = state.origin;
    } catch (e) { }

    return Response.redirect(`${errorOrigin}/dashboard/settings/accounts?success=false&error=${encodeURIComponent(err.message)}`, 302)
  }
})
