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

  const url = new URL(req.url)
  const code = url.searchParams.get('code')
  const stateStr = url.searchParams.get('state')
  const error = url.searchParams.get('error')

  if (error) {
    console.error(`[META_CALLBACK] OAuth Error: ${error}`)
    const redirectUrl = `socialpulse://auth-success?success=false&error=${encodeURIComponent(error)}`
    return Response.redirect(redirectUrl, 302)
  }

  if (!code || !stateStr) {
    return new Response('Missing code or state', { status: 400 })
  }

  try {
    const state = JSON.parse(decodeURIComponent(stateStr))
    const { platform, user_id } = state

    const FB_APP_ID = Deno.env.get('FACEBOOK_APP_ID')
    const FB_APP_SECRET = Deno.env.get('FACEBOOK_APP_SECRET')
    const IG_APP_SECRET = Deno.env.get('INSTAGRAM_APP_SECRET') || FB_APP_SECRET
    const APP_SECRET = platform === 'instagram' ? IG_APP_SECRET : FB_APP_SECRET
    const REDIRECT_URI = 'https://velo.tedoraltd.com/callback/meta'

    console.log(`[META_CALLBACK] Finalizing ${platform} for user: ${user_id}`)

    // 1. Exchange code for Short-Lived User Access Token
    const exchangeUrl = new URL('https://graph.facebook.com/v21.0/oauth/access_token')
    exchangeUrl.searchParams.append('client_id', FB_APP_ID!)
    exchangeUrl.searchParams.append('client_secret', APP_SECRET!)
    exchangeUrl.searchParams.append('redirect_uri', REDIRECT_URI)
    exchangeUrl.searchParams.append('code', code)

    const exchangeRes = await fetch(exchangeUrl.toString())
    const exchangeData = await exchangeRes.json()

    if (exchangeData.error) throw new Error(exchangeData.error.message)

    const shortLivedToken = exchangeData.access_token

    // 2. Exchange for Long-Lived User Access Token (60 days)
    const longLivedUrl = new URL('https://graph.facebook.com/v21.0/oauth/access_token')
    longLivedUrl.searchParams.append('grant_type', 'fb_exchange_token')
    longLivedUrl.searchParams.append('client_id', FB_APP_ID!)
    longLivedUrl.searchParams.append('client_secret', APP_SECRET!)
    longLivedUrl.searchParams.append('fb_exchange_token', shortLivedToken)

    const longLivedRes = await fetch(longLivedUrl.toString())
    const longLivedData = await longLivedRes.json()

    if (longLivedData.error) throw new Error(longLivedData.error.message)

    const longLivedUserToken = longLivedData.access_token

    // 3. Fetch specific Platform Data (Page tokens for FB, IG Business Info for IG)
    let userData = { name: `${platform} User`, id: '', token: longLivedUserToken }

    if (platform === 'facebook') {
      // Get the list of pages and their tokens (these will be long-lived tokens)
      const accountsRes = await fetch(`https://graph.facebook.com/v21.0/me/accounts?access_token=${longLivedUserToken}`)
      const accountsData = await accountsRes.json()
      
      if (accountsData.data && accountsData.data.length > 0) {
        // Just pick the first page for now, or handle multiple in your app logic
        const page = accountsData.data[0]
        userData = {
          name: page.name,
          id: page.id,
          token: page.access_token // Page access token (long-lived if the user token was long-lived)
        }
      } else {
        // Fallback to user profile if no pages found
        const meRes = await fetch(`https://graph.facebook.com/me?fields=id,name&access_token=${longLivedUserToken}`)
        const meData = await meRes.json()
        userData = { name: meData.name, id: meData.id, token: longLivedUserToken }
      }
    } else if (platform === 'instagram') {
      // Get IG Business account linked to a page
      const pagesRes = await fetch(`https://graph.facebook.com/v21.0/me/accounts?fields=name,access_token,instagram_business_account{id,username,name,profile_picture_url}&access_token=${longLivedUserToken}`)
      const pagesData = await pagesRes.json()
      
      const igPage = pagesData.data?.find((p: any) => p.instagram_business_account)
      if (igPage) {
        userData = {
          name: igPage.instagram_business_account.username || igPage.instagram_business_account.name,
          id: igPage.instagram_business_account.id,
          token: igPage.access_token // We use the Page Token to publish to IG Business API
        }
      }
    }

    // 4. Persist to Database
    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    console.log(`[META_CALLBACK] Persisting for user: ${user_id} (${userData.name})`)
    
    const { error: dbError } = await supabaseClient
      .from('linked_accounts')
      .upsert({
        user_id: user_id,
        platform: platform,
        connected: true,
        username: userData.name,
        external_id: userData.id,
        access_token: userData.token, // Store the long-lived Page/User token
        updated_at: new Date().toISOString()
      }, { onConflict: 'user_id,platform' })

    if (dbError) throw dbError

    // 5. Final Jump back to the web proxy success page
    const webSuccessUrl = `https://velo.tedoraltd.com/auth-success?platform=${platform}&success=true&user_id=${user_id}`
    
    return new Response(null, {
      status: 302,
      headers: {
        'Location': webSuccessUrl,
        'Access-Control-Allow-Origin': '*',
      },
    })

  } catch (err: any) {
    const errorMessage = err?.message || 'Internal Callback Error';
    console.error('[META_CALLBACK] Failure:', errorMessage, err);
    
    const errorUrl = `https://velo.tedoraltd.com/auth-success?platform=meta&success=false&error=${encodeURIComponent(errorMessage)}`;
    return new Response(null, {
      status: 302,
      headers: {
        'Location': errorUrl,
        'Access-Control-Allow-Origin': '*',
      },
    });
  }
})
