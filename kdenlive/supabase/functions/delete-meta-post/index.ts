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
    const { platform, external_id, post_id } = await req.json()
    const authHeader = req.headers.get('Authorization')

    console.log(`[META_DELETE] Request received for ${platform}: ${external_id}, post: ${post_id}`);

    if (!platform || !external_id || !post_id || !authHeader) {
      throw new Error('platform, external_id, post_id, and Authorization are required')
    }

    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_ANON_KEY') ?? '',
      { global: { headers: { Authorization: authHeader } } }
    )

    // 1. Fetch the linked account details (token)
    // We use the authenticated user's token for security
    const { data: account, error: accountError } = await supabaseClient
      .from('linked_accounts')
      .select('access_token')
      .eq('platform', platform === 'instagram' ? 'instagram' : 'facebook')
      .single()

    if (accountError || !account?.access_token) {
        throw new Error(`Account connection not found for ${platform}`)
    }

    const access_token = account.access_token

    // 2. Perform Meta API Deletion
    console.log(`[META_DELETE] Sending DELETE request to Meta for ${external_id}...`);
    
    // Facebook and Instagram both use the same DELETE /{id} structure on the Graph API
    const metaUrl = new URL(`https://graph.facebook.com/v21.0/${external_id}`)
    metaUrl.searchParams.append('access_token', access_token)

    const metaRes = await fetch(metaUrl.toString(), {
      method: 'DELETE'
    })

    const metaData = await metaRes.json()
    console.log(`[META_DELETE] Meta responded with:`, metaData);

    // If success: true or it was already deleted (error contains 'Object with ID ... does not exist')
    if (!metaRes.ok) {
        // Handle "Post already deleted" edge case gracefully
        if (metaData.error?.code === 100 || metaData.error?.message?.includes('does not exist')) {
            console.log(`[META_DELETE] Post ${external_id} was already removed from ${platform}. Proceeding to cleanup.`);
        } else {
            throw new Error(`Meta API Error: ${metaData.error?.message || 'Unknown error'}`)
        }
    }

    // 3. Database Cleanup (Only if Meta succeeded or post was already gone)
    console.log(`[META_DELETE] Step 3: Removing record ${post_id} from Supabase...`);
    const serviceClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    const { error: dbError } = await supabaseClient
      .from('posts')
      .delete()
      .eq('id', post_id)

    if (dbError) {
      console.error(`[META_DELETE] Supabase Delete Failed: ${dbError.message}`);
      throw dbError
    }

    console.log(`[META_DELETE] Deletion complete for post ${post_id}`);
    return new Response(JSON.stringify({ success: true }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })

  } catch (error: any) {
    console.error('[META_DELETE] Error:', error.message)
    return new Response(JSON.stringify({ success: false, error: error.message }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400
    })
  }
})
