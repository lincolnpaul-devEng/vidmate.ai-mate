import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.38.4"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const { platform, content, media_url, media_type, action, comment_id } = await req.json()
    const authHeader = req.headers.get('Authorization')

    if (!authHeader) throw new Error('Authorization required')

    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_ANON_KEY') ?? '',
      { global: { headers: { Authorization: authHeader } } }
    )

    // Fetch the linked account details (token and external ID)
    const { data: account, error: accountError } = await supabaseClient
      .from('linked_accounts')
      .select('access_token, external_id')
      .eq('platform', platform)
      .single()

    if (accountError || !account?.access_token) {
        throw new Error(`Account connection not found for ${platform}`)
    }

    const access_token = account.access_token
    const external_id = account.external_id

    if (action === 'reply' && comment_id) {
        console.log(`[META_PUBLISH] Replying to comment ${comment_id} on ${platform}...`)
        const endpoint = platform === 'facebook' 
            ? `https://graph.facebook.com/v21.0/${comment_id}/comments`
            : `https://graph.facebook.com/v21.0/${comment_id}/replies`
            
        const res = await fetch(endpoint, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ message: content, access_token })
        })
        const data = await res.json()
        if (data.error) throw new Error(data.error.message)
        
        return new Response(JSON.stringify({ success: true, publish_id: data.id }), {
            headers: { ...corsHeaders, 'Content-Type': 'application/json' },
            status: 200
        })
    }

    console.log(`[META_PUBLISH] Publishing to ${platform} (${external_id})...`)

    if (platform === 'facebook') {
      // Facebook Page Publishing
      let endpoint = `https://graph.facebook.com/v21.0/${external_id}/feed`
      const params = new URLSearchParams()
      
      if (media_url) {
        if (media_type === 'video') {
            endpoint = `https://graph.facebook.com/v21.0/${external_id}/videos`
            params.append('file_url', media_url)
            params.append('description', content)
        } else {
            endpoint = `https://graph.facebook.com/v21.0/${external_id}/photos`
            params.append('url', media_url)
            params.append('caption', content)
        }
      } else {
        params.append('message', content)
      }
      
      params.append('access_token', access_token)
      
      const response = await fetch(`${endpoint}?${params.toString()}`, { method: 'POST' })
      const data = await response.json()
      
      if (data.error) throw new Error(data.error.message)
      
      return new Response(JSON.stringify({ success: true, publish_id: data.id || data.post_id }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 200
      })

    } else if (platform === 'instagram') {
      // Instagram Business Account Publishing (2-step)
      // 1. Create Media Container
      const containerUrl = new URL(`https://graph.facebook.com/v21.0/${external_id}/media`)
      containerUrl.searchParams.append('access_token', access_token)
      containerUrl.searchParams.append('caption', content)
      
      if (media_type === 'video') {
        containerUrl.searchParams.append('media_type', 'REELS')
        containerUrl.searchParams.append('video_url', media_url)
      } else {
        containerUrl.searchParams.append('image_url', media_url)
      }

      const containerRes = await fetch(containerUrl.toString(), { method: 'POST' })
      const containerData = await containerRes.json()
      
      if (containerData.error) throw new Error(`Container Error: ${containerData.error.message}`)
      
      const creationId = containerData.id

      // Wait a bit for processing (Instagram requirement)
      // In a real production app, we'd use a webhook to listen for 'finished' status,
      // but for this implementation, we will poll or wait slightly.
      // Instagram says: "For videos, wait until status_code is FINISHED"
      
      let status = 'IN_PROGRESS'
      let attempts = 0
      while (status !== 'FINISHED' && attempts < 10) {
          await new Promise(resolve => setTimeout(resolve, 3000)) // Wait 3s
          const statusRes = await fetch(`https://graph.facebook.com/v21.0/${creationId}?fields=status_code&access_token=${access_token}`)
          const statusData = await statusRes.json()
          status = statusData.status_code
          attempts++
          if (status === 'ERROR') throw new Error('Instagram media processing failed')
      }

      // 2. Publish Media
      const publishUrl = new URL(`https://graph.facebook.com/v21.0/${external_id}/media_publish`)
      publishUrl.searchParams.append('access_token', access_token)
      publishUrl.searchParams.append('creation_id', creationId)

      const publishRes = await fetch(publishUrl.toString(), { method: 'POST' })
      const publishData = await publishRes.json()
      
      if (publishData.error) throw new Error(`Publish Error: ${publishData.error.message}`)

      return new Response(JSON.stringify({ success: true, publish_id: publishData.id }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 200
      })
    }

    throw new Error('Unsupported platform')

  } catch (error: any) {
    console.error('[META_PUBLISH] Error:', error)
    return new Response(JSON.stringify({ success: false, error: error.message }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 500
    })
  }
})
