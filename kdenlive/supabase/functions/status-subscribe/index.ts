import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.39.0";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

const RESEND_API_KEY = Deno.env.get("RESEND_API_KEY") || "";
const RESEND_FROM_EMAIL = Deno.env.get("RESEND_FROM_EMAIL") || "Velo System Status <noreply@tedoraltd.com>";
const SUPABASE_URL = Deno.env.get("SUPABASE_URL") || "";
const SUPABASE_SERVICE_ROLE_KEY = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") || "";

serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders });
  }

  try {
    const { email, categories = ["all"] } = await req.json();
    if (!email || typeof email !== "string" || !email.includes("@") || !email.includes(".")) {
      return new Response(
        JSON.stringify({ error: "Please provide a valid email address." }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const cleanEmail = email.trim().toLowerCase();

    if (SUPABASE_URL && SUPABASE_SERVICE_ROLE_KEY) {
      const supabase = createClient(SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY);
      await supabase.from("status_subscribers").upsert(
        {
          email: cleanEmail,
          status: "active",
          categories,
          updated_at: new Date().toISOString(),
        },
        { onConflict: "email" }
      );
    }

    let emailSent = false;
    if (RESEND_API_KEY) {
      try {
        const html = `<!DOCTYPE html><html><head><meta charset="utf-8"><title>Velo Status Subscription Confirmed</title></head><body style="margin:0;padding:24px;background-color:#0d1117;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;color:#c9d1d9;"><div style="max-width:540px;margin:0 auto;background-color:#161b22;border:1px solid #30363d;border-radius:12px;padding:32px;"><h2 style="color:#ffffff;margin:0 0 16px 0;font-size:18px;"><span style="color:#238636;">&#9679;</span> Velo System Status</h2><h1 style="color:#ffffff;font-size:22px;margin:0 0 12px 0;">Subscription Confirmed</h1><p style="color:#8b949e;line-height:1.6;font-size:14px;margin:0 0 20px 0;">You are now subscribed to automated incident alerts and maintenance updates for <strong>${cleanEmail}</strong>.</p><div style="background:#0d1117;border:1px solid #30363d;padding:16px;border-radius:8px;margin-bottom:24px;"><div style="font-size:12px;font-weight:bold;color:#58a6ff;text-transform:uppercase;margin-bottom:8px;">Monitored Subsystems:</div><div style="font-size:13px;color:#c9d1d9;line-height:1.8;">&#10003; Timeline Compositing Engine (60 FPS)<br/>&#10003; AI Agent Orchestrator &amp; LLM Proxies<br/>&#10003; Whisper ASR &amp; Vision Segmentation<br/>&#10003; Cloud Export &amp; Video Rendering Farms</div></div><div style="text-align:center;"><a href="https://velo.tedoraltd.com/status" style="display:inline-block;background-color:#238636;color:#ffffff;text-decoration:none;padding:12px 24px;border-radius:6px;font-weight:bold;font-size:14px;">View Live Status Dashboard &rarr;</a></div></div></body></html>`;

        const res = await fetch("https://api.resend.com/emails", {
          method: "POST",
          headers: {
            Authorization: `Bearer ${RESEND_API_KEY}`,
            "Content-Type": "application/json",
          },
          body: JSON.stringify({
            from: RESEND_FROM_EMAIL,
            to: cleanEmail,
            subject: "Confirmed: Velo System Status Notifications",
            html,
          }),
        });
        emailSent = res.ok;
      } catch (err) {
        console.error("[status-subscribe] Resend dispatch error:", err);
      }
    }

    return new Response(
      JSON.stringify({
        ok: true,
        email: cleanEmail,
        emailSent,
        message: "Successfully subscribed to Velo status updates.",
      }),
      { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  } catch (err) {
    return new Response(
      JSON.stringify({ error: err instanceof Error ? err.message : "Internal Error" }),
      { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  }
});