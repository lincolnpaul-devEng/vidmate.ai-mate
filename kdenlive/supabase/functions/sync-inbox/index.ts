import { serve } from 'https://deno.land/std@0.168.0/http/server.ts'
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.38.4'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Max-Age': '86400',
}

type InboxRow = {
  user_id: string
  external_id: string
  source_platform: 'yt' | 'fb' | 'ig'
  content: string
  author_meta: Record<string, unknown>
  created_at: string
  is_replied: boolean
}

async function syncYouTubeComments(
  userId: string,
  accessToken: string,
): Promise<InboxRow[]> {
  const items: InboxRow[] = []

  const channelRes = await fetch(
    'https://www.googleapis.com/youtube/v3/channels?part=contentDetails&mine=true',
    { headers: { Authorization: `Bearer ${accessToken}` } },
  )
  const channelData = await channelRes.json()
  if (channelData.error) throw new Error(`YouTube: ${channelData.error.message}`)

  const uploadsPlaylistId = channelData.items?.[0]?.contentDetails?.relatedPlaylists?.uploads
  if (!uploadsPlaylistId) return items

  const playlistRes = await fetch(
    `https://www.googleapis.com/youtube/v3/playlistItems?part=snippet&playlistId=${uploadsPlaylistId}&maxResults=15`,
    { headers: { Authorization: `Bearer ${accessToken}` } },
  )
  const playlistData = await playlistRes.json()
  const videos = playlistData.items || []

  await Promise.all(
    videos.map(async (entry: { snippet: { resourceId: { videoId: string } } }) => {
      const videoId = entry.snippet.resourceId.videoId
      try {
        const commentRes = await fetch(
          `https://www.googleapis.com/youtube/v3/commentThreads?part=snippet&videoId=${videoId}&maxResults=25&order=time`,
          { headers: { Authorization: `Bearer ${accessToken}` } },
        )
        const commentData = await commentRes.json()
        for (const thread of commentData.items || []) {
          const snippet = thread.snippet.topLevelComment.snippet
          items.push({
            user_id: userId,
            external_id: thread.id,
            source_platform: 'yt',
            content: snippet.textDisplay || snippet.textOriginal || '',
            author_meta: {
              authorDisplayName: snippet.authorDisplayName,
              authorProfileImageUrl: snippet.authorProfileImageUrl,
              authorChannelUrl: snippet.authorChannelUrl,
              videoId,
            },
            created_at: snippet.publishedAt,
            is_replied: false,
          })
        }
      } catch (err) {
        console.warn(`[SYNC_INBOX] YouTube comments for ${videoId}:`, err)
      }
    }),
  )

  return items
}

async function syncFacebookComments(
  userId: string,
  pageToken: string,
  pageId: string,
): Promise<InboxRow[]> {
  const items: InboxRow[] = []

  const postsUrl = new URL(`https://graph.facebook.com/v21.0/${pageId}/published_posts`)
  postsUrl.searchParams.set('fields', 'id,message,created_time')
  postsUrl.searchParams.set('access_token', pageToken)
  postsUrl.searchParams.set('limit', '10')

  const postsRes = await fetch(postsUrl.toString())
  const postsData = await postsRes.json()
  if (postsData.error) throw new Error(`Facebook: ${postsData.error.message}`)

  for (const post of postsData.data || []) {
    try {
      const commentsUrl = new URL(`https://graph.facebook.com/v21.0/${post.id}/comments`)
      commentsUrl.searchParams.set('fields', 'id,message,from,created_time')
      commentsUrl.searchParams.set('access_token', pageToken)
      commentsUrl.searchParams.set('limit', '25')

      const commentsRes = await fetch(commentsUrl.toString())
      const commentsData = await commentsRes.json()

      for (const comment of commentsData.data || []) {
        items.push({
          user_id: userId,
          external_id: comment.id,
          source_platform: 'fb',
          content: comment.message || '',
          author_meta: {
            name: comment.from?.name,
            id: comment.from?.id,
            postId: post.id,
          },
          created_at: comment.created_time,
          is_replied: false,
        })
      }
    } catch (err) {
      console.warn(`[SYNC_INBOX] Facebook comments for ${post.id}:`, err)
    }
  }

  return items
}

