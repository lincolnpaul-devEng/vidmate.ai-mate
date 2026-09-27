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
    const body = await req.json()
    const {
      media_url,
      video_url,
      access_token: providedToken,
      content,
      title,
      settings = {}
    } = body

    const {
      privacy_level,
      disable_duet,
      disable_comment,
      disable_stitch,
      is_aigc
    } = { ...body, ...settings }

    const authHeader = req.headers.get('Authorization')
    if (!authHeader && !providedToken) throw new Error('Authorization required')

    let access_token = providedToken

    if (!access_token && authHeader) {
      const supabaseClient = createClient(
        Deno.env.get('SUPABASE_URL') ?? '',
        Deno.env.get('SUPABASE_ANON_KEY') ?? '',
        { global: { headers: { Authorization: authHeader } } }
      )

      const { data: account, error: accountError } = await supabaseClient
        .from('linked_accounts')
        .select('access_token')
        .eq('platform', 'tiktok')
        .single()

      if (accountError || !account?.access_token) {
        throw new Error('TikTok account connection not found')
      }
      access_token = account.access_token
    }

    // 1. Rewrite Cloudinary URL to Proxy URL and enforce .mp4 extension for TikTok validation
    let FINAL_URL = video_url || media_url;
    if (FINAL_URL.includes('res.cloudinary.com')) {
      FINAL_URL = FINAL_URL.replace('https://res.cloudinary.com', 'https://velo.tedoraltd.com/proxy/media');
      // Cloudinary returns URLs without extensions sometimes, or .mov.
      // TikTok strictly demands .mp4 or .webm in the URL string.
      FINAL_URL = FINAL_URL.replace(/\.(mp4|mov|m4v|webm)$/i, '');
      FINAL_URL += '.mp4';
    }

    console.log(`[TIKTOK_PUBLISH] Initiating PULL_FROM_URL for ${FINAL_URL}`)

    // 2. Note: Creator Info was previously checked by the frontend modal to comply with TikTok v2 UX guidelines.
    const dynamicPrivacyLevel = privacy_level || 'SELF_ONLY';

    // 3. Initialize Video Post
    const initRes = await fetch('https://open.tiktokapis.com/v2/post/publish/video/init/', {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${access_token}`,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        post_info: (() => {
          const info: any = {
            title: (title || content || 'Posted via SocialPulse').substring(0, 150),
            privacy_level: dynamicPrivacyLevel,
          };
          if (disable_duet) info.disable_duet = disable_duet;
          if (disable_comment) info.disable_comment = disable_comment;
          if (disable_stitch) info.disable_stitch = disable_stitch;
          if (is_aigc) info.is_aigc = is_aigc;
          return info;
        })(),
        source_info: {
          source: 'PULL_FROM_URL',
          video_url: FINAL_URL,
        }
      })
    })

    const initData = await initRes.json()
    if (initData.error && initData.error.code !== 'ok') {
      const payloadDump = JSON.stringify({
        post_info: (() => {
          const info: any = {
            title: (title || 'Posted via SocialPulse').substring(0, 150),
            privacy_level: dynamicPrivacyLevel,
          };
          if (disable_duet) info.disable_duet = disable_duet;
          if (disable_comment) info.disable_comment = disable_comment;
          if (disable_stitch) info.disable_stitch = disable_stitch;
          if (is_aigc) info.is_aigc = is_aigc;
          return info;
        })(), source_info: { source: 'PULL_FROM_URL', video_url: FINAL_URL }
      });

      const err = new Error(`TikTok Publish Error: ${initData.error.message || initData.error.code}. RAW DUMP: ${JSON.stringify(initData.error)}. SENT PAYLOAD: ${payloadDump}`) as any;
      err.log_id = initData.error.log_id;
      throw err;
    }

    const publishId = initData.data?.publish_id

    // 2. Respond to client
    return new Response(JSON.stringify({
      success: true,
      publish_id: publishId,
      message: 'Video publishing initiated successfully'
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })

  } catch (error) {
    console.error('TikTok Publish Error:', error)
    return new Response(JSON.stringify({
      success: false,
      error: error.message,
      log_id: error.log_id || 'N/A'
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }
})
