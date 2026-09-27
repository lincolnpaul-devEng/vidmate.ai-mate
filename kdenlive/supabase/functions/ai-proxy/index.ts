// Supabase Edge Function: ai-proxy
// Proxies AI completion requests and models queries to OpenRouter / Groq / LLM endpoints
// with dynamic model discovery for the Velo / SocialPulse video agent editor.

import { serve } from "https://deno.land/std@0.168.0/http/server.ts";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type, x-openrouter-key, x-velo-provider",
  "Access-Control-Allow-Methods": "POST, GET, OPTIONS",
};

// Capability Models mapping matching the web video agent panel
export const CAPABILITY_MODELS: Record<string, string> = {
  NVIDIA: "nvidia/nemotron-nano-12b-v2-vl:free",
  DEEPSEEK: "deepseek/deepseek-chat",
  MISTRAL: "mistralai/mistral-small-24b-instruct-2501",
  LLAMA_VISION: "meta-llama/llama-3.2-11b-vision-instruct",
  FALLBACK: "openrouter/auto",
};

const OPENROUTER_CHAT_URL = "https://openrouter.ai/api/v1/chat/completions";
const OPENROUTER_MODELS_URL = "https://openrouter.ai/api/v1/models";
const GROQ_CHAT_URL = "https://api.groq.com/openai/v1/chat/completions";
const GROQ_MODELS_URL = "https://api.groq.com/openai/v1/models";

export interface DynamicModelItem {
  id: string;
  name: string;
  provider: 'openrouter' | 'groq';
  contextLength?: number;
  description?: string;
}

async function handleGetModels(openRouterKey?: string, groqKey?: string): Promise<Response> {
  const models: DynamicModelItem[] = [];

  // 1. Fetch OpenRouter models
  if (openRouterKey) {
    try {
      const orRes = await fetch(OPENROUTER_MODELS_URL, {
        headers: {
          Authorization: `Bearer ${openRouterKey}`,
          "HTTP-Referer": "https://socialpulse.app",
          "X-OpenRouter-Title": "SocialPulse Video Editor",
        },
      });
      if (orRes.ok) {
        const orData = await orRes.json();
        if (Array.isArray(orData.data)) {
          for (const m of orData.data) {
            if (m && m.id) {
              if (typeof m.id === 'string' && m.id.endsWith(':batch')) continue;
              // Heuristic to detect whether this model is part of a free tier.
              const isFree = Boolean(m.free === true || (m.pricing && /free/i.test(String(m.pricing))) || /free/i.test(String(m.id)) || /free/i.test(String(m.name)));
              models.push({
                id: m.id,
                name: m.name || m.id,
                provider: 'openrouter',
                contextLength: m.context_length,
                description: m.description,
                // @ts-ignore - ephemeral field for the UI to know which models are free
                free: isFree,
              } as any);
            }
          }
        }
      } else {
        console.warn("[ai-proxy] OpenRouter models fetch failed:", orRes.status);
      }
    } catch (e) {
      console.error("[ai-proxy] Error fetching OpenRouter models:", e);
    }
  }

  // 2. Fetch Groq models
  if (groqKey) {
    try {
      const groqRes = await fetch(GROQ_MODELS_URL, {
        headers: {
          Authorization: `Bearer ${groqKey}`,
        },
      });
      if (groqRes.ok) {
        const groqData = await groqRes.json();
        if (Array.isArray(groqData.data)) {
          for (const m of groqData.data) {
            if (m && m.id) {
              const isFree = Boolean(m.free === true || (m.pricing && /free/i.test(String(m.pricing))) || /free/i.test(String(m.id)) || /free/i.test(String(m.name)));
              models.push({
                id: `groq/${m.id}`,
                name: `Groq: ${m.id}`,
                provider: 'groq',
                contextLength: m.context_window,
                description: m.owned_by ? `Owned by ${m.owned_by}` : undefined,
                // @ts-ignore
                free: isFree,
              } as any);
            }
          }
        }
      } else {
        console.warn("[ai-proxy] Groq models fetch failed:", groqRes.status);
      }
    } catch (e) {
      console.error("[ai-proxy] Error fetching Groq models:", e);
    }
  }

  return new Response(JSON.stringify({ data: models, count: models.length }), {
    status: 200,
    headers: { ...corsHeaders, "Content-Type": "application/json" },
  });
}

