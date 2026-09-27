import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.39.0"

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
      throw new Error('ELEVENLABS_API_KEY is not configured in Supabase secrets')
    }

    const { text, voiceId, modelId, speed, stability, similarityBoost, folder } = await req.json()

    if (!text || typeof text !== 'string' || !text.trim()) {
      throw new Error('Text is required for voice generation')
    }

    let targetVoiceId = (voiceId || '21m00Tcm4TlvDq8ikWAM').trim() // Default to Rachel
    const targetModel = modelId || 'eleven_multilingual_v2'

    // If voiceId is a descriptive name (not a 15+ alphanumeric ID), resolve it against ElevenLabs voices
    if (!/^[A-Za-z0-9]{15,}$/.test(targetVoiceId)) {
      try {
        const listRes = await fetch('https://api.elevenlabs.io/v1/voices', {
          headers: { 'xi-api-key': apiKey },
        })
        if (listRes.ok) {
          const listData = await listRes.json()
          const voices = listData.voices || []
          const q = targetVoiceId.toLowerCase()
          const exact = voices.find((v: any) => v.name?.toLowerCase() === q)
          const partial = voices.find((v: any) => v.name?.toLowerCase().includes(q) || q.includes(v.name?.toLowerCase()))
          if (exact?.voice_id) {
            targetVoiceId = exact.voice_id
          } else if (partial?.voice_id) {
            targetVoiceId = partial.voice_id
          } else if (voices.length > 0) {
            // Fallback to first available voice
            targetVoiceId = voices[0].voice_id
          }
        }
      } catch (err) {
        console.warn('Voice name lookup failed, using fallback ID:', err)
        targetVoiceId = '21m00Tcm4TlvDq8ikWAM'
      }
    }

    console.log(`🗣️ Generating ElevenLabs voice for resolved voiceId: ${targetVoiceId}...`)

    const elevenResponse = await fetch(`https://api.elevenlabs.io/v1/text-to-speech/${targetVoiceId}?output_format=mp3_44100_128`, {
      method: 'POST',
      headers: {
        'xi-api-key': apiKey,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        text: text.trim(),
        model_id: targetModel,
        voice_settings: {
          stability: typeof stability === 'number' ? stability : 0.5,
          similarity_boost: typeof similarityBoost === 'number' ? similarityBoost : 0.75,
          speed: typeof speed === 'number' ? speed : 1.0,
        },
      }),
    })

    if (!elevenResponse.ok) {
      const errText = await elevenResponse.text()
      console.error(`❌ ElevenLabs API error ${elevenResponse.status}:`, errText)
      throw new Error(`ElevenLabs TTS failed: ${errText || elevenResponse.statusText}`)
    }

    const audioArrayBuffer = await elevenResponse.arrayBuffer()
    const audioBytes = new Uint8Array(audioArrayBuffer)

    // Calculate approximate duration (MP3 at 128 kbps: 128000 bits/s = 16000 bytes/s)
    const durationSeconds = Math.max(1, Math.round((audioBytes.length / 16000) * 10) / 10)

    // Initialize Supabase Storage client to upload the generated file
    const supabaseUrl = Deno.env.get('SUPABASE_URL')
    const supabaseServiceKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')

    let publicUrl = ''

    if (supabaseUrl && supabaseServiceKey) {
      const supabase = createClient(supabaseUrl, supabaseServiceKey)
      const fileName = `voices/${folder || 'ai-generations'}/${Date.now()}_${crypto.randomUUID().slice(0, 8)}.mp3`

      const { data: uploadData, error: uploadError } = await supabase.storage
        .from('media')
        .upload(fileName, audioBytes, {
          contentType: 'audio/mpeg',
          upsert: true,
        })

      if (!uploadError && uploadData) {
        const { data: publicData } = supabase.storage.from('media').getPublicUrl(fileName)
        publicUrl = publicData.publicUrl
      }
    }

    // If storage upload wasn't used or failed, encode as base64 data URI
    if (!publicUrl) {
      const base64Audio = btoa(String.fromCharCode(...audioBytes))
      publicUrl = `data:audio/mpeg;base64,${base64Audio}`
    }

    console.log(`✅ Voice generated successfully (${durationSeconds}s)`)

    return new Response(JSON.stringify({
      success: true,
      url: publicUrl,
      duration: durationSeconds,
      durationSeconds,
      voiceId: targetVoiceId,
      text: text.trim(),
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200,
    })
  } catch (error: any) {
    console.error('❌ generate-voice error:', error)
    return new Response(JSON.stringify({
      success: false,
      error: error.message || 'Failed to generate voice',
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400,
    })
  }
})
