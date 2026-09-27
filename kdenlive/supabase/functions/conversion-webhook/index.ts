import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.38.4"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type, x-user-id, stripe-signature, x-shopify-hmac-sha256, x-provider',
}

// HMAC-SHA256 verification for Stripe and Shopify
async function verifyHmacSha256(payload: string, signature: string, secret: string): Promise<boolean> {
  try {
    const encoder = new TextEncoder();
    const key = await crypto.subtle.importKey(
      'raw', encoder.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']
    );
    const sig = await crypto.subtle.sign('HMAC', key, encoder.encode(payload));
    const computed = Array.from(new Uint8Array(sig)).map(b => b.toString(16).padStart(2, '0')).join('');
    return computed === signature.toLowerCase();
  } catch {
    return false;
  }
}

// Parse Stripe signature header: t=timestamp,v1=signature
function parseStripeSignature(header: string): { timestamp: string; signature: string } | null {
  try {
    const parts: Record<string, string> = {};
    header.split(',').forEach(part => {
      const [key, val] = part.split('=');
      if (key && val) parts[key.trim()] = val.trim();
    });
    return parts.t && parts.v1 ? { timestamp: parts.t, signature: parts.v1 } : null;
  } catch {
    return null;
  }
}

interface ConversionEvent {
  user_id: string;
  campaign_id: string | null;
  provider: string;
  event_type: string;
  revenue: number;
  currency: string;
  click_id: string | null;
  utm_source: string | null;
  utm_medium: string | null;
  utm_campaign: string | null;
  utm_content: string | null;
  utm_term: string | null;
  customer_email: string | null;
  order_id: string | null;
  raw_payload: Record<string, unknown>;
}

