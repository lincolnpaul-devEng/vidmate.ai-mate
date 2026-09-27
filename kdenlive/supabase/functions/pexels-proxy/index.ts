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
    const action = url.searchParams.get('action') // search or image
    const type = url.searchParams.get('type') || 'photos' 
    const query = url.searchParams.get('query')
    const page = url.searchParams.get('page') || '1'
    const perPage = url.searchParams.get('per_page') || '20'

    const apiKey = Deno.env.get('PEXELS_API_KEY')
    if (!apiKey) throw new Error('PEXELS_API_KEY not set')

    // Proxy the image bytes and add CORP header for COEP compliance
    if (action === 'image') {
       const imageUrl = url.searchParams.get('url')
       if (!imageUrl) throw new Error('No URL provided')
       
       const imgRes = await fetch(imageUrl)
       const blob = await imgRes.blob()
       
       return new Response(blob, {
         headers: {
           ...corsHeaders,
           'Content-Type': imgRes.headers.get('Content-Type') || 'image/jpeg',
           'Cross-Origin-Resource-Policy': 'cross-origin',
           'Cache-Control': 'public, max-age=31536000'
         }
       })
    }

    // Proxy the API
    const baseUrl = type === 'videos' ? 'https://api.pexels.com/videos' : 'https://api.pexels.com/v1'
    const fetchUrl = query
      ? `${baseUrl}/search?query=${encodeURIComponent(query)}&page=${page}&per_page=${perPage}`
      : `${baseUrl}/${type === 'videos' ? 'popular' : 'curated'}?page=${page}&per_page=${perPage}`

    const res = await fetch(fetchUrl, {
      headers: { Authorization: apiKey }
    })
    
    if (!res.ok) {
      const errorText = await res.text()
      throw new Error(`Pexels API error: ${res.status} ${errorText}`)
    }

    const data = await res.json()

    // Transform data to match the format expected by the frontend (DesignCombo types)
    if (type === 'videos' && data.videos) {
      data.videos = data.videos.map((v: any) => ({
        id: `pexels_video_${v.id}`,
        details: {
          src: v.video_files.find((f: any) => f.quality === 'hd' || f.quality === 'sd')?.link || v.video_files[0]?.link,
          width: v.width,
          height: v.height,
          duration: v.duration,
          fps: v.video_files[0]?.fps || 30
        },
        preview: v.video_pictures[0]?.picture || v.image,
        type: 'video',
        metadata: { pexels_id: v.id, user: v.user, video_files: v.video_files, video_pictures: v.video_pictures }
      }));
    } else if (type === 'photos' && data.photos) {
      data.photos = data.photos.map((p: any) => ({
        id: `pexels_${p.id}`,
        details: {
          src: p.src.large2x,
          width: p.width,
          height: p.height,
          photographer: p.photographer,
          photographer_url: p.photographer_url,
          alt: p.alt
        },
        preview: p.src.medium,
        type: 'image',
        metadata: { pexels_id: p.id, avg_color: p.avg_color, original_url: p.src.original }
      }));
    }

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
