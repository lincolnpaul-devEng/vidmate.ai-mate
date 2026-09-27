-- ============================================================
-- SOCIALPULSE ADMIN MANAGEMENT TABLES
-- Run this in Supabase SQL Editor
-- ============================================================

-- 1. ADMIN USERS (who can access the Tedora admin dashboard)
CREATE TABLE IF NOT EXISTS admin_users (
  id UUID PRIMARY KEY REFERENCES auth.users(id) ON DELETE CASCADE,
  role TEXT DEFAULT 'admin' CHECK (role IN ('super_admin', 'admin', 'support', 'viewer')),
  permissions JSONB DEFAULT '{"users": true, "billing": true, "analytics": true, "renders": true, "support": true, "system": true}'::jsonb,
  created_at TIMESTAMPTZ DEFAULT now()
);

-- 2. SUBSCRIPTION PLANS
CREATE TABLE IF NOT EXISTS plans (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  name TEXT NOT NULL,
  slug TEXT UNIQUE NOT NULL,
  price_kes INTEGER NOT NULL DEFAULT 0,
  price_usd DECIMAL(10,2) DEFAULT 0,
  interval TEXT DEFAULT 'month' CHECK (interval IN ('month', 'year')),
  features JSONB DEFAULT '{}'::jsonb,
  credits_included INTEGER DEFAULT 0,
  is_active BOOLEAN DEFAULT true,
  created_at TIMESTAMPTZ DEFAULT now()
);

-- 3. USER SUBSCRIPTIONS
CREATE TABLE IF NOT EXISTS subscriptions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE,
  plan_id UUID REFERENCES plans(id) ON DELETE SET NULL,
  status TEXT DEFAULT 'active' CHECK (status IN ('active', 'cancelled', 'expired', 'trialing', 'past_due')),
  started_at TIMESTAMPTZ DEFAULT now(),
  expires_at TIMESTAMPTZ,
  payment_provider TEXT,
  payment_id TEXT,
  metadata JSONB DEFAULT '{}'::jsonb,
  created_at TIMESTAMPTZ DEFAULT now(),
  updated_at TIMESTAMPTZ DEFAULT now()
);

-- 4. TRANSACTIONS / PAYMENTS
CREATE TABLE IF NOT EXISTS transactions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES profiles(id) ON DELETE SET NULL,
  amount DECIMAL(10,2) NOT NULL,
  currency TEXT DEFAULT 'KES',
  type TEXT CHECK (type IN ('payment', 'refund', 'credit_purchase', 'credit_usage', 'payout')),
  status TEXT DEFAULT 'completed' CHECK (status IN ('pending', 'completed', 'failed', 'refunded')),
  description TEXT,
  metadata JSONB DEFAULT '{}'::jsonb,
  created_at TIMESTAMPTZ DEFAULT now()
);

-- 5. USER SESSIONS (for tracking time spent)
CREATE TABLE IF NOT EXISTS user_sessions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE,
  started_at TIMESTAMPTZ DEFAULT now(),
  ended_at TIMESTAMPTZ,
  duration_seconds INTEGER DEFAULT 0,
  ip_address TEXT,
  user_agent TEXT,
  page_visited TEXT
);

-- 6. SUPPORT TICKETS
CREATE TABLE IF NOT EXISTS support_tickets (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES profiles(id) ON DELETE SET NULL,
  subject TEXT NOT NULL,
  description TEXT,
  status TEXT DEFAULT 'open' CHECK (status IN ('open', 'in_progress', 'resolved', 'closed')),
  priority TEXT DEFAULT 'medium' CHECK (priority IN ('low', 'medium', 'high', 'urgent')),
  assigned_to UUID REFERENCES admin_users(id) ON DELETE SET NULL,
  created_at TIMESTAMPTZ DEFAULT now(),
  updated_at TIMESTAMPTZ DEFAULT now()
);

-- 7. API USAGE TRACKING
CREATE TABLE IF NOT EXISTS api_usage (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES profiles(id) ON DELETE SET NULL,
  service TEXT NOT NULL, -- 'openrouter', 'elevenlabs', 'videodb', 'youtube'
  endpoint TEXT,
  tokens_used INTEGER DEFAULT 0,
  cost_usd DECIMAL(10,6) DEFAULT 0,
  status TEXT DEFAULT 'success',
  created_at TIMESTAMPTZ DEFAULT now()
);

-- 8. RENDER JOBS (for tracking video rendering)
CREATE TABLE IF NOT EXISTS render_jobs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES profiles(id) ON DELETE SET NULL,
  project_id UUID,
  type TEXT DEFAULT 'video', -- 'video', 'reframe', 'clip'
  status TEXT DEFAULT 'queued' CHECK (status IN ('queued', 'processing', 'completed', 'failed')),
  input_duration INTEGER, -- seconds
  output_duration INTEGER, -- seconds
  credits_used INTEGER DEFAULT 0,
  storage_path TEXT,
  error_message TEXT,
  started_at TIMESTAMPTZ,
  completed_at TIMESTAMPTZ,
  created_at TIMESTAMPTZ DEFAULT now()
);

