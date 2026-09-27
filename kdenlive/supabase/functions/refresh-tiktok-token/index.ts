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
    const { user_id } = await req.json().catch(() => ({}));
    const authHeader = req.headers.get('Authorization');
    
    let supabaseClient;
    let userId;

    let userFromToken = null;
    if (authHeader) {
      const tempClient = createClient(
        Deno.env.get('SUPABASE_URL') ?? '',
        Deno.env.get('SUPABASE_ANON_KEY') ?? '',
        { global: { headers: { Authorization: authHeader } } }
      );
      const { data: { user } } = await tempClient.auth.getUser();
      userFromToken = user;
    }

    if (userFromToken) {
      userId = userFromToken.id;
      supabaseClient = createClient(
        Deno.env.get('SUPABASE_URL') ?? '',
        Deno.env.get('SUPABASE_ANON_KEY') ?? '',
        { global: { headers: { Authorization: authHeader } } }
      );
    } else if (user_id) {
      console.log(`[TIKTOK_REFRESH] Using user_id fallback for: ${user_id}`);
      supabaseClient = createClient(
        Deno.env.get('SUPABASE_URL') ?? '',
        Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
      );
      userId = user_id;
    } else {
      throw new Error('Authorization required or valid user_id missing');
    }

    console.log(`[TIKTOK_REFRESH] Searching for linked account for user: ${userId}`);

    const { data: account, error: accountError } = await supabaseClient
      .from('linked_accounts')
      .select('refresh_token, external_id')
      .eq('platform', 'tiktok')
      .eq('user_id', userId)
      .single();

    if (accountError || !account?.refresh_token) {
      throw new Error('TikTok connection is localized. Please Re-link TikTok one last time to enable cloud persistence.')
    }

    // 2. Perform TikTok Refresh
    const clientKey = Deno.env.get('TIKTOK_CLIENT_KEY')
    const clientSecret = Deno.env.get('TIKTOK_CLIENT_SECRET')

    console.log(`[TIKTOK_REFRESH] Attempting refresh for user ${userId}...`)

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 8000); // 8 second internal timeout

    let refreshRes;
    try {
      refreshRes = await fetch('https://open.tiktokapis.com/v2/oauth/token/', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: new URLSearchParams({
          client_key: clientKey!,
          client_secret: clientSecret!,
          grant_type: 'refresh_token',
          refresh_token: account.refresh_token,
        }),
        signal: controller.signal
      })
    } catch (e: any) {
      if (e.name === 'AbortError') throw new Error('TikTok API Handshake Timeout (Slow Platform Response)')
      throw e;
    } finally {
      clearTimeout(timeoutId);
    }

    const refreshData = await refreshRes.json()

    if (refreshData.error) {
      console.error(`[TIKTOK_REFRESH] TikTok API Error:`, refreshData.error_description || refreshData.error)
      throw new Error(`TikTok Refresh Failed: ${refreshData.error_description || refreshData.error}`)
    }

    const newAccessToken = refreshData.access_token
    const newRefreshToken = refreshData.refresh_token // Refresh tokens can also be rolled

    // 3. Update the database with new tokens
    const { error: updateError } = await supabaseClient
      .from('linked_accounts')
      .update({
        access_token: newAccessToken,
        refresh_token: newRefreshToken,
        updated_at: new Date().toISOString()
      })
      .eq('platform', 'tiktok')
      .eq('user_id', userId)

    if (updateError) throw updateError

    console.log(`[TIKTOK_REFRESH] Successfully refreshed and updated tokens for ${userId}`)

    return new Response(JSON.stringify({ 
      success: true, 
      access_token: newAccessToken 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })

  } catch (error: any) {
    console.error('[TIKTOK_REFRESH] Final Failure:', error.message)
    return new Response(JSON.stringify({ 
      success: false, 
      error: error.message 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })
  }
})