async function syncInstagramComments(
  userId: string,
  accessToken: string,
  igUserId: string,
): Promise<InboxRow[]> {
  const items: InboxRow[] = []

  const mediaUrl = new URL(`https://graph.facebook.com/v21.0/${igUserId}/media`)
  mediaUrl.searchParams.set('fields', 'id,caption,timestamp')
  mediaUrl.searchParams.set('access_token', accessToken)
  mediaUrl.searchParams.set('limit', '10')

  const mediaRes = await fetch(mediaUrl.toString())
  const mediaData = await mediaRes.json()
  if (mediaData.error) throw new Error(`Instagram: ${mediaData.error.message}`)

  for (const media of mediaData.data || []) {
    try {
      const commentsUrl = new URL(`https://graph.facebook.com/v21.0/${media.id}/comments`)
      commentsUrl.searchParams.set('fields', 'id,text,username,timestamp,from')
      commentsUrl.searchParams.set('access_token', accessToken)
      commentsUrl.searchParams.set('limit', '25')

      const commentsRes = await fetch(commentsUrl.toString())
      const commentsData = await commentsRes.json()

      for (const comment of commentsData.data || []) {
        items.push({
          user_id: userId,
          external_id: comment.id,
          source_platform: 'ig',
          content: comment.text || '',
          author_meta: {
            username: comment.username || comment.from?.username,
            name: comment.from?.username,
            mediaId: media.id,
          },
          created_at: comment.timestamp,
          is_replied: false,
        })
      }
    } catch (err) {
      console.warn(`[SYNC_INBOX] Instagram comments for ${media.id}:`, err)
    }
  }

  return items
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
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? '',
    )

    const { data: accounts, error: accountsError } = await supabase
      .from('linked_accounts')
      .select('platform, access_token, external_id, connected')
      .eq('user_id', user_id)
      .eq('connected', true)

    if (accountsError) throw accountsError

    const allItems: InboxRow[] = []
    const platformResults: Record<string, { count: number; error?: string }> = {}

    for (const account of accounts || []) {
      try {
        if (account.platform === 'youtube' && account.access_token) {
          const ytItems = await syncYouTubeComments(user_id, account.access_token)
          allItems.push(...ytItems)
          platformResults.youtube = { count: ytItems.length }
        }

        if (account.platform === 'facebook' && account.access_token && account.external_id) {
          const fbItems = await syncFacebookComments(
            user_id,
            account.access_token,
            account.external_id,
          )
          allItems.push(...fbItems)
          platformResults.facebook = { count: fbItems.length }
        }

        if (account.platform === 'instagram' && account.access_token && account.external_id) {
          const igItems = await syncInstagramComments(
            user_id,
            account.access_token,
            account.external_id,
          )
          allItems.push(...igItems)
          platformResults.instagram = { count: igItems.length }
        }
      } catch (platformErr) {
        platformResults[account.platform] = {
          count: 0,
          error: platformErr instanceof Error ? platformErr.message : 'Sync failed',
        }
      }
    }

    if (allItems.length > 0) {
      const { error: upsertError } = await supabase.from('inbox_items').upsert(allItems, {
        onConflict: 'user_id,external_id,source_platform',
        ignoreDuplicates: false,
      })
      if (upsertError) throw upsertError
    }

    return new Response(
      JSON.stringify({
        success: true,
        total: allItems.length,
        platforms: platformResults,
        connected: (accounts || []).map((a) => a.platform),
      }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 200 },
    )
  } catch (error) {
    console.error('[SYNC_INBOX]', error)
    return new Response(
      JSON.stringify({
        success: false,
        error: error instanceof Error ? error.message : 'Sync failed',
      }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 500 },
    )
  }
})
