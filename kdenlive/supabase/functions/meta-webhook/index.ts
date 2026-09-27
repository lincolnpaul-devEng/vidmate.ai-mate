import { serve } from 'https://deno.land/std@0.168.0/http/server.ts'
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.50.0'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

function timingSafeEqual(a: Uint8Array, b: Uint8Array) {
  if (a.length !== b.length) return false
  let diff = 0
  for (let i = 0; i < a.length; i++) diff |= a[i] ^ b[i]
  return diff === 0
}

async function verifyMetaSignature(req: Request, rawBody: Uint8Array) {
  const appSecret = Deno.env.get('META_APP_SECRET')
  if (!appSecret) return { ok: true, reason: 'META_APP_SECRET not set (signature not enforced)' }

  const sig256 = req.headers.get('x-hub-signature-256')
  if (!sig256?.startsWith('sha256=')) return { ok: false, reason: 'Missing x-hub-signature-256' }

  const expected = await crypto.subtle.sign(
    'HMAC',
    await crypto.subtle.importKey(
      'raw',
      new TextEncoder().encode(appSecret),
      { name: 'HMAC', hash: 'SHA-256' },
      false,
      ['sign'],
    ),
    rawBody,
  )

  const expectedHex = Array.from(new Uint8Array(expected))
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('')

  const receivedHex = sig256.slice('sha256='.length).trim()

  const expectedBytes = new TextEncoder().encode(expectedHex)
  const receivedBytes = new TextEncoder().encode(receivedHex)

  return {
    ok: timingSafeEqual(expectedBytes, receivedBytes),
    reason: 'x-hub-signature-256 mismatch',
  }
}

serve(async (req) => {
  if (req.method === 'OPTIONS') return new Response('ok', { headers: corsHeaders })

  // GET: Meta verification handshake
  if (req.method === 'GET') {
    const url = new URL(req.url)
    const mode = url.searchParams.get('hub.mode')
    const token = url.searchParams.get('hub.verify_token')
    const challenge = url.searchParams.get('hub.challenge')

    const verifyToken = Deno.env.get('META_WEBHOOK_VERIFY_TOKEN') || 'socialpulse_secure_verify_2026'

    if (mode === 'subscribe' && token === verifyToken && challenge) {
      return new Response(challenge, { status: 200, headers: corsHeaders })
    }

    return new Response('Forbidden', { status: 403, headers: corsHeaders })
  }

  if (req.method !== 'POST') {
    return new Response('Method Not Allowed', { status: 405, headers: corsHeaders })
  }

  // POST: Ingest comment-like payloads into inbox_items
  const raw = new Uint8Array(await req.arrayBuffer())
  const sig = await verifyMetaSignature(req, raw)
  if (!sig.ok) {
    return new Response(JSON.stringify({ error: 'Unauthorized', details: sig.reason }), {
      status: 401,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }

  let payload: any
  try {
    payload = JSON.parse(new TextDecoder().decode(raw))
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON' }), {
      status: 400,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }

  const supabaseUrl = Deno.env.get('SUPABASE_URL') ?? ''
  const serviceKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
  if (!supabaseUrl || !serviceKey) {
    return new Response(JSON.stringify({ error: 'Server misconfigured' }), {
      status: 500,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }

  const supabase = createClient(supabaseUrl, serviceKey)

  if (payload?.object !== 'page' && payload?.object !== 'instagram') {
    return new Response(JSON.stringify({ ok: true, ignored: true }), {
      status: 200,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    })
  }

  const sourcePlatform = payload.object === 'instagram' ? 'ig' : 'fb'
  const entries = Array.isArray(payload.entry) ? payload.entry : []

  // NOTE: This keeps parity with the old Next.js endpoint: it accepts a user_id if provided.
  // For production correctness, map entry.id (Page/IG account) → a specific user_id via linked_accounts.
  const results: any[] = []

  for (const entry of entries) {
    const changes = Array.isArray(entry?.changes) ? entry.changes : []
    for (const change of changes) {
      if (change?.field !== 'feed' && change?.field !== 'comments') continue
      const v = change?.value || {}

      const userId =
        v.user_id ||
        Deno.env.get('WEBHOOK_FALLBACK_USER_ID') ||
        '00000000-0000-0000-0000-000000000000'

      const externalId = v.comment_id || v.id
      if (!externalId) continue

      const upsertRes = await supabase
        .from('inbox_items')
        .upsert(
          {
            user_id: userId,
            source_platform: sourcePlatform,
            content: v.message || v.text || '',
            author_meta: {
              name: v.from?.name || 'Unknown User',
              id: v.from?.id,
            },
            external_id: externalId,
            created_at: new Date().toISOString(),
          },
          { onConflict: 'user_id,external_id,source_platform' },
        )

      results.push({ external_id: externalId, error: upsertRes.error?.message })
    }
  }

  return new Response(JSON.stringify({ ok: true, results }), {
    status: 200,
    headers: { ...corsHeaders, 'Content-Type': 'application/json' },
  })
})

