import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.38.4";
const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type'
};
serve(async (req)=>{
  if (req.method === 'OPTIONS') {
    return new Response('ok', {
      headers: corsHeaders
    });
  }
  try {
    const body = await req.json();
    const { media_url, video_url, content, title, privacy_status, publish_at } = body || {};
    console.log('[YOUTUBE_PUBLISH] Request body:', {
      media_url: media_url ? 'present' : 'missing',
      video_url: video_url ? 'present' : 'missing',
      content,
      title
    });
    if (!media_url && !video_url) {
      throw new Error('media_url or video_url is required');
    }
    const authHeader = req.headers.get('Authorization');
    if (!authHeader) {
      throw new Error('Authorization required');
    }
    const supabaseClient = createClient(Deno.env.get('SUPABASE_URL') ?? '', Deno.env.get('SUPABASE_ANON_KEY') ?? '', {
      global: {
        headers: {
          Authorization: authHeader
        }
      }
    });
    const { data: userData, error: userError } = await supabaseClient.auth.getUser();
    console.log('[YOUTUBE_PUBLISH] Auth result:', {
      userId: userData?.user?.id,
      error: userError?.message
    });
    if (userError || !userData?.user) {
      throw new Error(`Unable to authenticate user: ${userError?.message || 'no user'}`);
    }
    const { data: account, error: accountError } = await supabaseClient.from('linked_accounts').select('access_token').eq('platform', 'youtube').eq('user_id', userData.user.id).single();
    console.log('[YOUTUBE_PUBLISH] Account lookup:', {
      found: !!account,
      error: accountError?.message
    });
    if (accountError || !account?.access_token) {
      throw new Error(`Connected YouTube account not found: ${accountError?.message || 'no account'}`);
    }
    const accessToken = account.access_token;
    const sourceUrl = video_url || media_url;
    console.log('[YOUTUBE_PUBLISH] Fetching media from:', sourceUrl);
    const mediaResponse = await fetch(sourceUrl);
    console.log('[YOUTUBE_PUBLISH] Media fetch status:', mediaResponse.status);
    if (!mediaResponse.ok) {
      throw new Error(`Failed to fetch media for upload: ${mediaResponse.status} ${mediaResponse.statusText}`);
    }
    const mediaBuffer = new Uint8Array(await mediaResponse.arrayBuffer());
    console.log('[YOUTUBE_PUBLISH] Media buffer size:', mediaBuffer.length);
    const initResponse = await fetch('https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status', {
      method: 'POST',
      headers: {
        Authorization: `Bearer ${accessToken}`,
        'Content-Type': 'application/json; charset=UTF-8',
        'X-Upload-Content-Type': 'video/*'
      },
      body: JSON.stringify({
        snippet: {
          title: title || content || 'Posted via SocialPulse',
          description: content || '',
          categoryId: '22'
        },
        status: {
          privacyStatus: privacy_status || 'public',
          publishAt: publish_at || null,
          selfDeclaredMadeForKids: false
        }
      })
    });
    console.log('[YOUTUBE_PUBLISH] YouTube init response status:', initResponse.status);
    if (!initResponse.ok) {
      const errorBody = await initResponse.text();
      console.log('[YOUTUBE_PUBLISH] YouTube init error:', errorBody);
      throw new Error(`YouTube init failed: ${initResponse.status} - ${errorBody}`);
    }
    const uploadUrl = initResponse.headers.get('Location');
    console.log('[YOUTUBE_PUBLISH] Upload URL obtained:', !!uploadUrl);
    if (!uploadUrl) {
      throw new Error('Missing YouTube upload location');
    }
    const uploadResponse = await fetch(uploadUrl, {
      method: 'PUT',
      headers: {
        Authorization: `Bearer ${accessToken}`,
        'Content-Type': 'video/*',
        'Content-Length': String(mediaBuffer.length)
      },
      body: mediaBuffer
    });
    console.log('[YOUTUBE_PUBLISH] YouTube upload response status:', uploadResponse.status);
    if (!uploadResponse.ok) {
      const uploadError = await uploadResponse.text();
      console.log('[YOUTUBE_PUBLISH] YouTube upload error:', uploadError);
      throw new Error(`YouTube upload failed: ${uploadResponse.status} - ${uploadError}`);
    }
    const result = await uploadResponse.json();
    console.log('[YOUTUBE_PUBLISH] YouTube response video_id:', result?.id);
    if (!result?.id) {
      throw new Error('YouTube upload did not return a video id');
    }
    return new Response(JSON.stringify({
      success: true,
      video_id: result.id,
      url: `https://www.youtube.com/watch?v=${result.id}`
    }), {
      headers: {
        ...corsHeaders,
        'Content-Type': 'application/json'
      }
    });
  } catch (error) {
    console.error('[YOUTUBE_PUBLISH] Error:', error);
    // Determine appropriate status code based on error type
    let statusCode = 500;
    const errorMsg = error.message || 'YouTube publish failed';
    if (errorMsg.includes('required') || errorMsg.includes('not found') || errorMsg.includes('Unable to authenticate') || errorMsg.includes('authorization')) {
      statusCode = 400 // Client error for missing credentials/auth
      ;
    }
    return new Response(JSON.stringify({
      success: false,
      error: errorMsg
    }), {
      status: statusCode,
      headers: {
        ...corsHeaders,
        'Content-Type': 'application/json'
      }
    });
  }
});
