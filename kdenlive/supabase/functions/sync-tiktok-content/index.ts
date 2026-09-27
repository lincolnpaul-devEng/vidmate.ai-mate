import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

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
    if (!user_id) throw new Error('user_id is required')

    const supabase = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // 1. Get TikTok credentials
    const { data: account, error: accountError } = await supabase
      .from('linked_accounts')
      .select('*')
      .eq('user_id', user_id)
      .eq('platform', 'tiktok')
      .single()

    if (accountError || !account) {
      return new Response(JSON.stringify({ success: false, message: 'TikTok account not connected' }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      })
    }

    console.log(`[SYNC_TIKTOK] Fetching videos for user: ${user_id}`);

    // Step 0: ACTIVE RECONCILIATION: Check for 'processing' posts in our DB
    console.log(`[SYNC_TIKTOK] Checking for posts in 'processing' status...`)
    const { data: processingPosts } = await supabase
      .from('posts')
      .select('id, external_id, caption, created_at')
      .eq('user_id', user_id)
      .eq('platform', 'tiktok')
      .eq('status', 'processing')

    if (processingPosts && processingPosts.length > 0) {
      console.log(`[SYNC_TIKTOK] Found ${processingPosts.length} processing posts. Checking TikTok status...`)
      for (const post of processingPosts) {
        try {
          const statusRes = await fetch('https://open.tiktokapis.com/v2/post/publish/status/fetch/', {
            method: 'POST',
            headers: {
              'Authorization': `Bearer ${account.access_token}`,
              'Content-Type': 'application/json',
            },
            body: JSON.stringify({ publish_id: post.external_id })
          })
          const statusData = await statusRes.json()
          
          if (statusData.data?.status === 'PUBLISH_COMPLETE') {
             console.log(`[SYNC_TIKTOK] Post ${post.external_id} completed. Updating...`)
             await supabase
              .from('posts')
              .update({ 
                status: 'published',
                last_synced_at: new Date().toISOString()
              })
              .eq('id', post.id)
          } else if (statusData.data?.status === 'FAILED') {
             await supabase
              .from('posts')
              .update({ status: 'failed' })
              .eq('id', post.id)
          }
        } catch (e) {
          console.error(`[SYNC_TIKTOK] Status fetch failed for ${post.id}:`, e.message)
        }
      }
    }

    // 2. Fetch videos from TikTok API v2
    // TikTok v2 video/list requires 'fields' as a query parameter even for POST
    const fields = "id,video_description,create_time,cover_image_url,share_url,embed_html,view_count,like_count,comment_count,share_count";
    const tiktokRes = await fetch(`https://open.tiktokapis.com/v2/video/list/?fields=${fields}`, {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${account.access_token}`,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        max_count: 10
      })
    })

    const tiktokData = await tiktokRes.json()

    if (tiktokData.error && tiktokData.error.code !== 'ok') {
      const err = tiktokData.error;
      console.error(`[SYNC_TIKTOK] API Error:`, JSON.stringify(err));
      // Capture detailed error info
      throw new Error(`TikTok API Error (${err.code || 'unknown'}): ${err.message || JSON.stringify(err)}`);
    }

    const videos = tiktokData.data?.videos || []
    console.log(`[SYNC_TIKTOK] Found ${videos.length} videos`);

    // 3. Map and Insert into posts table
    const postsToUpsert: any[] = [];
    
    for (const video of videos) {
      // RECONCILIATION LOGIC: TikTok publish_id != video_id
      // Try soft-matching by caption and timestamp
      if (processingPosts && processingPosts.length > 0) {
        const videoTime = video.create_time * 1000;
        const match = processingPosts.find(p => {
          const pTime = new Date(p.created_at).getTime();
          // Match if captions match and time is within 10 minutes
          const timeMatch = Math.abs(videoTime - pTime) < 10 * 60 * 1000;
          const captionMatch = p.caption === (video.video_description || '');
          return timeMatch && captionMatch;
        });

        if (match) {
          console.log(`[SYNC_TIKTOK] Soft-matched video ${video.id} to local post ${match.id}. Promoting...`);
          await supabase
            .from('posts')
            .update({
              external_id: video.id, // Migrate from publish_id to video_id
              status: 'published',
              platform_links: { tiktok: video.share_url },
              last_synced_at: new Date().toISOString()
            })
            .eq('id', match.id);
          
          continue; // Skip upsert as we promoted the existing record
        }
      }

      postsToUpsert.push({
        user_id: user_id,
        caption: video.video_description || '',
        platforms: ['tiktok'],
        platform: 'tiktok',
        status: 'published',
        media_urls: [video.cover_image_url],
        created_at: new Date(video.create_time * 1000).toISOString(),
        external_id: video.id,
        platform_links: { tiktok: video.share_url },
        last_synced_at: new Date().toISOString(),
        metrics: {
          views: video.view_count || 0,
          likes: video.like_count || 0,
          comments: video.comment_count || 0
        },
        analytics: {
          impressions: video.view_count || 0,
          engagements: (video.like_count || 0) + (video.comment_count || 0),
          clicks: 0,
          shares: video.share_count || 0
        }
      });
    }

    if (postsToUpsert.length > 0) {
      console.log(`[SYNC_TIKTOK] Upserting ${postsToUpsert.length} items...`);
      const { error: upsertError } = await supabase
        .from('posts')
        .upsert(postsToUpsert, { onConflict: 'user_id,external_id,platform' })

      if (upsertError) {
        console.error(`[SYNC_TIKTOK] Upsert Error:`, upsertError);
        throw new Error(`Database Error during sync: ${upsertError.message}`);
      }
    }

    return new Response(JSON.stringify({ 
      success: true, 
      message: `Successfully synced ${postsToUpsert.length} TikTok videos`,
      count: postsToUpsert.length
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })

  } catch (error: any) {
    console.error('[SYNC_TIKTOK] Exception:', error)
    return new Response(JSON.stringify({ 
      success: false, 
      error: error.message || 'Internal Server Error' 
    }), {
      status: 200, 
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }
})
