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

    // 1. Get the Facebook linked account for this user
    const { data: account, error: accountError } = await supabase
      .from('linked_accounts')
      .select('access_token, external_id, username')
      .eq('user_id', user_id)
      .eq('platform', 'facebook')
      .single()

    if (accountError || !account) {
      throw new Error('Facebook account not linked or not found')
    }

    const { access_token: pageToken, external_id: pageId } = account

    if (!pageToken || !pageId) {
      throw new Error('Missing Page Token or Page ID for Facebook sync')
    }

    // 2. ACTIVE RECONCILIATION: Check for 'processing' posts in our DB
    console.log(`[SYNC_FB] Checking for posts in 'processing' status...`)
    const { data: processingPosts } = await supabase
      .from('posts')
      .select('id, external_id, caption')
      .eq('user_id', user_id)
      .eq('platform', 'facebook')
      .eq('status', 'processing')

    if (processingPosts && processingPosts.length > 0) {
      console.log(`[SYNC_FB] Found ${processingPosts.length} processing posts. Checking Meta status...`)
      for (const post of processingPosts) {
        try {
          // Check video status on Facebook
          const statusUrl = `https://graph.facebook.com/v21.0/${post.external_id}?fields=status,permalink_url&access_token=${pageToken}`
          const statusRes = await fetch(statusUrl)
          const statusData = await statusRes.json()

          if (statusData.status) {
            const videoStatus = statusData.status.video_status
            console.log(`[SYNC_FB] Video ${post.external_id} status: ${videoStatus}`)

            if (videoStatus === 'ready') {
              await supabase
                .from('posts')
                .update({ 
                  status: 'published', 
                  platform_links: { facebook: statusData.permalink_url },
                  last_synced_at: new Date().toISOString()
                })
                .eq('id', post.id)
            } else if (videoStatus === 'error') {
              await supabase
                .from('posts')
                .update({ status: 'failed' })
                .eq('id', post.id)
            }
          }
        } catch (e) {
          console.error(`[SYNC_FB] Failed to check status for ${post.external_id}:`, e.message)
        }
      }
    }

    // 3. Fetch Published Posts from the Page
    const fbUrl = new URL(`https://graph.facebook.com/v21.0/${pageId}/published_posts`)
    fbUrl.searchParams.append('fields', 'id,message,created_time,full_picture,permalink_url,attachments{target}')
    fbUrl.searchParams.append('access_token', pageToken)
    fbUrl.searchParams.append('limit', '15') // Increased limit for better reconciliation coverage

    console.log(`[SYNC_FB] Fetching posts for page ${pageId}...`)
    const fbRes = await fetch(fbUrl.toString())
    const fbData = await fbRes.json()

    if (fbData.error) {
      throw new Error(`Facebook API Error: ${fbData.error.message}`)
    }

    const fbPosts = fbData.data || []
    console.log(`[SYNC_FB] Found ${fbPosts.length} posts. Mapping to SocialPulse schema...`)

    if (fbPosts.length === 0) {
      return new Response(JSON.stringify({ success: true, message: 'No posts found to sync' }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      })
    }

    // 3. Map to SocialPulse Post Schema
    const postsToUpsert: any[] = [];
    
    for (const fp of fbPosts) {
      let reach = 0;
      let impressions = 0;
      let likes = 0;
      let comments = 0;

      // RECONCILIATION LOGIC: Check if this live post matches a 'processing' record via its Video ID
      const videoId = fp.attachments?.data?.[0]?.target?.id;
      if (videoId && processingPosts) {
        const match = processingPosts.find(p => p.external_id === videoId);
        if (match) {
          console.log(`[SYNC_FB] Reconciled Video ID ${videoId} -> Post ID ${fp.id}. Promoting local record ${match.id}`);
          // We will update the existing record immediately to avoid conflicts/duplicates
          await supabase
            .from('posts')
            .update({ 
               external_id: fp.id, // Migrate to Post ID for future metrics sync
               status: 'published',
               platform_links: { facebook: fp.permalink_url },
               last_synced_at: new Date().toISOString()
            })
            .eq('id', match.id);
          
          // Remove from processingPosts to prevent redundant Step 2 updates if loop continues
          // and skip adding to postsToUpsert as we already updated it
          continue; 
        }
      }

      // Localized fetch for metrics to isolate failures
      try {
        const detailUrl = new URL(`https://graph.facebook.com/v21.0/${fp.id}`)
        detailUrl.searchParams.append('fields', 'insights.metric(post_impressions),likes.summary(true),comments.summary(true)')
        detailUrl.searchParams.append('access_token', pageToken)
        
        const detailRes = await fetch(detailUrl.toString());
        const detailData = await detailRes.json();
        
        if (!detailData.error) {
          impressions = detailData.insights?.data?.find((i: any) => i.name === 'post_impressions')?.values?.[0]?.value || 0;
          likes = detailData.likes?.summary?.total_count || 0;
          comments = detailData.comments?.summary?.total_count || 0;
        }
      } catch (e) {
        console.warn(`[SYNC_FB] Exception during post detail fetch for ${fp.id}:`, e);
      }

      postsToUpsert.push({
        user_id: user_id,
        external_id: fp.id,
        platform: 'facebook',
        platforms: ['facebook'],
        caption: fp.message || '',
        status: 'published',
        media_urls: fp.full_picture ? [fp.full_picture] : [],
        created_at: fp.created_time,
        last_synced_at: new Date().toISOString(),
        platform_links: { facebook: fp.permalink_url },
        metrics: {
          views: impressions,
          likes: likes,
          comments: comments
        },
        analytics: {
          impressions: impressions,
          engagements: likes + comments,
          clicks: 0,
          shares: 0 
        }
      });
    }

    // 4. Upsert into database
    const { error: upsertError } = await supabase
      .from('posts')
      .upsert(postsToUpsert, {
        onConflict: 'user_id,external_id,platform',
        ignoreDuplicates: false
      })

    if (upsertError) throw upsertError

    return new Response(JSON.stringify({ 
      success: true, 
      count: postsToUpsert.length,
      message: `Successfully synced ${postsToUpsert.length} Facebook posts`
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })

  } catch (error) {
    console.error('[SYNC_FB] Error:', error.message)
    return new Response(JSON.stringify({ 
      success: false, 
      error: error.message 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })
  }
})
