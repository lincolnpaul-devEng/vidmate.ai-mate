-- ==========================================================
-- Migration: Velo Status Subscribers & Incident Notifications
-- ==========================================================

-- 1. Create status_subscribers table
CREATE TABLE IF NOT EXISTS public.status_subscribers (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    email TEXT UNIQUE NOT NULL,
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'unsubscribed', 'bounced')),
    categories TEXT[] DEFAULT ARRAY['all'],
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. Create status_incidents table
CREATE TABLE IF NOT EXISTS public.status_incidents (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    title TEXT NOT NULL,
    service_id TEXT NOT NULL,
    severity TEXT NOT NULL DEFAULT 'minor' CHECK (severity IN ('info', 'minor', 'major', 'critical')),
    status TEXT NOT NULL DEFAULT 'investigating' CHECK (status IN ('investigating', 'identified', 'monitoring', 'resolved')),
    description TEXT NOT NULL,
    affected_components TEXT[] DEFAULT ARRAY[]::TEXT[],
    resolved_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 3. Create status_incident_broadcasts table
CREATE TABLE IF NOT EXISTS public.status_incident_broadcasts (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    incident_id UUID REFERENCES public.status_incidents(id) ON DELETE CASCADE,
    recipient_email TEXT NOT NULL,
    resend_email_id TEXT,
    sent_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 4. Create telemetry_heartbeats table for anonymous desktop fleet telemetry
CREATE TABLE IF NOT EXISTS public.telemetry_heartbeats (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    app_version TEXT NOT NULL,
    platform TEXT NOT NULL,
    arch TEXT,
    uptime_sec INTEGER NOT NULL DEFAULT 0,
    llm_latency_ms INTEGER,
    render_fps NUMERIC(5, 2) DEFAULT 60.0,
    export_success_rate NUMERIC(4, 2) DEFAULT 1.0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 5. Create machine_registry table (Cursor IDE-Style Anti-Abuse)
CREATE TABLE IF NOT EXISTS public.machine_registry (
    device_id TEXT PRIMARY KEY,
    machine_id TEXT,
    mac_machine_id TEXT,
    platform TEXT,
    arch TEXT,
    bound_emails TEXT[] DEFAULT ARRAY[]::TEXT[],
    bound_emails_count INTEGER DEFAULT 0,
    is_blocked BOOLEAN DEFAULT false,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 6. Create user_device_sessions table (Single Active Session Enforcement)
CREATE TABLE IF NOT EXISTS public.user_device_sessions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    email TEXT NOT NULL,
    device_id TEXT NOT NULL,
    platform TEXT,
    arch TEXT,
    app_version TEXT,
    is_active BOOLEAN NOT NULL DEFAULT true,
    revoked_reason TEXT,
    last_heartbeat_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (email, device_id)
);

-- 7. Enable Row Level Security (RLS)
ALTER TABLE public.status_subscribers ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.status_incidents ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.status_incident_broadcasts ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.telemetry_heartbeats ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.machine_registry ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.user_device_sessions ENABLE ROW LEVEL SECURITY;

-- 8. Policies
DROP POLICY IF EXISTS "Allow public insert into status_subscribers" ON public.status_subscribers;
CREATE POLICY "Allow public insert into status_subscribers"
ON public.status_subscribers
FOR INSERT
WITH CHECK (true);

DROP POLICY IF EXISTS "Allow public read status_incidents" ON public.status_incidents;
CREATE POLICY "Allow public read status_incidents"
ON public.status_incidents
FOR SELECT
USING (true);

DROP POLICY IF EXISTS "Allow public insert into telemetry_heartbeats" ON public.telemetry_heartbeats;
CREATE POLICY "Allow public insert into telemetry_heartbeats"
ON public.telemetry_heartbeats
FOR INSERT
WITH CHECK (true);

DROP POLICY IF EXISTS "Allow public access user_device_sessions" ON public.user_device_sessions;
CREATE POLICY "Allow public access user_device_sessions"
ON public.user_device_sessions
FOR ALL
USING (true)
WITH CHECK (true);

DROP POLICY IF EXISTS "Allow public access machine_registry" ON public.machine_registry;
CREATE POLICY "Allow public access machine_registry"
ON public.machine_registry
FOR ALL
USING (true)
WITH CHECK (true);

-- 9. Indexes
CREATE INDEX IF NOT EXISTS idx_status_subscribers_email ON public.status_subscribers(email);
CREATE INDEX IF NOT EXISTS idx_status_incidents_created_at ON public.status_incidents(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_telemetry_heartbeats_created_at ON public.telemetry_heartbeats(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_user_device_sessions_email ON public.user_device_sessions(email);
CREATE INDEX IF NOT EXISTS idx_user_device_sessions_device_id ON public.user_device_sessions(device_id);
CREATE INDEX IF NOT EXISTS idx_machine_registry_device_id ON public.machine_registry(device_id);