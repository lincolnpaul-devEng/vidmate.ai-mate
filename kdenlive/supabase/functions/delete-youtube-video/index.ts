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
    const { video_id, access_token, post_id } = await req.json()
    console.log(`[BACKEND] Deletion request received for video: ${video_id}, post: ${post_id}`);
    
    if (!video_id || !access_token || !post_id) {
      throw new Error('video_id, access_token, and post_id are required')
    }

    // Step 3: API Execution (YouTube DELETE)
    console.log(`[BACKEND] Step 3: Sending DELETE request to YouTube for ${video_id}...`);
    const ytRes = await fetch(`https://www.googleapis.com/youtube/v3/videos?id=${video_id}`, {
      method: 'DELETE',
      headers: { Authorization: `Bearer ${access_token}` }
    })

    console.log(`[BACKEND] YouTube responded with status: ${ytRes.status}`);

    // YouTube responds with 204 No Content on success.
    // If it's 404, the video is already gone, so we should proceed to Step 4.
    if (!ytRes.ok && ytRes.status !== 204 && ytRes.status !== 404) {
      const errText = await ytRes.text()
      console.error(`[BACKEND] YouTube Deletion Failed: ${errText}`);
      throw new Error(`YouTube API Error: ${ytRes.status} - ${errText}`)
    }

    if (ytRes.status === 404) {
      console.log(`[BACKEND] Video ${video_id} was already deleted from YouTube. Proceeding to cleanup.`);
    }

    // Step 4: Database Cleanup (Only if YouTube succeeded)
    console.log(`[BACKEND] Step 4: Removing record ${post_id} from Supabase...`);
    const supabase = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    const { error: dbError } = await supabase
      .from('posts')
      .delete()
      .eq('id', post_id)

    if (dbError) {
      console.error(`[BACKEND] Supabase Delete Failed: ${dbError.message}`);
      throw dbError
    }

    console.log(`[BACKEND] Deletion complete for post ${post_id}`);
    return new Response(JSON.stringify({ success: true }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })

  } catch (error) {
    console.error('[BACKEND] Error in delete-youtube-video:', error.message);
    return new Response(JSON.stringify({ success: false, error: error.message }), {
      status: 200, // Return 200 so Supabase SDK doesn't swallow the error message
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }
})
