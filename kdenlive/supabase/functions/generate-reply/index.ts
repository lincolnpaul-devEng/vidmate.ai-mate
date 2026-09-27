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
    const { comment } = await req.json()
    const openRouterKey = Deno.env.get('OPENROUTER_API_KEY')

    if (!openRouterKey) {
      throw new Error('OpenRouter API key missing')
    }

    const prompt = `You are a social media manager assistant. Generate 3 types of replies to this comment: "${comment}".
    1. Professional: Formal and polite.
    2. Casual: Friendly and conversational.
    3. Witty: Clever and engaging.
    Return ONLY a JSON object with keys "professional", "casual", and "witty".`

    const response = await fetch("https://openrouter.ai/api/v1/chat/completions", {
      method: "POST",
      headers: {
        "Authorization": `Bearer ${openRouterKey}`,
        "Content-Type": "application/json",
        "HTTP-Referer": "https://velo.tedoraltd.com",
        "X-Title": "SocialPulse",
      },
      body: JSON.stringify({
        "model": "deepseek/deepseek-chat",
        "messages": [
          { "role": "user", "content": prompt }
        ],
        "max_tokens": 1000,
        "response_format": { "type": "json_object" }
      })
    });

    const data = await response.json();
    if (data.error) {
      throw new Error(`OpenRouter Error: ${data.error.message || JSON.stringify(data.error)}`);
    }

    const content = data.choices?.[0]?.message?.content;
    if (!content) throw new Error('No reply content generated from AI');

    const replies = JSON.parse(content);

    return new Response(JSON.stringify(replies), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200,
    })
  } catch (error) {
    console.error('Reply Generation error:', error);
    return new Response(JSON.stringify({
      success: false,
      error: error.message
    }), {
      status: error.message.includes('API key') ? 401 : 500,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }
})
