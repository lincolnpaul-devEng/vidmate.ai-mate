import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.38.4"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type, x-user-id',
}

// 5. Rate Limiting: Token Bucket Algorithm State
interface RateLimiter {
  tokens: number;
  lastRefill: number;
  maxTokens: number;
  refillRate: number; // tokens per second
}

const limiters: Record<string, RateLimiter> = {};

function rateLimit(userId: string): boolean {
  const now = Date.now();
  if (!limiters[userId]) {
    limiters[userId] = {
      tokens: 10,
      lastRefill: now,
      maxTokens: 10,
      refillRate: 0.5, // 1 token every 2 seconds
    };
  }

  const limiter = limiters[userId];
  const elapsed = (now - limiter.lastRefill) / 1000;
  limiter.tokens = Math.min(limiter.maxTokens, limiter.tokens + elapsed * limiter.refillRate);
  limiter.lastRefill = now;

  if (limiter.tokens >= 1) {
    limiter.tokens -= 1;
    return true;
  }
  return false;
}

// Minimum and Maximum Budget Rules per Network (Validation Engine)
const BUDGET_RULES: Record<string, { minDaily: number; maxDaily: number }> = {
  'facebook': { minDaily: 1, maxDaily: 50000 },
  'google': { minDaily: 5, maxDaily: 100000 },
  'linkedin': { minDaily: 10, maxDaily: 25000 },
  'twitter': { minDaily: 5, maxDaily: 20000 },
  'tiktok': { minDaily: 20, maxDaily: 30000 }
};

