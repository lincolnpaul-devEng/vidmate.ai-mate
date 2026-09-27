import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.38.4"

console.log("Scheduler Edge Function initialized")

// YouTube API: Update video privacy status to PUBLIC
const dispatchYouTube = async (post: any, accessToken: string) => {
  try {
    console.log(`[${post.id}] Dispatching to YouTube...`)
    
    // If the video was uploaded with a scheduled time, YouTube might have already published it
    // But we need to ensure privacy status is set correctly
    if (post.youtube_video_id) {
      const response = await fetch(
        `https://www.googleapis.com/youtube/v3/videos?part=status&id=${post.youtube_video_id}`,
        {
          method: 'PUT',
          headers: {
            'Authorization': `Bearer ${accessToken}`,
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({
            kind: 'youtube#video',
            etag: 'placeholder',
            id: post.youtube_video_id,
            status: {
              uploadStatus: 'processed',
              privacyStatus: 'public',
              publishAt: null, // Clear any scheduled time
            },
          }),
        }
      )

      if (!response.ok) {
        const error = await response.json()
        console.error(`[${post.id}] YouTube API error:`, error)
        throw new Error(`YouTube API error: ${error.error?.message || 'Unknown error'}`)
      }

      console.log(`[${post.id}] YouTube video ${post.youtube_video_id} published successfully`)
      return true
    }

    // If no YouTube video ID, this might not be a YouTube post
    return false
  } catch (err: any) {
    console.error(`[${post.id}] YouTube dispatch error:`, err.message)
    throw err
  }
}

serve(async (req) => {
  try {
    // 1. Authorization (Only accept requests containing the CRON_SECRET)
    const authHeader = req.headers.get('Authorization')
    const cronSecret = Deno.env.get('CRON_SECRET')
    const allowPlaceholder = Deno.env.get('ALLOW_CRON_PLACEHOLDER') === 'true'
    const isPlaceholderHeader = authHeader === 'Bearer ${SECRET}'

    if (cronSecret && authHeader !== `Bearer ${cronSecret}`) {
      if (isPlaceholderHeader && allowPlaceholder) {
        console.warn('Allowing Cron placeholder header because ALLOW_CRON_PLACEHOLDER=true. Update the cron job to send the real secret instead.')
      } else {
        if (isPlaceholderHeader) {
          console.warn('Unauthorized invocation attempt with literal ${SECRET} placeholder header. Replace the cron header with the real CRON_SECRET.')
        } else {
          console.warn('Unauthorized invocation attempt.')
        }
        return new Response(JSON.stringify({ error: 'Unauthorized' }), { status: 401, headers: { "Content-Type": "application/json" } })
      }
    }

    // 2. Mount Supabase DB with Service Role
    const supabaseUrl = Deno.env.get('SUPABASE_URL') ?? ''
    const supabaseKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    
    if (!supabaseUrl || !supabaseKey) { return new Response(JSON.stringify({ error: "Missing DB env vars" }), { status: 500 }); }
    
    const supabase = createClient(supabaseUrl, supabaseKey)

    // 3. Sweep the database for posts due right now
    const now = new Date().toISOString()
    console.log(`[SCHEDULER] Running scheduled post check at ${now}`)
    
    const { data: posts, error } = await supabase
      .from('posts')
      .select('*, profiles(*)')
      .eq('status', 'scheduled')
      .lte('scheduled_at', now)

    if (error) throw error

    if (!posts || posts.length === 0) {
      console.log('[SCHEDULER] No scheduled posts due for processing.')
      return new Response(JSON.stringify({ message: "No scheduled posts due for processing." }), { 
        headers: { "Content-Type": "application/json" } 
      })
    }

    console.log(`[SCHEDULER] Discovered ${posts.length} posts due for dispatch. Processing...`)

    // 4. Dispatch Phase
    const results = []
    const failed = []
    
    for (const post of posts) {
      console.log(`[${post.id}] Processing scheduled post...`)
      
      // Step A: Load the user's Keyring to extract their OAuth Vault tokens
      const { data: accounts } = await supabase
        .from('linked_accounts')
        .select('*')
        .eq('user_id', post.user_id)
        .eq('connected', true)

      if (!accounts || accounts.length === 0) {
        console.log(`[${post.id}] No connected accounts found for user ${post.user_id}`)
        const { error: failUpdateErr } = await supabase
          .from('posts')
          .update({
            status: 'failed',
            last_synced_at: now,
            metadata: { ...(post.metadata || {}), scheduler_last_run_at: now, scheduler_last_status: 'failed' }
          })
          .eq('id', post.id)
        if (failUpdateErr) {
          console.warn(`[${post.id}] Failed to mark post as failed:`, failUpdateErr.message)
        }
        failed.push({ post_id: post.id, error: 'No connected accounts found' })
        continue
      }

      const { error: pendingUpdateErr } = await supabase
        .from('posts')
        .update({
          status: 'pending',
          last_synced_at: now,
          metadata: { ...(post.metadata || {}), scheduler_last_run_at: now, scheduler_last_status: 'pending' }
        })
        .eq('id', post.id)

      if (pendingUpdateErr) {
        console.warn(`[${post.id}] Failed to mark post as pending before dispatch:`, pendingUpdateErr.message)
      }

      // Step B: Dispatch loop per account
      let postSucceeded = false
      
      for (const account of accounts) {
        if (!account.access_token) {
          console.log(`[${post.id}] Skipping ${account.platform}: Missing OAuth access token`)
          continue
        }

        try {
          // --- PLATFORM API DISPATCH ARCHITECTURE ---
          if (account.platform === 'youtube') {
            if (post.youtube_video_id) {
              await dispatchYouTube(post, account.access_token)
              console.log(`[${post.id}] YouTube privacy status updated to public`)
            } else {
              const mediaUrl = post.media_urls?.[0] || post.media_url
              if (!mediaUrl) {
                throw new Error('No media URL found on scheduled post')
              }
              console.log(`[${post.id}] Triggering youtube-publish for scheduled video upload...`)
              const { data: publishRes, error: publishErr } = await supabase.functions.invoke('youtube-publish', {
                body: { 
                  media_url: mediaUrl, 
                  title: post.title || post.caption || 'Scheduled Video', 
                  content: post.caption || post.content || '',
                  user_id: post.user_id 
                }
              })

              if (publishErr) throw publishErr
              if (publishRes?.error) throw new Error(publishRes.error)

              const ytVideoId = publishRes?.video_id || null
              if (ytVideoId) {
                const { error: updateErr } = await supabase
                  .from('posts')
                  .update({ youtube_video_id: ytVideoId })
                  .eq('id', post.id)
                if (updateErr) {
                  console.warn(`[${post.id}] Failed to save youtube_video_id:`, updateErr.message)
                }
              }
              console.log(`[${post.id}] YouTube scheduled upload successful: ${publishRes?.url}`)
            }

            console.log(`[${post.id}] Successfully transmitted payload to ${account.platform}`)
            results.push({ post_id: post.id, platform: account.platform, status: 'success' })
            postSucceeded = true
          } else {
            console.log(`[${post.id}] Platform ${account.platform} not yet implemented`)
          }
        } catch (err: any) {
          console.error(`[${post.id}] Dispatch error for ${account.platform}:`, err.message)
          failed.push({ post_id: post.id, platform: account.platform, error: err.message })
        }
      }

      // Step C: Atomically update post state to prevent double execution
      // Only mark the post published if this post's dispatch loop succeeded for at least one platform
      if (postSucceeded) {
        const updateResult = await supabase
          .from('posts')
          .update({
            status: 'published',
            last_synced_at: now,
            metadata: { ...(post.metadata || {}), scheduler_last_run_at: now, scheduler_last_status: 'published' }
          })
          .eq('id', post.id)
        
        if (updateResult.error) {
          console.error(`[${post.id}] Failed to update post status:`, updateResult.error)
        } else {
          console.log(`[${post.id}] Post status updated to published`)
        }
      } else {
        const failedUpdateResult = await supabase
          .from('posts')
          .update({
            status: 'failed',
            last_synced_at: now,
            metadata: { ...(post.metadata || {}), scheduler_last_run_at: now, scheduler_last_status: 'failed' }
          })
          .eq('id', post.id)

        if (failedUpdateResult.error) {
          console.error(`[${post.id}] Failed to mark post as failed:`, failedUpdateResult.error)
        } else {
          console.log(`[${post.id}] Post status updated to failed`)
        }
      }
    }

    // 5. Metrics Synchronization Phase (New)
    console.log('[SCHEDULER] Starting automated metrics sync for all connected accounts...')
    
    const { data: allAccounts } = await supabase
      .from('linked_accounts')
      .select('user_id, platform')
      .eq('connected', true)
      .in('platform', ['facebook', 'tiktok'])

    if (allAccounts && allAccounts.length > 0) {
      console.log(`[SCHEDULER] Found ${allAccounts.length} accounts to sync.`)
      for (const account of allAccounts) {
        try {
          const syncFunction = account.platform === 'facebook' ? 'sync-facebook-content' : 'sync-tiktok-content'
          console.log(`[SCHEDULER] Triggering ${syncFunction} for user ${account.user_id}...`)
          
          const { data: syncData, error: syncError } = await supabase.functions.invoke(syncFunction, {
            body: { user_id: account.user_id }
          })

          if (syncError) throw syncError
          results.push({ user_id: account.user_id, platform: account.platform, type: 'sync', status: 'success' })
        } catch (syncErr: any) {
          console.error(`[SCHEDULER] Sync failed for ${account.platform} (user: ${account.user_id}):`, syncErr.message)
          failed.push({ user_id: account.user_id, platform: account.platform, type: 'sync', error: syncErr.message })
        }
      }
    }

    const summary = {
      success: true,
      scheduled_posts_processed: posts.length,
      successful_operations: results.length,
      failed_operations: failed.length,
      results,
      ...(failed.length > 0 && { failures: failed })
    }

    console.log('[SCHEDULER] Run complete:', summary)
    return new Response(JSON.stringify(summary), { 
      headers: { "Content-Type": "application/json" } 
    })

  } catch (err: any) {
    console.error("[SCHEDULER] Error:", err.message)
    return new Response(JSON.stringify({ error: err.message }), { 
      status: 500, headers: { "Content-Type": "application/json" } 
    })
  }
})
