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
    const { user_id } = await req.json()
    if (!user_id) throw new Error('User ID is required')

    const supabase = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // 1. Get/Refresh TikTok Token
    console.log(`[TIKTOK_CREATOR_INFO] Refreshing token for user ${user_id}...`)
    const { data: refreshData, error: refreshError } = await supabase.functions.invoke('refresh-tiktok-token', {
      body: { user_id }
    })

    if (refreshError || !refreshData?.success) {
      const msg = refreshError?.message || refreshData?.error || 'Token refresh failed';
      console.error(`[TIKTOK_CREATOR_INFO] Refresh Failed:`, msg)
      throw new Error(msg)
    }

    const accessToken = refreshData.access_token

    // 2. Query Creator Info
    const infoRes = await fetch('https://open.tiktokapis.com/v2/post/publish/creator_info/query/', {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${accessToken}`,
        'Content-Type': 'application/json',
      }
    });

    const infoData = await infoRes.json()
    console.log('[TIKTOK_CREATOR_INFO] API Response Status:', infoRes.status)

    if (infoData.error && infoData.error.code !== 'ok') {
      console.error('[TIKTOK_CREATOR_INFO] TikTok API Error Schema:', infoData.error)
      throw new Error(`TikTok API Rejected: ${infoData.error.message || infoData.error.code}`)
    }

    if (!infoData.data) {
      console.error('[TIKTOK_CREATOR_INFO] Empty data payload:', infoData)
      throw new Error('TikTok returned success but no data payload.')
    }

    return new Response(JSON.stringify({ 
      success: true, 
      data: infoData.data 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })

  } catch (error) {
    console.error('TikTok Creator Info Error:', error.message)
    return new Response(JSON.stringify({ 
      success: false, 
      error: error.message 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })
  }
})
