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
    const { public_ids, resource_type = 'video' } = await req.json()
    const authHeader = req.headers.get('Authorization')

    if (!public_ids || !Array.isArray(public_ids) || public_ids.length === 0) {
      throw new Error('public_ids array is required')
    }

    // 1. Verify User Session for Security
    if (authHeader) {
      const supabaseClient = createClient(
        Deno.env.get('SUPABASE_URL') ?? '',
        Deno.env.get('SUPABASE_ANON_KEY') ?? '',
        { global: { headers: { Authorization: authHeader } } }
      )
      const { data: { user } } = await supabaseClient.auth.getUser()
      if (!user) throw new Error('Unauthenticated request')
    } else {
      throw new Error('Authorization header missing')
    }

    // 2. Load Cloudinary Config
    const cloudinaryApiSecret = Deno.env.get('CLOUDINARY_API_SECRET');
    const cloudinaryApiKey = Deno.env.get('CLOUDINARY_API_KEY');
    const cloudinaryCloudName = Deno.env.get('CLOUDINARY_CLOUD_NAME');

    if (!cloudinaryApiSecret || !cloudinaryApiKey || !cloudinaryCloudName) {
      throw new Error('Cloudinary configuration missing on server');
    }

    const deleted = [];
    const errors = [];

    // 3. Delete Assets Backend-to-Backend
    for (const public_id of public_ids) {
      const timestamp = Math.round(new Date().getTime() / 1000);
      const signatureSource = `public_id=${public_id}&timestamp=${timestamp}${cloudinaryApiSecret}`;
      
      const signatureArrayBuffer = await crypto.subtle.digest(
        "SHA-1",
        new TextEncoder().encode(signatureSource)
      )
      
      const signature = Array.from(new Uint8Array(signatureArrayBuffer))
        .map(b => b.toString(16).padStart(2, '0'))
        .join('');

      const formData = new URLSearchParams()
      formData.append('public_id', public_id)
      formData.append('timestamp', timestamp.toString())
      formData.append('api_key', cloudinaryApiKey)
      formData.append('signature', signature)

      const res = await fetch(`https://api.cloudinary.com/v1_1/${cloudinaryCloudName}/${resource_type}/destroy`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: formData.toString()
      })

      const data = await res.json()
      if (data.result === 'ok') {
        deleted.push(public_id)
      } else {
        errors.push({ public_id, error: data })
      }
    }

    return new Response(JSON.stringify({ success: true, deleted, errors }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 200
    })

  } catch (error: any) {
    return new Response(JSON.stringify({ success: false, error: error.message }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400
    })
  }
})
