-- ============================================================
-- SOCIALPULSE ADMIN DASHBOARD — CORRECT RLS POLICIES
-- Based on actual database schema
-- ============================================================

-- STEP 1: DROP ALL POLICIES (from all migrations)
DROP POLICY IF EXISTS "Users can view their own profile" ON profiles;
DROP POLICY IF EXISTS "Users can update their own profile" ON profiles;
DROP POLICY IF EXISTS "Admin can view all profiles" ON profiles;
DROP POLICY IF EXISTS "Authenticated can read profiles" ON profiles;
DROP POLICY IF EXISTS "Admins can view subscriptions" ON subscriptions;
DROP POLICY IF EXISTS "Admins can manage subscriptions" ON subscriptions;
DROP POLICY IF EXISTS "Admin can read subscriptions" ON subscriptions;
DROP POLICY IF EXISTS "Authenticated can read subscriptions" ON subscriptions;
DROP POLICY IF EXISTS "Admins can view transactions" ON transactions;
DROP POLICY IF EXISTS "Admins can manage transactions" ON transactions;
DROP POLICY IF EXISTS "Admin can read transactions" ON transactions;
DROP POLICY IF EXISTS "Authenticated can read transactions" ON transactions;
DROP POLICY IF EXISTS "Admins can view render_jobs" ON render_jobs;
DROP POLICY IF EXISTS "Admins can view sessions" ON user_sessions;
DROP POLICY IF EXISTS "Authenticated can read user_sessions" ON user_sessions;
DROP POLICY IF EXISTS "Admins can view api_usage" ON api_usage;
DROP POLICY IF EXISTS "Authenticated can read api_usage" ON api_usage;
DROP POLICY IF EXISTS "Admins can view tickets" ON support_tickets;
DROP POLICY IF EXISTS "Admins can manage tickets" ON support_tickets;
DROP POLICY IF EXISTS "Authenticated can read support_tickets" ON support_tickets;
DROP POLICY IF EXISTS "Admins can manage plans" ON plans;
DROP POLICY IF EXISTS "Anyone can view plans" ON plans;
DROP POLICY IF EXISTS "Admins can manage coupons" ON coupons;
DROP POLICY IF EXISTS "Anyone can view active coupons" ON coupons;
DROP POLICY IF EXISTS "Admin users can view all" ON admin_users;
DROP POLICY IF EXISTS "Super admins can manage" ON admin_users;
DROP POLICY IF EXISTS "Users can view own transactions" ON transactions;
DROP POLICY IF EXISTS "Users can view own subscriptions" ON subscriptions;
DROP POLICY IF EXISTS "Users can view own tickets" ON support_tickets;
DROP POLICY IF EXISTS "Users can create tickets" ON support_tickets;

-- STEP 2: ENABLE RLS
ALTER TABLE profiles ENABLE ROW LEVEL SECURITY;
ALTER TABLE linked_accounts ENABLE ROW LEVEL SECURITY;
ALTER TABLE posts ENABLE ROW LEVEL SECURITY;
ALTER TABLE render_jobs ENABLE ROW LEVEL SECURITY;
ALTER TABLE subscriptions ENABLE ROW LEVEL SECURITY;
ALTER TABLE transactions ENABLE ROW LEVEL SECURITY;
ALTER TABLE user_sessions ENABLE ROW LEVEL SECURITY;
ALTER TABLE api_usage ENABLE ROW LEVEL SECURITY;
ALTER TABLE support_tickets ENABLE ROW LEVEL SECURITY;
ALTER TABLE plans ENABLE ROW LEVEL SECURITY;
ALTER TABLE coupons ENABLE ROW LEVEL SECURITY;
ALTER TABLE notifications ENABLE ROW LEVEL SECURITY;
ALTER TABLE inbox_items ENABLE ROW LEVEL SECURITY;
ALTER TABLE studio_projects ENABLE ROW LEVEL SECURITY;
ALTER TABLE ai_clips ENABLE ROW LEVEL SECURITY;

-- STEP 3: CREATE POLICIES

-- Profiles (has id column)
CREATE POLICY "Users can view own profile" ON profiles FOR SELECT TO anon, authenticated USING (id = auth.uid());
CREATE POLICY "Users can update own profile" ON profiles FOR UPDATE TO authenticated USING (id = auth.uid()) WITH CHECK (id = auth.uid());
CREATE POLICY "Admins can view all profiles" ON profiles FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));
CREATE POLICY "Admins can update all profiles" ON profiles FOR UPDATE TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Linked Accounts (has user_id)
CREATE POLICY "Users can view own linked_accounts" ON linked_accounts FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all linked_accounts" ON linked_accounts FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Posts (has user_id)
CREATE POLICY "Users can view own posts" ON posts FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all posts" ON posts FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Render Jobs (has user_id)
CREATE POLICY "Users can view own render_jobs" ON render_jobs FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all render_jobs" ON render_jobs FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Subscriptions (has user_id)
CREATE POLICY "Users can view own subscriptions" ON subscriptions FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all subscriptions" ON subscriptions FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));
CREATE POLICY "Admins can manage subscriptions" ON subscriptions FOR ALL TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Transactions (has user_id)
CREATE POLICY "Users can view own transactions" ON transactions FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all transactions" ON transactions FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- User Sessions (has user_id, admin only)
CREATE POLICY "Admins can view all user_sessions" ON user_sessions FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- API Usage (has user_id, admin only)
CREATE POLICY "Admins can view all api_usage" ON api_usage FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Support Tickets (has user_id)
CREATE POLICY "Users can view own tickets" ON support_tickets FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Users can create tickets" ON support_tickets FOR INSERT TO authenticated WITH CHECK (user_id = auth.uid());
CREATE POLICY "Admins can view all tickets" ON support_tickets FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));
CREATE POLICY "Admins can manage tickets" ON support_tickets FOR ALL TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Plans (no user_id, public read)
CREATE POLICY "Anyone can view plans" ON plans FOR SELECT TO authenticated, anon USING (true);
CREATE POLICY "Admins can manage plans" ON plans FOR ALL TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Coupons (no user_id)
CREATE POLICY "Anyone can view active coupons" ON coupons FOR SELECT TO authenticated, anon USING (is_active = true);
CREATE POLICY "Admins can manage coupons" ON coupons FOR ALL TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Notifications (has user_id)
CREATE POLICY "Users can view own notifications" ON notifications FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all notifications" ON notifications FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Inbox Items (has user_id)
CREATE POLICY "Users can view own inbox" ON inbox_items FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all inbox" ON inbox_items FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Studio Projects (has user_id)
CREATE POLICY "Users can view own projects" ON studio_projects FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Admins can view all projects" ON studio_projects FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- AI Clips (has project_id, not user_id — join through studio_projects)
CREATE POLICY "Admins can view all ai_clips" ON ai_clips FOR SELECT TO authenticated USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));
