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
    const query = url.searchParams.get('query')
    const page = url.searchParams.get('page') || '1'
    const pageSize = url.searchParams.get('page_size') || '30'

    const apiKey = Deno.env.get('FREESOUND_API_KEY')
    if (!apiKey) throw new Error('FREESOUND_API_KEY not set')

    // Freesound Search API
    const fetchUrl = new URL('https://freesound.org/apiv2/search/text/')
    
    // Freesound often fails with * query now, so we use a sensible default
    const safeQuery = (!query || query === '*') ? 'ambient' : query;
    fetchUrl.searchParams.set('query', safeQuery)
    
    if (!query || query === '*') {
      fetchUrl.searchParams.set('sort', 'rating_desc')
    }

    fetchUrl.searchParams.set('page', page)
    fetchUrl.searchParams.set('page_size', pageSize)
    fetchUrl.searchParams.set('fields', 'id,name,previews,duration,username,type')

    console.log(`[FREESOUND] Querying: ${fetchUrl.toString()}`)

    const res = await fetch(fetchUrl.toString(), {
      headers: {
        'Authorization': `Token ${apiKey}`
      }
    })
    
    if (!res.ok) {
      const errorData = await res.text()
      console.error(`[FREESOUND] API Error: ${res.status}`, errorData)
      throw new Error(`Freesound API error: ${res.status} - ${errorData}`)
    }

    const data = await res.json()
    const supabaseUrl = Deno.env.get('SUPABASE_URL') || 'https://mgurbmoubtqzsgfdlgur.supabase.co'

    // Transform Freesound response to DesignCombo IAudio format with proxied preview URLs
    const soundEffects = (data.results || []).map((s: any) => {
      const rawSrc = s.previews?.['preview-hq-mp3'] || s.previews?.['preview-lq-mp3'] || ''
      const proxiedSrc = rawSrc
        ? `${supabaseUrl}/functions/v1/proxy-audio?url=${encodeURIComponent(rawSrc)}`
        : ''
      return {
        id: `freesound_${s.id}`,
        details: {
          src: proxiedSrc,
        },
        name: s.name,
        type: 'audio',
        metadata: {
          author: s.username,
          duration: s.duration,
          freesound_id: s.id,
          raw_src: rawSrc,
        }
      }
    })

    const result = {
      soundEffects,
      pagination: {
        hasMore: !!data.next,
        total: data.count
      }
    }

    return new Response(JSON.stringify(result), {
      headers: { 
        ...corsHeaders, 
        'Content-Type': 'application/json',
        'Cross-Origin-Resource-Policy': 'cross-origin' 
      }
    })
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400,
    })
  }
})