-- 9. COUPONS / PROMO CODES
CREATE TABLE IF NOT EXISTS coupons (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  code TEXT UNIQUE NOT NULL,
  discount_type TEXT CHECK (discount_type IN ('percentage', 'fixed_kes', 'fixed_usd')),
  discount_value DECIMAL(10,2) NOT NULL,
  max_uses INTEGER DEFAULT NULL, -- NULL = unlimited
  uses_count INTEGER DEFAULT 0,
  valid_from TIMESTAMPTZ DEFAULT now(),
  expires_at TIMESTAMPTZ,
  is_active BOOLEAN DEFAULT true,
  created_at TIMESTAMPTZ DEFAULT now()
);

-- 10. ADMIN ACTIVITY LOG
CREATE TABLE IF NOT EXISTS admin_activity_log (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  admin_id UUID REFERENCES admin_users(id) ON DELETE SET NULL,
  action TEXT NOT NULL,
  target_type TEXT, -- 'user', 'subscription', 'render', etc.
  target_id UUID,
  details JSONB DEFAULT '{}'::jsonb,
  ip_address TEXT,
  created_at TIMESTAMPTZ DEFAULT now()
);

-- ============================================================
-- INSERT DEFAULT PLANS (SocialPulse pricing)
-- ============================================================
INSERT INTO plans (name, slug, price_kes, price_usd, credits_included, features) VALUES
  ('Free Plan', 'free', 0, 0, 100, '{"platforms": 1, "posts_per_month": 10, "analytics": "basic"}'::jsonb),
  ('Basic Plan', 'basic', 450, 4, 500, '{"platforms": 3, "posts_per_month": "unlimited", "analytics": "growth", "email_support": true}'::jsonb),
  ('Pro Plan', 'pro', 1850, 14, 2000, '{"platforms": "all", "posts_per_month": "unlimited", "analytics": "advanced", "ai_captions": true, "priority_support": true}'::jsonb),
  ('Professional Plan', 'professional', 4500, 35, 10000, '{"platforms": "all", "posts_per_month": "unlimited", "analytics": "advanced", "ai_captions": true, "api_access": true, "white_label": true, "team_collaboration": true, "dedicated_support": true}'::jsonb)
ON CONFLICT (slug) DO NOTHING;

-- ============================================================
-- ROW LEVEL SECURITY
-- ============================================================

ALTER TABLE admin_users ENABLE ROW LEVEL SECURITY;
ALTER TABLE plans ENABLE ROW LEVEL SECURITY;
ALTER TABLE subscriptions ENABLE ROW LEVEL SECURITY;
ALTER TABLE transactions ENABLE ROW LEVEL SECURITY;
ALTER TABLE user_sessions ENABLE ROW LEVEL SECURITY;
ALTER TABLE support_tickets ENABLE ROW LEVEL SECURITY;
ALTER TABLE api_usage ENABLE ROW LEVEL SECURITY;
ALTER TABLE render_jobs ENABLE ROW LEVEL SECURITY;
ALTER TABLE coupons ENABLE ROW LEVEL SECURITY;
ALTER TABLE admin_activity_log ENABLE ROW LEVEL SECURITY;

-- Admin users policies
CREATE POLICY "Admin users can view all" ON admin_users FOR SELECT TO authenticated USING (true);
CREATE POLICY "Super admins can manage" ON admin_users FOR ALL TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid() AND role = 'super_admin')
);

-- Plans policies
CREATE POLICY "Anyone can view plans" ON plans FOR SELECT TO authenticated, anon USING (true);
CREATE POLICY "Admins can manage plans" ON plans FOR ALL TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- Subscriptions policies
CREATE POLICY "Admins can view subscriptions" ON subscriptions FOR SELECT TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);
CREATE POLICY "Admins can manage subscriptions" ON subscriptions FOR ALL TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- Transactions policies
CREATE POLICY "Admins can view transactions" ON transactions FOR SELECT TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);
CREATE POLICY "Admins can manage transactions" ON transactions FOR ALL TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- User sessions policies
CREATE POLICY "Admins can view sessions" ON user_sessions FOR SELECT TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- Support tickets policies
CREATE POLICY "Admins can view tickets" ON support_tickets FOR SELECT TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);
CREATE POLICY "Admins can manage tickets" ON support_tickets FOR ALL TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- API usage policies
CREATE POLICY "Admins can view api_usage" ON api_usage FOR SELECT TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- Render jobs policies
CREATE POLICY "Admins can view render_jobs" ON render_jobs FOR SELECT TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- Coupons policies
CREATE POLICY "Anyone can view active coupons" ON coupons FOR SELECT TO authenticated, anon USING (is_active = true);
CREATE POLICY "Admins can manage coupons" ON coupons FOR ALL TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);

-- Admin activity log policies
CREATE POLICY "Admins can view activity log" ON admin_activity_log FOR SELECT TO authenticated USING (
  EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid())
);
-- ============================================================
-- VERIFY (run separately after migration)
-- ============================================================
-- SELECT 'admin_users' as table_name, count(*) as rows FROM admin_users
-- UNION ALL SELECT 'plans', count(*) FROM plans
-- UNION ALL SELECT 'subscriptions', count(*) FROM subscriptions
-- UNION ALL SELECT 'transactions', count(*) FROM transactions
-- UNION ALL SELECT 'user_sessions', count(*) FROM user_sessions
-- UNION ALL SELECT 'support_tickets', count(*) FROM support_tickets
-- UNION ALL SELECT 'api_usage', count(*) FROM api_usage
-- UNION ALL SELECT 'render_jobs', count(*) FROM render_jobs
-- UNION ALL SELECT 'coupons', count(*) FROM coupons
-- UNION ALL SELECT 'admin_activity_log', count(*) FROM admin_activity_log;
