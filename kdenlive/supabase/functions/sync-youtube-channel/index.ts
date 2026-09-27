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
    const { access_token, user_id } = await req.json()
    
    if (!access_token) throw new Error('Google Access Token is required')
    if (!user_id) throw new Error('User ID is required')

    const supabase = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // Step 0: ACTIVE RECONCILIATION: Check for 'processing' posts in our DB
    console.log(`[SYNC_YT] Checking for posts in 'processing' status...`)
    const { data: processingPosts } = await supabase
      .from('posts')
      .select('id, external_id')
      .eq('user_id', user_id)
      .eq('platform', 'youtube')
      .eq('status', 'processing')

    if (processingPosts && processingPosts.length > 0) {
      console.log(`[SYNC_YT] Found ${processingPosts.length} processing posts. Checking YouTube status...`)
      const procIds = processingPosts.map(p => p.external_id).join(',')
      try {
        const procRes = await fetch(
          `https://www.googleapis.com/youtube/v3/videos?part=status,snippet&id=${procIds}`,
          { headers: { Authorization: `Bearer ${access_token}` } }
        )
        const procData = await procRes.json()
        
        if (procData.items) {
          for (const item of procData.items) {
            if (item.status?.uploadStatus === 'processed') {
              console.log(`[SYNC_YT] Video ${item.id} is now processed. Updating DB...`)
              await supabase
                .from('posts')
                .update({ 
                  status: 'published',
                  last_synced_at: new Date().toISOString(),
                  platform_links: { youtube: `https://www.youtube.com/watch?v=${item.id}` }
                })
                .eq('external_id', item.id)
                .eq('platform', 'youtube')
            } else if (item.status?.uploadStatus === 'failed') {
               await supabase
                .from('posts')
                .update({ status: 'failed' })
                .eq('external_id', item.id)
                .eq('platform', 'youtube')
            }
          }
        }
      } catch (e) {
        console.error(`[SYNC_YT] Reconciliation fetch failed:`, e.message)
      }
    }

    // Step A: Get Channel's Uploads Playlist ID
    const channelRes = await fetch(
      `https://www.googleapis.com/youtube/v3/channels?part=contentDetails&mine=true`,
      { headers: { Authorization: `Bearer ${access_token}` } }
    )
    const channelData = await channelRes.json()
    if (channelData.error) throw new Error(`YouTube API Error (Channel): ${channelData.error.message}`)
    
    const uploadsPlaylistId = channelData.items?.[0]?.contentDetails?.relatedPlaylists?.uploads
    if (!uploadsPlaylistId) throw new Error('Could not find uploads playlist for this channel')

    // Step B: Get Recent Videos from the Uploads Playlist
    const playlistRes = await fetch(
      `https://www.googleapis.com/youtube/v3/playlistItems?part=snippet&playlistId=${uploadsPlaylistId}&maxResults=50`,
      { headers: { Authorization: `Bearer ${access_token}` } }
    )
    const playlistData = await playlistRes.json()
    if (playlistData.error) throw new Error(`YouTube API Error (Playlist): ${playlistData.error.message}`)

    const videos = playlistData.items || []
    if (videos.length === 0) {
      return new Response(JSON.stringify({ message: 'No videos found in this channel' }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      })
    }

    // Step C: Get Detailed Analytics (Statistics) for all Videos
    const videoIds = videos.map((v: any) => v.snippet.resourceId.videoId).join(',')
    const statsRes = await fetch(
      `https://www.googleapis.com/youtube/v3/videos?part=statistics,snippet,status&id=${videoIds}`,
      { headers: { Authorization: `Bearer ${access_token}` } }
    )
    const statsData = await statsRes.json()
    if (statsData.error) throw new Error(`YouTube API Error (Stats): ${statsData.error.message}`)

    // Step D: Map and Upsert into Supabase
    const postsToUpsert = statsData.items.map((v: any) => ({
      user_id: user_id,
      external_id: v.id,
      platform: 'youtube',
      platforms: ['youtube'],
      caption: v.snippet.title,
      content: v.snippet.description,
      status: 'published', // These are all live uploads
      media_urls: [v.snippet.thumbnails?.maxres?.url || v.snippet.thumbnails?.high?.url || v.snippet.thumbnails?.default?.url].filter(Boolean),
      platform_links: { youtube: `https://www.youtube.com/watch?v=${v.id}` },
      metrics: {
        views: parseInt(v.statistics.viewCount || '0'),
        likes: parseInt(v.statistics.likeCount || '0'),
        comments: parseInt(v.statistics.commentCount || '0'),
      },
      last_synced_at: new Date().toISOString(),
      created_at: v.snippet.publishedAt,
    }))

    // Use upsert with the unique constraint on (user_id, external_id, platform)
    const { error: upsertError } = await supabase
      .from('posts')
      .upsert(postsToUpsert, { 
        onConflict: 'user_id,external_id,platform',
        ignoreDuplicates: false // We want to update metrics
      })

    if (upsertError) throw upsertError

    // Step E: Sync Comments for ALL Recent Videos
    console.log(`Synchronizing comments for ${postsToUpsert.length} videos...`);
    const inboxItems: any[] = [];

    // Use Promise.all to fetch comments for all videos in parallel (up to 50)
    const commentPromises = postsToUpsert.map(async (video: any) => {
      try {
        const commentRes = await fetch(
          `https://www.googleapis.com/youtube/v3/commentThreads?part=snippet&videoId=${video.external_id}&maxResults=20`,
          { headers: { Authorization: `Bearer ${access_token}` } }
        );
        const commentData = await commentRes.json();
        
        if (commentData.items) {
          commentData.items.forEach((item: any) => {
            const commentToken = item.snippet.topLevelComment.snippet;
            inboxItems.push({
              user_id: user_id,
              external_id: item.id,
              source_platform: 'yt',
              content: commentToken.textDisplay,
              author_meta: {
                authorDisplayName: commentToken.authorDisplayName,
                authorProfileImageUrl: commentToken.authorProfileImageUrl,
                authorChannelUrl: commentToken.authorChannelUrl,
              },
              created_at: commentToken.publishedAt,
              is_replied: false,
            });
          });
        }
      } catch (err) {
        console.error(`Error fetching comments for ${video.external_id}:`, err);
      }
    });

    await Promise.all(commentPromises);

    if (inboxItems.length > 0) {
      const { error: inboxError } = await supabase
        .from('inbox_items')
        .upsert(inboxItems, { 
          onConflict: 'user_id,external_id,source_platform'
        });
      
      if (inboxError) {
         console.warn('Inbox sync warning (some comments might have failed):', inboxError.message);
      }
    }

    return new Response(JSON.stringify({ 
      success: true, 
      video_count: postsToUpsert.length,
      comment_count: inboxItems.length,
      message: `Synced ${postsToUpsert.length} videos and collected ${inboxItems.length} comments`
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })

  } catch (error) {
    console.error('Sync error:', error)
    return new Response(JSON.stringify({ 
      success: false, 
      error: error.message 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })
  }
})
