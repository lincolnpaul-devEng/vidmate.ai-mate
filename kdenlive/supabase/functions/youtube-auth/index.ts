import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') return new Response('ok', { headers: corsHeaders })

  const { user_id } = await req.json()
  const origin = req.headers.get('origin') || 'https://velo.tedoraltd.com'
  if (!user_id) return new Response(JSON.stringify({ error: 'Missing user_id' }), { status: 400, headers: { ...corsHeaders, 'Content-Type': 'application/json' } })

  const GOOGLE_CLIENT_ID = Deno.env.get('GOOGLE_CLIENT_ID')
  const REDIRECT_URI = 'https://mgurbmoubtqzsgfdlgur.supabase.co/functions/v1/youtube-callback'

  const scopes = [
    'https://www.googleapis.com/auth/youtube.upload',
    'https://www.googleapis.com/auth/youtube',
    'https://www.googleapis.com/auth/youtube.force-ssl',
    'https://www.googleapis.com/auth/yt-analytics.readonly'
  ]

  const authUrl = new URL('https://accounts.google.com/o/oauth2/v2/auth')

  if (!GOOGLE_CLIENT_ID) {
    console.error('[YOUTUBE_AUTH] GOOGLE_CLIENT_ID is missing!');
    return new Response(JSON.stringify({ error: 'Server configuration error' }), { status: 500, headers: { ...corsHeaders, 'Content-Type': 'application/json' } });
  }

  authUrl.searchParams.append('client_id', GOOGLE_CLIENT_ID!)
  authUrl.searchParams.append('redirect_uri', REDIRECT_URI)
  authUrl.searchParams.append('response_type', 'code')
  authUrl.searchParams.append('scope', scopes.join(' '))
  authUrl.searchParams.append('access_type', 'offline')
  authUrl.searchParams.append('prompt', 'consent')
  authUrl.searchParams.append('state', JSON.stringify({ user_id, origin }))

  console.log('[YOUTUBE_AUTH] Generated Auth URL:', authUrl.toString());

  return new Response(JSON.stringify({ url: authUrl.toString() }), {
    headers: { ...corsHeaders, 'Content-Type': 'application/json' },
  })
})