serve(async (req) => {
  // CORS preflight
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  const url = new URL(req.url);
  const path = url.pathname.replace(/\/+$/, '');

  try {
    const supabaseUrl = Deno.env.get('SUPABASE_URL') ?? '';
    const supabaseKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? '';
    const supabase = createClient(supabaseUrl, supabaseKey);

    // --- webhook pipeline endpoint ---
    if (path.endsWith('/webhook') || url.searchParams.get('webhook') === 'true') {
      const body = await req.json().catch(() => ({}));
      console.log('[AD_AGGREGATOR_WEBHOOK] Received webhook payload:', body);

      // Handle compliance changes from platform webhook (e.g. Meta ads account alerts)
      const { ad_account_id, status_warning, campaign_id, issue_type } = body;
      
      if (campaign_id && status_warning) {
        // Automatically transition status to "Active with issue" or relevant status
        const nextStatus = issue_type === 'payment' || issue_type === 'disapproved' ? 'Active with issue' : 'Active';
        
        await supabase
          .from('ad_campaigns')
          .update({ status: nextStatus })
          .eq('campaign_id', campaign_id);

        console.log(`[AD_AGGREGATOR_WEBHOOK] Updated campaign ${campaign_id} to ${nextStatus}`);
      }

      return new Response(JSON.stringify({ success: true, message: 'Webhook processed' }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 200
      });
    }

    // --- Campaign listing & Sync trigger ---
    if (req.method === 'GET') {
      const userId = url.searchParams.get('user_id') || req.headers.get('x-user-id');
      if (!userId) {
        return new Response(JSON.stringify({ error: 'Missing user_id parameter or header' }), {
          status: 400,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' }
        });
      }

      // Fetch from internal ad_campaigns database
      const { data: campaigns, error: dbError } = await supabase
        .from('ad_campaigns')
        .select('*')
        .eq('user_id', userId)
        .order('created_at', { ascending: false });

      if (dbError) throw dbError;

      // Simulated dynamic polling check: Auto-refresh budget/status via metrics engine
      const updatedCampaigns = campaigns.map(c => {
        // Simulation of ledger telemetry updates (State 1 -> State 2 transitions)
        if (c.status === 'Pending') {
          // 30% chance it transitions to active
          const isApproved = Math.random() > 0.4;
          c.status = isApproved ? 'Active' : 'Pending';
        }
        
        // Pacing alert (State 3): Trigger warning if campaign spent reaches budget
        if (c.spent >= c.budget && c.budget > 0) {
          c.status = 'Paused'; // Out of funds
        }

        // Live ROAS Engine — uses first-party attribution revenue
        const totalRevenue = parseFloat(c.total_revenue) || 0;
        const totalSpent = parseFloat(c.spent) || 1;
        c.roas = totalSpent > 0 ? (totalRevenue / totalSpent).toFixed(2) : '0.00';
        c.total_revenue = totalRevenue;
        c.conversion_count = c.conversion_count || 0;

        // Legacy ROI fallback for campaigns without attribution data
        const conversionVal = c.result_value * 45;
        c.roi = totalRevenue > 0 
          ? (totalRevenue / totalSpent).toFixed(1) 
          : (totalSpent > 0 ? (conversionVal / totalSpent).toFixed(1) : '0.0');

        return c;
      });

      // Write back updated statuses if changed
      for (const camp of updatedCampaigns) {
        await supabase
          .from('ad_campaigns')
          .update({ status: camp.status })
          .eq('id', camp.id);
      }

      return new Response(JSON.stringify({ success: true, campaigns: updatedCampaigns }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 200
      });
    }

    // --- Campaign Creation: Dispatcher Pipeline (POST) ---
    if (req.method === 'POST') {
      const { user_id, name, budget, objective, creative_url, platform, ad_sets, creatives, demographics } = await req.json();

      if (!user_id || !name || !budget || !objective || !platform) {
        return new Response(JSON.stringify({ error: 'Missing required creation fields' }), {
          status: 400,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' }
        });
      }

      // Apply Leaky Bucket Rate Limiting per User
      if (!rateLimit(user_id)) {
        return new Response(JSON.stringify({ error: 'Rate limit exceeded. Please try again shortly.' }), {
          status: 429,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' }
        });
      }

      // Validation Engine checking min/max constraints
      const cleanPlatform = platform.toLowerCase().replace(' ads', '');
      const rules = BUDGET_RULES[cleanPlatform] || { minDaily: 1, maxDaily: 100000 };
      const budgetNum = parseFloat(budget);

      if (budgetNum < rules.minDaily || budgetNum > rules.maxDaily) {
        return new Response(JSON.stringify({
          error: `Budget validation failed: ${platform} budget must be between $${rules.minDaily} and $${rules.maxDaily} daily.`
        }), {
          status: 400,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' }
        });
      }

      // Retrieve connected account from DB
      const { data: account } = await supabase
        .from('linked_accounts')
        .select('*')
        .eq('user_id', user_id)
        .eq('platform', cleanPlatform)
        .single();

      // Simulated platform API endpoints mapping (AdCampaignDispatcher)
      let externalCampaignId = `c-ext-${Math.floor(Math.random() * 900000 + 100000)}`;
      let status = 'Pending'; // State 1: Pending Platform Review
      
      console.log(`[AdCampaignDispatcher] Dispatching campaign "${name}" to ${platform} payload...`);
      
      if (cleanPlatform === 'meta') {
        const adAccountId = account?.external_id || 'act_1028374659';
        console.log(`[META] POST to /act_${adAccountId}/campaigns`);
      } else if (cleanPlatform === 'google') {
        const customerId = account?.external_id || '102-837-4659';
        console.log(`[GOOGLE] Dispatching to CampaignService.MutateCampaigns with Customer ID ${customerId}`);
      } else if (cleanPlatform === 'linkedin') {
        const urn = account?.external_id || 'urn:li:sponsoredAccount:482937';
        console.log(`[LINKEDIN] POST to /v2/adCampaignsV2 with URN ${urn}`);
      } else if (cleanPlatform === 'twitter' || cleanPlatform === 'tiktok') {
        console.log(`[${cleanPlatform.toUpperCase()}] Dispatching mutation payload matching standard configs`);
      }

      // Save Campaign locally to DB as Pending/In Review
      const newCamp: Record<string, any> = {
        user_id,
        campaign_id: externalCampaignId,
        name,
        platform,
        objective,
        status, // Pending
        spent: 0,
        budget: budgetNum,
        impressions: 0,
        clicks: 0,
        ctr: 0,
        result_value: 0,
        result_label: objective === 'Leads' ? 'Leads' : objective === 'Conversions' ? 'Conversions' : objective === 'Post Engagement' ? 'Engagements' : 'Reach'
      };

      // Attach multi-variate campaign data if provided
      if (ad_sets) newCamp.ad_sets = ad_sets;
      if (creatives) newCamp.creatives = creatives;
      if (demographics) newCamp.demographics = demographics;

      const { data: savedCamp, error: dbError } = await supabase
        .from('ad_campaigns')
        .insert(newCamp)
        .select()
        .single();

      if (dbError) throw dbError;

      return new Response(JSON.stringify({ success: true, campaign: savedCamp }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 200
      });
    }

    throw new Error(`Method ${req.method} not allowed`);

  } catch (error: any) {
    console.error('[AD_AGGREGATOR_ERROR]:', error.message);
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    });
  }
});
