import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type, range',
  'Access-Control-Allow-Methods': 'GET, HEAD, OPTIONS',
  'Access-Control-Expose-Headers': 'Content-Length, Content-Range, Accept-Ranges, Content-Type',
}

serve(async (req) => {
  // Handle CORS Preflight
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const url = new URL(req.url)
    const targetUrlParam = url.searchParams.get('url')
    
    if (!targetUrlParam) {
      throw new Error('URL query parameter is required')
    }

    const decodedUrl = decodeURIComponent(targetUrlParam)

    const outgoingHeaders: Record<string, string> = {
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36',
    }

    const range = req.headers.get('range')
    if (range) {
      outgoingHeaders['range'] = range
    }

    const response = await fetch(decodedUrl, {
      method: req.method === 'HEAD' ? 'HEAD' : 'GET',
      headers: outgoingHeaders,
    })

    if (!response.ok && response.status !== 206) {
      throw new Error(`Failed to fetch target audio: status ${response.status}`)
    }

    const headers = new Headers(corsHeaders)
    headers.set('Content-Type', response.headers.get('Content-Type') || 'audio/mpeg')
    headers.set('Cross-Origin-Resource-Policy', 'cross-origin')
    
    const contentLength = response.headers.get('Content-Length')
    if (contentLength) headers.set('Content-Length', contentLength)
    
    const contentRange = response.headers.get('Content-Range')
    if (contentRange) headers.set('Content-Range', contentRange)
    
    const acceptRanges = response.headers.get('Accept-Ranges') || 'bytes'
    headers.set('Accept-Ranges', acceptRanges)

    return new Response(response.body, {
      status: response.status,
      headers,
    })
  } catch (error: any) {
    console.error('❌ proxy-audio error:', error)
    return new Response(JSON.stringify({ error: error.message }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400,
    })
  }
})