serve(async (req: Request) => {
  // Handle CORS preflight
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  const url = new URL(req.url);
  const openRouterKey =
    req.headers.get("x-openrouter-key") ||
    Deno.env.get("OPENROUTER_API_KEY") ||
    Deno.env.get("VITE_OPENROUTER_API_KEY");
  const groqKey =
    Deno.env.get("GROQ_API_KEY") ||
    Deno.env.get("VITE_GROQ_API_KEY");

  // Support GET /models or query param action=models
  if (req.method === "GET" && (url.pathname.endsWith("/models") || url.searchParams.get("action") === "models")) {
    return handleGetModels(openRouterKey, groqKey);
  }

  if (req.method !== "POST") {
    return new Response(
      JSON.stringify({ error: "Method not allowed." }),
      { status: 405, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  }

  try {
    const body = await req.json();

    // Check if client requested action: "models" via POST
    if (body.action === "models") {
      return handleGetModels(openRouterKey, groqKey);
    }

    const {
      model = "AUTO",
      messages = [],
      tools,
      response_format,
      max_tokens = 4096,
      max_completion_tokens,
      temperature = 0.7,
      stream = false,
    } = body;

    // Safe debug logging: record token-related fields and counts, avoid logging message contents or API keys.
    try {
      const incomingMaxInput = (body as any).max_input_tokens ?? (body as any).maxInputTokens ?? undefined;
      const messagesCount = Array.isArray(messages) ? messages.length : typeof messages;
      const toolsCount = Array.isArray(tools) ? tools.length : typeof tools;
      console.debug('[ai-proxy] incoming payload summary', {
        model,
        max_tokens,
        max_completion_tokens,
        max_input_tokens: incomingMaxInput,
        temperature,
        stream,
        messagesCount,
        toolsCount,
      });
    } catch (e) {
      // Non-fatal - logging must never break the request handling
      console.warn('[ai-proxy] debug logging failed', e);
    }

    // If the client requested AUTO (or omitted model), attempt to auto-select
    // a suitable free model from the upstream providers. Otherwise normalize
    // model string to resolve capability aliases.
    let resolvedModel: string | undefined = undefined;
    const modelKey = typeof model === "string" ? model.trim() : '';
    if (!modelKey || modelKey.toUpperCase() === 'AUTO') {
      try {
        const listRes = await handleGetModels(openRouterKey, groqKey);
        if (listRes.ok) {
          const payload = await listRes.json();
          const available: DynamicModelItem[] = Array.isArray(payload.data) ? payload.data : [];
          // Prefer OpenRouter free models, then any OpenRouter, then Groq free, then any Groq
          const orFree = available.find((m) => m.provider === 'openrouter' && (m as any).free === true);
          const orAny = available.find((m) => m.provider === 'openrouter');
          const groqFree = available.find((m) => m.provider === 'groq' && (m as any).free === true);
          const groqAny = available.find((m) => m.provider === 'groq');
          const chosen = orFree || orAny || groqFree || groqAny;
          resolvedModel = chosen ? chosen.id : CAPABILITY_MODELS.FALLBACK;
        }
      } catch (e) {
        console.warn('[ai-proxy] auto model selection failed:', e);
      }
    }
    if (!resolvedModel) {
      const upperKey = typeof model === "string" ? model.trim().toUpperCase() : "";
      resolvedModel = CAPABILITY_MODELS[upperKey] || model;
    }

    // Determine target provider: Groq vs OpenRouter
    const isGroq = typeof resolvedModel === "string" && (
      resolvedModel.startsWith("groq/") ||
      resolvedModel.includes("llama-3.3-70b") ||
      resolvedModel.includes("llama-3.1-8b") ||
      resolvedModel.includes("whisper") ||
      resolvedModel.includes("orpheus") ||
      resolvedModel.includes("qwen3.6-27b")
    );

    const targetUrl = isGroq ? GROQ_CHAT_URL : OPENROUTER_CHAT_URL;
    const targetApiKey = isGroq ? (groqKey || openRouterKey) : openRouterKey;
    const rawTargetModel = typeof resolvedModel === "string"
      ? resolvedModel.replace(/^groq\//, "").replace(/:batch$/i, "")
      : resolvedModel;

    if (!targetApiKey) {
      return new Response(
        JSON.stringify({ error: `${isGroq ? 'GROQ_API_KEY' : 'OPENROUTER_API_KEY'} not configured in secrets.` }),
        { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // Normalize and prefer a canonical internal `maxOutputTokens` field.
    // Accept multiple incoming names that may be present in the wild.
    const incomingMaxOutput = (body as any).maxOutputTokens ?? (body as any).max_output_tokens
      ?? (typeof max_completion_tokens === 'number' && Number.isFinite(max_completion_tokens) ? Math.trunc(max_completion_tokens) : undefined)
      ?? (typeof max_tokens === 'number' && Number.isFinite(max_tokens) ? Math.trunc(max_tokens) : undefined);
    const maxOutputTokens = typeof incomingMaxOutput === 'number' && Number.isFinite(incomingMaxOutput)
      ? Math.trunc(incomingMaxOutput)
      : 4096;

    const payload: Record<string, unknown> = {
      model: rawTargetModel,
      messages,
      temperature,
      stream,
    };

    // Map the canonical `maxOutputTokens` to provider-specific fields:
    // - OpenRouter (and OpenAI-compatible): use `max_tokens` (or `max_completion_tokens` when appropriate)
    // - Groq: use `max_completion_tokens` (Groq compatibility)
    if (isGroq) {
      payload['max_completion_tokens'] = maxOutputTokens;
    } else {
      payload['max_tokens'] = maxOutputTokens;
    }

    if (tools && Array.isArray(tools) && tools.length > 0) {
      payload.tools = tools;
    }
    if (response_format && typeof response_format === "object") {
      payload.response_format = response_format;
    }

    // Dispatch completion request
    const dispatchRequest = async (targetEndpoint: string, key: string, targetModelId: string) => {
      const currentPayload = { ...payload, model: targetModelId };
      // Safe outgoing debug: redact message texts, but include counts and token fields.
      try {
        const outgoingSummary = {
          model: targetModelId,
          isGroq,
          messagesCount: Array.isArray(messages) ? messages.length : typeof messages,
          toolsCount: Array.isArray(tools) ? tools.length : typeof tools,
          tokenField: isGroq ? 'max_completion_tokens' : 'max_tokens',
          tokenValue: isGroq ? currentPayload['max_completion_tokens'] : currentPayload['max_tokens'],
          stream: !!stream,
        };
        console.debug('[ai-proxy] outgoing payload summary', outgoingSummary);
      } catch (e) {
        console.warn('[ai-proxy] outgoing summary logging failed', e);
      }
      const headers: Record<string, string> = {
        Authorization: `Bearer ${key}`,
        "Content-Type": "application/json",
      };
      if (!isGroq) {
        headers["HTTP-Referer"] = "https://socialpulse.app";
        headers["X-OpenRouter-Title"] = "SocialPulse Video Editor";
      }
      return fetch(targetEndpoint, {
        method: "POST",
        headers,
        body: JSON.stringify(currentPayload),
      });
    };

    let response = await dispatchRequest(targetUrl, targetApiKey, rawTargetModel);

    // Fallback if primary model fails with error status
    if (!response.ok && resolvedModel !== CAPABILITY_MODELS.FALLBACK && openRouterKey) {
      const errText = await response.text();
      console.warn(`[ai-proxy] Model '${resolvedModel}' returned HTTP ${response.status}: ${errText}. Attempting fallback model '${CAPABILITY_MODELS.FALLBACK}'...`);
      if (response.status === 402) {
        return new Response(
          JSON.stringify({
            error: {
              message: "OpenRouter account balance is exhausted. Please top up credits at https://openrouter.ai/settings/credits or select a free model (e.g. models tagged :free) in Settings -> Agent Model."
            }
          }),
          { status: 402, headers: { ...corsHeaders, "Content-Type": "application/json" } }
        );
      }
      response = await dispatchRequest(OPENROUTER_CHAT_URL, openRouterKey, CAPABILITY_MODELS.FALLBACK);
    }

    if (stream && response.ok) {
      return new Response(response.body, {
        status: response.status,
        headers: {
          ...corsHeaders,
          "Content-Type": "text/event-stream",
          "Cache-Control": "no-cache",
          "Connection": "keep-alive",
        },
      });
    }

    const responseData = await response.text();
    return new Response(responseData, {
      status: response.status,
      headers: {
        ...corsHeaders,
        "Content-Type": "application/json",
      },
    });
  } catch (error: any) {
    console.error("[ai-proxy] Exception:", error);
    return new Response(
      JSON.stringify({ error: error?.message || String(error) }),
      { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  }
});
