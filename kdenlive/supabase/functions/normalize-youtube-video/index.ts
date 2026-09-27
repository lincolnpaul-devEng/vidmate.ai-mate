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
    const { video_id, post_id, access_token } = await req.json()
    if (!video_id || !post_id) throw new Error('video_id and post_id are required')

    // 1. Setup Supabase
    const supabase = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // 2. Call the Production Video Processor on Render
    const PROCESSOR_URL = "https://socialpulse-video-processor.onrender.com/process-video";
    const ytUrl = `https://www.youtube.com/watch?v=${video_id}`;
    
    console.log(`🚀 Sending normalization request to: ${PROCESSOR_URL}`);
    console.log(`📺 Target Video: ${ytUrl}`);

    const processorRes = await fetch(PROCESSOR_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        youtube_url: ytUrl,
        user_id: post_id,
        access_token: access_token // Provide token to bypass bot detection
      }),
      signal: AbortSignal.timeout(600000) // 10 min timeout for large downloads
    });

    if (!processorRes.ok) {
        const errorText = await processorRes.text();
        throw new Error(`Video processor failed: ${processorRes.status} - ${errorText}`);
    }

    const processorData = await processorRes.json();
    if (processorData.status !== "success") {
        throw new Error(`Normalization failed: ${processorData.detail || "Unknown error"}`);
    }

    // 3. Update Post Record in Supabase
    const secureUrl = processorData.cloudinary_url;
    const mediaMetadata = [{
      type: 'video',
      aspectRatio: 16/9, // Default for most YT videos; processor could return this if needed
      thumbnailUrl: secureUrl.replace('.mp4', '.jpg').replace('.mov', '.jpg'),
      publicId: processorData.public_id,
      version: new Date().getTime().toString()
    }];

    console.log(`✅ Normalization complete: ${secureUrl}`);

    const { error: updateError } = await supabase
      .from('posts')
      .update({
        media_urls: [secureUrl],
        media_metadata: mediaMetadata,
        status: 'published'
      })
      .eq('id', post_id);

    if (updateError) throw updateError;

    return new Response(JSON.stringify({ 
      success: true, 
      url: secureUrl,
      metadata: mediaMetadata[0]
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    });

  } catch (error) {
    console.error('Final normalization error:', error);
    return new Response(JSON.stringify({ 
      success: false, 
      error: error.message 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }
})
