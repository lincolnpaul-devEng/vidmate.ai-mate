import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.39.0";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

const SUPABASE_URL = Deno.env.get("SUPABASE_URL") || "";
const SUPABASE_SERVICE_ROLE_KEY = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") || "";
const MAX_ACCOUNTS_PER_DEVICE = 2; // Cursor-style machine limit to prevent trial cycling

serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders });
  }

  try {
    const body = await req.json();
    const { action, email, deviceId, machineId, macMachineId, platform, arch, appVersion } = body;

    if (!email || !deviceId) {
      return new Response(
        JSON.stringify({ error: "email and deviceId are required." }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const cleanEmail = String(email).trim().toLowerCase();
    const cleanDeviceId = String(deviceId).trim();

    if (!SUPABASE_URL || !SUPABASE_SERVICE_ROLE_KEY) {
      return new Response(
        JSON.stringify({ ok: true, active: true }),
        { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const supabase = createClient(SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY);

    // 1. ACTION: BIND OR VALIDATE LOGIN
    if (action === "bind_or_validate") {
      const { data: machineRecord } = await supabase
        .from("machine_registry")
        .select("*")
        .eq("device_id", cleanDeviceId)
        .maybeSingle();

      if (machineRecord) {
        const boundEmails = machineRecord.bound_emails || [];
        if (!boundEmails.includes(cleanEmail)) {
          if (boundEmails.length >= MAX_ACCOUNTS_PER_DEVICE) {
            return new Response(
              JSON.stringify({
                ok: false,
                error: `Device Account Limit Exceeded: This machine has already been associated with ${MAX_ACCOUNTS_PER_DEVICE} accounts. Please sign in with your primary account to continue.`,
              }),
              { status: 403, headers: { ...corsHeaders, "Content-Type": "application/json" } }
            );
          }
          await supabase
            .from("machine_registry")
            .update({
              bound_emails: [...boundEmails, cleanEmail],
              bound_emails_count: boundEmails.length + 1,
              updated_at: new Date().toISOString(),
            })
            .eq("device_id", cleanDeviceId);
        }
      } else {
        await supabase.from("machine_registry").insert({
          device_id: cleanDeviceId,
          machine_id: machineId ? String(machineId) : null,
          mac_machine_id: macMachineId ? String(macMachineId) : null,
          platform: String(platform || ""),
          arch: String(arch || ""),
          bound_emails: [cleanEmail],
          bound_emails_count: 1,
        });
      }

      // Single Active Desktop Session Enforcement: Deactivate previous sessions
      await supabase
        .from("user_device_sessions")
        .update({ is_active: false, revoked_reason: "CONCURRENT_SESSION_TRANSFERRED", updated_at: new Date().toISOString() })
        .eq("email", cleanEmail)
        .neq("device_id", cleanDeviceId);

      // Register or reactivate current device session
      await supabase
        .from("user_device_sessions")
        .upsert(
          {
            email: cleanEmail,
            device_id: cleanDeviceId,
            platform: String(platform || ""),
            arch: String(arch || ""),
            app_version: String(appVersion || ""),
            is_active: true,
            last_heartbeat_at: new Date().toISOString(),
            revoked_reason: null,
            updated_at: new Date().toISOString(),
          },
          { onConflict: "email,device_id" }
        );

      return new Response(
        JSON.stringify({ ok: true, active: true, deviceId: cleanDeviceId, message: "Device session activated." }),
        { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // 2. ACTION: HEARTBEAT LEASE PROBE
    if (action === "heartbeat") {
      const { data: session } = await supabase
        .from("user_device_sessions")
        .select("is_active, revoked_reason")
        .eq("email", cleanEmail)
        .eq("device_id", cleanDeviceId)
        .maybeSingle();

      if (session && session.is_active === false) {
        return new Response(
          JSON.stringify({
            active: false,
            reason: session.revoked_reason === "CONCURRENT_SESSION_TRANSFERRED"
              ? "Your account was logged in on another device. Only 1 active desktop session is allowed on this plan."
              : "Your device session has been revoked.",
          }),
          { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } }
        );
      }

      await supabase
        .from("user_device_sessions")
        .update({ last_heartbeat_at: new Date().toISOString(), is_active: true })
        .eq("email", cleanEmail)
        .eq("device_id", cleanDeviceId);

      return new Response(
        JSON.stringify({ active: true }),
        { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // 3. ACTION: REVOKE OTHERS
    if (action === "revoke_others") {
      await supabase
        .from("user_device_sessions")
        .update({ is_active: false, revoked_reason: "MANUALLY_REVOKED", updated_at: new Date().toISOString() })
        .eq("email", cleanEmail)
        .neq("device_id", cleanDeviceId);

      return new Response(
        JSON.stringify({ ok: true, message: "All other device sessions revoked." }),
        { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    return new Response(
      JSON.stringify({ error: `Unknown action: ${action}` }),
      { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  } catch (err) {
    return new Response(
      JSON.stringify({ error: err instanceof Error ? err.message : "Internal Error" }),
      { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  }
});