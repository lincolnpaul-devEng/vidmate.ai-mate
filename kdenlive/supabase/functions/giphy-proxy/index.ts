import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

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
    const action = url.searchParams.get('action') // search, trending, or proxy
    const query = url.searchParams.get('q')
    const limit = url.searchParams.get('limit') || '24'
    const rating = url.searchParams.get('rating') || 'g'

    const apiKey = Deno.env.get('GIPHY_API_KEY')
    if (!apiKey) throw new Error('GIPHY_API_KEY not set')

    // Proxy the image bytes and add CORP header for COEP compliance
    if (action === 'proxy') {
       const imageUrl = url.searchParams.get('url')
       if (!imageUrl) throw new Error('No URL provided')
       
       const imgRes = await fetch(imageUrl)
       const blob = await imgRes.blob()
       
       return new Response(blob, {
         headers: {
           ...corsHeaders,
           'Content-Type': imgRes.headers.get('Content-Type') || 'image/gif',
           'Cross-Origin-Resource-Policy': 'cross-origin',
           'Cache-Control': 'public, max-age=31536000'
         }
       })
    }

    // Proxy the API
    let fetchUrl = ""
    if (action === 'trending') {
      fetchUrl = `https://api.giphy.com/v1/stickers/trending?api_key=${apiKey}&limit=${limit}&rating=${rating}`
    } else {
      fetchUrl = `https://api.giphy.com/v1/stickers/search?api_key=${apiKey}&q=${encodeURIComponent(query || "")}&limit=${limit}&rating=${rating}`
    }

    const res = await fetch(fetchUrl)
    
    if (!res.ok) {
      const errorText = await res.text()
      throw new Error(`GIPHY API error: ${res.status} ${errorText}`)
    }

    const data = await res.json()

    return new Response(JSON.stringify(data), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    })
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400,
    })
  }
})
