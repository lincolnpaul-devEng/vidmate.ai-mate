import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const { metrics } = await req.json()
    console.log('[AI_STRATEGY] Received metrics:', JSON.stringify(metrics))

    const openRouterKey = Deno.env.get('OPENROUTER_API_KEY')

    if (!openRouterKey) {
      console.error('[AI_STRATEGY] Config Error: OpenRouter API key missing in environment.')
      throw new Error('OpenRouter API key missing')
    }

    const prompt = `You are a professional social media data scientist. Analyze these performance metrics across platforms (YouTube, Instagram, Facebook):
    ${JSON.stringify(metrics)}
    
    Provide:
    1. A current market trend relevant to this creator's niche based on their data.
    2. A strategic "next move" (actionable advice for the next post).
    3. A general account "health score" from 0 to 100.
    
    Return ONLY a JSON object with keys: "trend", "nextMove", "score".`

    console.log('[AI_STRATEGY] Invoking OpenRouter...')
    const response = await fetch("https://openrouter.ai/api/v1/chat/completions", {
      method: "POST",
      headers: {
        "Authorization": `Bearer ${openRouterKey}`,
        "Content-Type": "application/json",
        "HTTP-Referer": "https://velo.tedoraltd.com",
        "X-Title": "SocialPulse Strategy Desk",
      },
      body: JSON.stringify({
        "model": "deepseek/deepseek-chat",
        "messages": [
          { "role": "system", "content": "You provide data-driven social media advice. You must return only valid JSON." },
          { "role": "user", "content": prompt }
        ],
        "response_format": { "type": "json_object" },
        "max_tokens": 1000
      })
    });

    if (!response.ok) {
      const errorText = await response.text();
      console.error(`[AI_STRATEGY] OpenRouter HTTP ${response.status}:`, errorText);
      throw new Error(`AI Provider Error (${response.status})`);
    }

    const data = await response.json();
    if (data.error) {
      console.error('[AI_STRATEGY] API Response Error:', JSON.stringify(data.error));
      throw new Error(`OpenRouter Error: ${data.error.message || JSON.stringify(data.error)}`);
    }

    const content = data.choices?.[0]?.message?.content;
    if (!content) {
      console.error('[AI_STRATEGY] No content in response:', JSON.stringify(data));
      throw new Error('No strategy content generated from AI');
    }

    console.log('[AI_STRATEGY] Strategy generated successfully.');
    return new Response(content, {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200,
    })

  } catch (error: any) {
    console.error('[AI_STRATEGY] Exception caught:', error.message)
    return new Response(JSON.stringify({
      success: false,
      error: error.message || 'Internal Server Error'
    }), {
      status: 200,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }
})