// Extract UTM parameters from a URL string
function extractUtmParams(url: string): Record<string, string | null> {
  try {
    const parsed = new URL(url);
    return {
      utm_source: parsed.searchParams.get('utm_source'),
      utm_medium: parsed.searchParams.get('utm_medium'),
      utm_campaign: parsed.searchParams.get('utm_campaign'),
      utm_content: parsed.searchParams.get('utm_content'),
      utm_term: parsed.searchParams.get('utm_term'),
    };
  } catch {
    return { utm_source: null, utm_medium: null, utm_campaign: null, utm_content: null, utm_term: null };
  }
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders });
  }

  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), {
      status: 405, headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    });
  }

  try {
    const supabaseUrl = Deno.env.get('SUPABASE_URL') ?? '';
    const supabaseKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? '';
    const supabase = createClient(supabaseUrl, supabaseKey);

    const rawBody = await req.text();
    const body = JSON.parse(rawBody);
    const url = new URL(req.url);

    // Determine provider from query param, header, or body
    const provider = url.searchParams.get('provider') 
      || req.headers.get('x-provider') 
      || body.provider 
      || 'custom';

    // Resolve user_id from query param or body
    const userId = url.searchParams.get('user_id') || body.user_id;
    if (!userId) {
      return new Response(JSON.stringify({ error: 'Missing user_id parameter' }), {
        status: 400, headers: { ...corsHeaders, 'Content-Type': 'application/json' }
      });
    }

    // Fetch user's stored pixel credentials for signature verification
    const { data: pixel } = await supabase
      .from('conversion_pixels')
      .select('*')
      .eq('user_id', userId)
      .eq('provider', provider)
      .single();

    let event: ConversionEvent | null = null;

    // ─── STRIPE HANDLER ───
    if (provider === 'stripe') {
      const stripeSignatureHeader = req.headers.get('stripe-signature');
      
      if (pixel?.api_key && stripeSignatureHeader) {
        const parsed = parseStripeSignature(stripeSignatureHeader);
        if (parsed) {
          const signedPayload = `${parsed.timestamp}.${rawBody}`;
          const isValid = await verifyHmacSha256(signedPayload, parsed.signature, pixel.api_key);
          if (!isValid) {
            console.warn('[CONVERSION_WEBHOOK] Stripe signature verification failed');
            return new Response(JSON.stringify({ error: 'Invalid Stripe signature' }), {
              status: 401, headers: { ...corsHeaders, 'Content-Type': 'application/json' }
            });
          }
        }
      }

      // Parse Stripe event object
      const stripeEvent = body.type || body.event_type;
      const stripeData = body.data?.object || body;

      if (stripeEvent === 'checkout.session.completed' || stripeEvent === 'payment_intent.succeeded') {
        const amount = (stripeData.amount_total || stripeData.amount || 0) / 100; // Stripe uses cents
        event = {
          user_id: userId,
          campaign_id: stripeData.client_reference_id || stripeData.metadata?.campaign_id || null,
          provider: 'stripe',
          event_type: 'purchase',
          revenue: amount,
          currency: (stripeData.currency || 'kes').toUpperCase(),
          click_id: stripeData.metadata?.click_id || null,
          utm_source: stripeData.metadata?.utm_source || null,
          utm_medium: stripeData.metadata?.utm_medium || null,
          utm_campaign: stripeData.metadata?.utm_campaign || stripeData.client_reference_id || null,
          utm_content: stripeData.metadata?.utm_content || null,
          utm_term: null,
          customer_email: stripeData.customer_email || stripeData.receipt_email || null,
          order_id: stripeData.id || null,
          raw_payload: body,
        };
      }
    }

    // ─── SHOPIFY HANDLER ───
    else if (provider === 'shopify') {
      const shopifyHmac = req.headers.get('x-shopify-hmac-sha256');

      if (pixel?.api_key && shopifyHmac) {
        const isValid = await verifyHmacSha256(rawBody, shopifyHmac, pixel.api_key);
        if (!isValid) {
          console.warn('[CONVERSION_WEBHOOK] Shopify HMAC verification failed');
          return new Response(JSON.stringify({ error: 'Invalid Shopify HMAC' }), {
            status: 401, headers: { ...corsHeaders, 'Content-Type': 'application/json' }
          });
        }
      }

      // Parse Shopify order webhook
      const totalPrice = parseFloat(body.total_price || '0');
      const landingSite = body.landing_site || '';
      const utmParams = extractUtmParams(landingSite.startsWith('http') ? landingSite : `https://x.com${landingSite}`);

      event = {
        user_id: userId,
        campaign_id: null,
        provider: 'shopify',
        event_type: 'purchase',
        revenue: totalPrice,
        currency: (body.currency || 'KES').toUpperCase(),
        click_id: null,
        utm_source: utmParams.utm_source,
        utm_medium: utmParams.utm_medium,
        utm_campaign: utmParams.utm_campaign,
        utm_content: utmParams.utm_content,
        utm_term: utmParams.utm_term,
        customer_email: body.email || body.contact_email || null,
        order_id: body.order_number?.toString() || body.id?.toString() || null,
        raw_payload: body,
      };
    }

    // ─── GA4 HANDLER ───
    else if (provider === 'ga4') {
      // GA4 Measurement Protocol server-side event
      // Accepts standardized event payload
      const ga4Event = body.events?.[0] || body;
      const params = ga4Event.params || {};

      event = {
        user_id: userId,
        campaign_id: null,
        provider: 'ga4',
        event_type: ga4Event.name === 'purchase' ? 'purchase'
          : ga4Event.name === 'sign_up' ? 'signup'
          : ga4Event.name === 'generate_lead' ? 'lead'
          : ga4Event.name === 'add_to_cart' ? 'add_to_cart'
          : 'custom',
        revenue: parseFloat(params.value || params.revenue || '0'),
        currency: (params.currency || 'KES').toUpperCase(),
        click_id: params.gclid || params.click_id || null,
        utm_source: params.source || body.utm_source || null,
        utm_medium: params.medium || body.utm_medium || null,
        utm_campaign: params.campaign || body.utm_campaign || null,
        utm_content: params.content || body.utm_content || null,
        utm_term: params.term || body.utm_term || null,
        customer_email: null,
        order_id: params.transaction_id || null,
        raw_payload: body,
      };
    }

    // ─── GENERIC/CUSTOM HANDLER ───
    else {
      event = {
        user_id: userId,
        campaign_id: body.campaign_id || null,
        provider: 'custom',
        event_type: body.event_type || 'custom',
        revenue: parseFloat(body.revenue || '0'),
        currency: (body.currency || 'KES').toUpperCase(),
        click_id: body.click_id || null,
        utm_source: body.utm_source || null,
        utm_medium: body.utm_medium || null,
        utm_campaign: body.utm_campaign || null,
        utm_content: body.utm_content || null,
        utm_term: body.utm_term || null,
        customer_email: body.email || null,
        order_id: body.order_id || null,
        raw_payload: body,
      };
    }

    if (!event) {
      return new Response(JSON.stringify({ success: true, message: 'Event type not tracked, skipped.' }), {
        status: 200, headers: { ...corsHeaders, 'Content-Type': 'application/json' }
      });
    }

    // ─── CAMPAIGN MATCHING ───
    // Try to match utm_campaign to an existing ad_campaigns.campaign_id
    let matchedCampaignId = event.campaign_id;

    if (!matchedCampaignId && event.utm_campaign) {
      const { data: matchedCamp } = await supabase
        .from('ad_campaigns')
        .select('campaign_id')
        .eq('user_id', userId)
        .eq('campaign_id', event.utm_campaign)
        .single();

      if (matchedCamp) {
        matchedCampaignId = matchedCamp.campaign_id;
      }
    }

    event.campaign_id = matchedCampaignId;

    // ─── PERSIST EVENT ───
    const { error: insertError } = await supabase
      .from('conversion_events')
      .insert(event);

    if (insertError) {
      console.error('[CONVERSION_WEBHOOK] Insert failed:', insertError.message);
      throw insertError;
    }

    // ─── ROLLUP TO CAMPAIGN ───
    if (matchedCampaignId && event.revenue > 0) {
      // Fetch current totals
      const { data: currentCamp } = await supabase
        .from('ad_campaigns')
        .select('total_revenue, conversion_count')
        .eq('user_id', userId)
        .eq('campaign_id', matchedCampaignId)
        .single();

      if (currentCamp) {
        await supabase
          .from('ad_campaigns')
          .update({
            total_revenue: (parseFloat(currentCamp.total_revenue) || 0) + event.revenue,
            conversion_count: (currentCamp.conversion_count || 0) + 1,
          })
          .eq('user_id', userId)
          .eq('campaign_id', matchedCampaignId);
      }
    }

    // ─── UPDATE PIXEL LAST EVENT ───
    if (pixel) {
      await supabase
        .from('conversion_pixels')
        .update({ status: 'active', last_event_at: new Date().toISOString() })
        .eq('id', pixel.id);
    }

    console.log(`[CONVERSION_WEBHOOK] ${provider} event processed: ${event.event_type}, revenue=${event.revenue} ${event.currency}, campaign=${matchedCampaignId || 'unmatched'}`);

    return new Response(JSON.stringify({
      success: true,
      event_id: event.order_id,
      matched_campaign: matchedCampaignId,
      revenue: event.revenue,
      currency: event.currency,
    }), {
      status: 200, headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    });

  } catch (error: any) {
    console.error('[CONVERSION_WEBHOOK_ERROR]:', error.message);
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500, headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    });
  }
});
