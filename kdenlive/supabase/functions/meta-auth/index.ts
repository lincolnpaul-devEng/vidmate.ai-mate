import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  const url = new URL(req.url)
  // Handle both JSON body and search params for flexibility
  let platform = url.searchParams.get('platform')
  let user_id = url.searchParams.get('user_id')

  // Support JSON body (from supabase.functions.invoke)
  if (!platform && req.method === 'POST') {
    try {
      const body = await req.json()
      platform = body.platform
      user_id = body.user_id
    } catch (_) { }
  }

  if (!platform || !user_id) {
    return new Response(JSON.stringify({ error: 'Missing platform or user_id' }), {
      status: 400,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }

  const FB_APP_ID = Deno.env.get('FACEBOOK_APP_ID')
  // Use the proxy URL for the callback
  const REDIRECT_URI = 'https://velo.tedoraltd.com/callback/meta'

  const scopes = [
    'pages_show_list',
    'pages_manage_posts',
    'pages_read_engagement',
    'business_management',
    'public_profile',
    'email',
    'ads_management',
    'ads_read'
  ]

  if (platform === 'instagram') {
    scopes.push('instagram_basic', 'instagram_content_publish')
  }

  // Use state to pass platform and user_id through the OAuth flow safely
  const state = encodeURIComponent(JSON.stringify({ platform, user_id }))

  const authUrl = new URL('https://www.facebook.com/v21.0/dialog/oauth')
  authUrl.searchParams.append('client_id', FB_APP_ID!)
  authUrl.searchParams.append('redirect_uri', REDIRECT_URI)
  authUrl.searchParams.append('state', state)
  authUrl.searchParams.append('scope', scopes.join(','))
  authUrl.searchParams.append('response_type', 'code')

  console.log(`[META_CONNECT] Initiating for ${platform} (user: ${user_id})`)

  const authUrlString = authUrl.toString()

  // Check if client expects JSON (from supabase.functions.invoke or AJAX)
  const acceptHeader = req.headers.get('accept') || ''
  const isJsonRequest = acceptHeader.includes('application/json') || req.method === 'POST'

  if (isJsonRequest) {
    return new Response(JSON.stringify({ url: authUrlString }), {
      status: 200,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }

  // Otherwise redirect directly (for window.location.href)
  return new Response(null, {
    status: 302,
    headers: {
      'Location': authUrlString,
      ...corsHeaders
    },
  })
})
