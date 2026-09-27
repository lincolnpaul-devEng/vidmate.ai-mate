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
    const formData = await req.formData();
    const file = formData.get('file') as File;
    const modelId = formData.get('model_id') as string || 'scribe_v1';
    const diarize = formData.get('diarize') === 'true';
    const tagAudioEvents = formData.get('tag_audio_events') === 'true';
    const numSpeakers = formData.get('num_speakers') as string;
    const languageCode = formData.get('language_code') as string;

    const apiKey = Deno.env.get('ELEVENLABS_API_KEY');
    if (!apiKey) {
      throw new Error('ELEVENLABS_API_KEY not set');
    }

    const elevenLabsFormData = new FormData();
    elevenLabsFormData.append('file', file);
    elevenLabsFormData.append('model_id', modelId);
    elevenLabsFormData.append('diarize', diarize.toString());
    elevenLabsFormData.append('tag_audio_events', tagAudioEvents.toString());
    elevenLabsFormData.append('timestamps_granularity', 'word');
    
    if (numSpeakers) elevenLabsFormData.append('num_speakers', numSpeakers);
    if (languageCode) elevenLabsFormData.append('language_code', languageCode);

    console.log(`[Transcribe] Uploading to ElevenLabs Scribe...`);

    const response = await fetch('https://api.elevenlabs.io/v1/speech-to-text', {
      method: 'POST',
      headers: {
        'xi-api-key': apiKey,
      },
      body: elevenLabsFormData,
    });

    if (!response.ok) {
      const errorText = await response.text();
      console.error(`[Scribe Error] ${response.status}: ${errorText}`);
      return new Response(JSON.stringify({ error: `ElevenLabs error: ${response.status}`, details: errorText }), {
        status: response.status,
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      });
    }

    const data = await response.json();
    return new Response(JSON.stringify(data), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    });

  } catch (error) {
    console.error(`[Edge Function Error] ${error.message}`);
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    });
  }
})
