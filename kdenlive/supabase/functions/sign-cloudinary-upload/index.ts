import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
  'Access-Control-Allow-Methods': 'POST, OPTIONS'
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    // 1. Get configuration
    const cloudinaryApiSecret = Deno.env.get('CLOUDINARY_API_SECRET');
    const cloudinaryApiKey = Deno.env.get('CLOUDINARY_API_KEY');
    const cloudinaryCloudName = Deno.env.get('CLOUDINARY_CLOUD_NAME');

    if (!cloudinaryApiSecret || !cloudinaryApiKey || !cloudinaryCloudName) {
      const missing = [];
      if (!cloudinaryApiSecret) missing.push('CLOUDINARY_API_SECRET');
      if (!cloudinaryApiKey) missing.push('CLOUDINARY_API_KEY');
      if (!cloudinaryCloudName) missing.push('CLOUDINARY_CLOUD_NAME');
      
      return new Response(
        JSON.stringify({ 
          error: 'Configuration missing', 
          details: `Missing: ${missing.join(', ')}. Please run 'supabase secrets set' for these variables.` 
        }),
        { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 500 }
      );
    }

    // 2. Clear parameters to sign (exclude signature and api_key)
    // We expect the client to pass parameters in the body (e.g., timestamp, upload_preset, etc.)
    let body;
    try {
      body = await req.json();
    } catch {
      body = {};
    }

    const timestamp = body.timestamp || Math.round(new Date().getTime() / 1000);
    
    // Cloudinary requires all parameters to be signed except:
    // api_key, signature, file, cloud_name, resource_type, callback
    const paramsToSign: Record<string, any> = {
      ...body,
      timestamp: timestamp,
    };

    // Filter out restricted keys
    const restricted = ['api_key', 'signature', 'file', 'cloud_name', 'resource_type', 'callback'];
    restricted.forEach(key => delete paramsToSign[key]);

    // 3. Generate signature
    // Sort parameters alphabetically
    const sortedKeys = Object.keys(paramsToSign).sort();
    const signatureSource = sortedKeys
      .map(key => `${key}=${paramsToSign[key]}`)
      .join('&') + cloudinaryApiSecret;

    const signature = await crypto.subtle.digest(
      "SHA-1",
      new TextEncoder().encode(signatureSource)
    ).then(hash => {
      return Array.from(new Uint8Array(hash))
        .map(b => b.toString(16).padStart(2, '0'))
        .join('');
    });

    return new Response(
      JSON.stringify({
        signature,
        timestamp,
        api_key: cloudinaryApiKey,
        cloud_name: cloudinaryCloudName,
        params_signed: sortedKeys
      }),
      {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 200,
      }
    )
  } catch (error) {
    return new Response(
      JSON.stringify({ error: 'Internal Server Error', details: error.message }),
      {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 500,
      }
    )
  }
})
