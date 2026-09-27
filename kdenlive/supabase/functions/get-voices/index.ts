import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  // Handle CORS Preflight
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const apiKey = Deno.env.get('ELEVENLABS_API_KEY')
    
    if (!apiKey) {
      console.error('❌ ELEVENLABS_API_KEY is not set in Supabase secrets')
      throw new Error('ELEVENLABS_API_KEY not set in Supabase secrets')
    }

    console.log('🗣️ Fetching voices from ElevenLabs API...')
    const response = await fetch('https://api.elevenlabs.io/v1/voices', {
      method: 'GET',
      headers: {
        'xi-api-key': apiKey,
        'Content-Type': 'application/json',
      }
    })

    if (!response.ok) {
      const errText = await response.text();
      console.error(`❌ ElevenLabs API responded with status ${response.status}:`, errText)
      throw new Error(`ElevenLabs API responded with status ${response.status}`)
    }

    const data = await response.json()
    console.log(`✅ Successfully fetched ${data.voices?.length || 0} voices`)

    return new Response(JSON.stringify(data), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })
  } catch (error: any) {
    console.error('❌ get-voices error:', error)
    return new Response(JSON.stringify({ 
      success: false, 
      error: error.message 
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400,
    })
  }
})
