-- ============================================================
-- SEED DEMO DATA FOR SOCIALPULSE ADMIN DASHBOARD
-- Run this in Supabase SQL Editor to populate demo data
-- ============================================================

-- 1. INSERT DEMO USERS (profiles)
INSERT INTO profiles (id, name, email, avatar_url, bio, is_premium, credits, subscription_tier, total_posts, total_reach, streak, created_at) VALUES
  ('11111111-1111-1111-1111-111111111111', 'Alice Creator', 'alice@example.com', NULL, 'Content creator focused on tech reviews', true, 1850, 'Pro Plan', 45, 12500, 12, now() - interval '60 days'),
  ('22222222-2222-2222-2222-222222222222', 'Bob Influencer', 'bob@example.com', NULL, 'Lifestyle and travel content', true, 4200, 'Professional Plan', 120, 87000, 45, now() - interval '90 days'),
  ('33333333-3333-3333-3333-333333333333', 'Carol Student', 'carol@university.ac.ke', NULL, 'Student learning video editing', false, 45, 'Free Plan', 3, 150, 2, now() - interval '14 days'),
  ('44444444-4444-4444-4444-444444444444', 'David Agency', 'david@agency.com', NULL, 'Running a small digital agency', true, 3200, 'Professional Plan', 89, 45000, 30, now() - interval '120 days'),
  ('55555555-5555-5555-5555-555555555555', 'Eva Freelancer', 'eva@example.com', NULL, 'Freelance video editor', true, 780, 'Basic Plan', 28, 8500, 18, now() - interval '45 days'),
  ('66666666-6666-6666-6666-666666666666', 'Frank Startup', 'frank@startup.io', NULL, 'Building a startup brand', true, 1450, 'Pro Plan', 52, 22000, 25, now() - interval '75 days'),
  ('77777777-7777-7777-7777-777777777777', 'Grace Blogger', 'grace@blog.com', NULL, 'Food and recipe blogger', false, 120, 'Free Plan', 8, 420, 5, now() - interval '21 days'),
  ('88888888-8888-8888-8888-888888888888', 'Henry Musician', 'henry@music.com', NULL, 'Music producer sharing beats', true, 920, 'Basic Plan', 35, 15000, 22, now() - interval '55 days'),
  ('99999999-9999-9999-9999-999999999999', 'Ivy Educator', 'ivy@school.ac.ke', NULL, 'Teacher creating educational content', true, 1680, 'Pro Plan', 67, 31000, 38, now() - interval '100 days'),
  ('aaaaaaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa', 'Jack Gamer', 'jack@gaming.com', NULL, 'Gaming highlights and streams', false, 80, 'Free Plan', 5, 280, 3, now() - interval '7 days')
ON CONFLICT (id) DO NOTHING;

-- 2. INSERT DEMO SUBSCRIPTIONS
INSERT INTO subscriptions (user_id, plan_id, status, started_at, expires_at) VALUES
  ('11111111-1111-1111-1111-111111111111', (SELECT id FROM plans WHERE slug = 'pro'), 'active', now() - interval '60 days', now() + interval '300 days'),
  ('22222222-2222-2222-2222-222222222222', (SELECT id FROM plans WHERE slug = 'professional'), 'active', now() - interval '90 days', now() + interval '275 days'),
  ('44444444-4444-4444-4444-444444444444', (SELECT id FROM plans WHERE slug = 'professional'), 'active', now() - interval '120 days', now() + interval '245 days'),
  ('55555555-5555-5555-5555-555555555555', (SELECT id FROM plans WHERE slug = 'basic'), 'active', now() - interval '45 days', now() + interval '320 days'),
  ('66666666-6666-6666-6666-666666666666', (SELECT id FROM plans WHERE slug = 'pro'), 'active', now() - interval '75 days', now() + interval '290 days'),
  ('88888888-8888-8888-8888-888888888888', (SELECT id FROM plans WHERE slug = 'basic'), 'active', now() - interval '55 days', now() + interval '310 days'),
  ('99999999-9999-9999-9999-999999999999', (SELECT id FROM plans WHERE slug = 'pro'), 'active', now() - interval '100 days', now() + interval '265 days')
ON CONFLICT DO NOTHING;

-- 3. INSERT DEMO TRANSACTIONS
INSERT INTO transactions (user_id, amount, currency, type, status, description, created_at) VALUES
  ('11111111-1111-1111-1111-111111111111', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '60 days'),
  ('11111111-1111-1111-1111-111111111111', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '30 days'),
  ('22222222-2222-2222-2222-222222222222', 4500, 'KES', 'payment', 'completed', 'Professional Plan - Monthly', now() - interval '90 days'),
  ('22222222-2222-2222-2222-222222222222', 4500, 'KES', 'payment', 'completed', 'Professional Plan - Monthly', now() - interval '60 days'),
  ('22222222-2222-2222-2222-222222222222', 4500, 'KES', 'payment', 'completed', 'Professional Plan - Monthly', now() - interval '30 days'),
  ('44444444-4444-4444-4444-444444444444', 4500, 'KES', 'payment', 'completed', 'Professional Plan - Monthly', now() - interval '120 days'),
  ('44444444-4444-4444-4444-444444444444', 4500, 'KES', 'payment', 'completed', 'Professional Plan - Monthly', now() - interval '90 days'),
  ('44444444-4444-4444-4444-444444444444', 4500, 'KES', 'payment', 'completed', 'Professional Plan - Monthly', now() - interval '60 days'),
  ('44444444-4444-4444-4444-444444444444', 4500, 'KES', 'payment', 'completed', 'Professional Plan - Monthly', now() - interval '30 days'),
  ('55555555-5555-5555-5555-555555555555', 450, 'KES', 'payment', 'completed', 'Basic Plan - Monthly', now() - interval '45 days'),
  ('55555555-5555-5555-5555-555555555555', 450, 'KES', 'payment', 'completed', 'Basic Plan - Monthly', now() - interval '15 days'),
  ('66666666-6666-6666-6666-666666666666', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '75 days'),
  ('66666666-6666-6666-6666-666666666666', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '45 days'),
  ('66666666-6666-6666-6666-666666666666', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '15 days'),
  ('88888888-8888-8888-8888-888888888888', 450, 'KES', 'payment', 'completed', 'Basic Plan - Monthly', now() - interval '55 days'),
  ('88888888-8888-8888-8888-888888888888', 450, 'KES', 'payment', 'completed', 'Basic Plan - Monthly', now() - interval '25 days'),
  ('99999999-9999-9999-9999-999999999999', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '100 days'),
  ('99999999-9999-9999-9999-999999999999', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '70 days'),
  ('99999999-9999-9999-9999-999999999999', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '40 days'),
  ('99999999-9999-9999-9999-999999999999', 1850, 'KES', 'payment', 'completed', 'Pro Plan - Monthly', now() - interval '10 days');

-- 4. INSERT DEMO RENDER JOBS
INSERT INTO render_jobs (user_id, type, status, input_duration, output_duration, credits_used, created_at) VALUES
  ('11111111-1111-1111-1111-111111111111', 'video', 'completed', 180, 60, 50, now() - interval '55 days'),
  ('11111111-1111-1111-1111-111111111111', 'clip', 'completed', 60, 30, 25, now() - interval '50 days'),
  ('22222222-2222-2222-2222-222222222222', 'video', 'completed', 300, 90, 75, now() - interval '85 days'),
  ('22222222-2222-2222-2222-222222222222', 'reframe', 'completed', 90, 90, 40, now() - interval '80 days'),
  ('22222222-2222-2222-2222-222222222222', 'clip', 'completed', 120, 45, 35, now() - interval '25 days'),
  ('44444444-4444-4444-4444-444444444444', 'video', 'completed', 240, 60, 60, now() - interval '115 days'),
  ('44444444-4444-4444-4444-444444444444', 'video', 'failed', 180, NULL, 10, now() - interval '100 days'),
  ('55555555-5555-5555-5555-555555555555', 'clip', 'completed', 90, 30, 20, now() - interval '40 days'),
  ('66666666-6666-6666-6666-666666666666', 'video', 'completed', 200, 75, 55, now() - interval '70 days'),
  ('66666666-6666-6666-6666-666666666666', 'reframe', 'completed', 75, 75, 35, now() - interval '20 days'),
  ('88888888-8888-8888-8888-888888888888', 'clip', 'completed', 60, 25, 18, now() - interval '50 days'),
  ('99999999-9999-9999-9999-999999999999', 'video', 'completed', 150, 50, 45, now() - interval '95 days'),
  ('99999999-9999-9999-9999-999999999999', 'clip', 'processing', 45, NULL, 0, now() - interval '1 day'),
  ('33333333-3333-3333-3333-333333333333', 'clip', 'queued', 30, NULL, 0, now() - interval '1 hour');

-- 5. INSERT DEMO USER SESSIONS
INSERT INTO user_sessions (user_id, started_at, ended_at, duration_seconds, page_visited) VALUES
  ('11111111-1111-1111-1111-111111111111', now() - interval '1 day', now() - interval '1 day' + interval '45 minutes', 2700, '/dashboard/editor'),
  ('22222222-2222-2222-2222-222222222222', now() - interval '2 hours', now() - interval '2 hours' + interval '30 minutes', 1800, '/dashboard/history'),
  ('44444444-4444-4444-4444-444444444444', now() - interval '3 hours', now() - interval '3 hours' + interval '60 minutes', 3600, '/dashboard/analytics'),
  ('55555555-5555-5555-5555-555555555555', now() - interval '5 hours', now() - interval '5 hours' + interval '20 minutes', 1200, '/dashboard/scheduler'),
  ('66666666-6666-6666-6666-666666666666', now() - interval '30 minutes', now() - interval '30 minutes' + interval '15 minutes', 900, '/dashboard/editor'),
  ('99999999-9999-9999-9999-999999999999', now() - interval '1 day', now() - interval '1 day' + interval '50 minutes', 3000, '/dashboard/editor'),
  ('11111111-1111-1111-1111-111111111111', now() - interval '2 days', now() - interval '2 days' + interval '35 minutes', 2100, '/dashboard/history'),
  ('22222222-2222-2222-2222-222222222222', now() - interval '12 hours', now() - interval '12 hours' + interval '25 minutes', 1500, '/dashboard/analytics');

-- 6. INSERT DEMO API USAGE
INSERT INTO api_usage (user_id, service, endpoint, tokens_used, cost_usd, created_at) VALUES
  ('11111111-1111-1111-1111-111111111111', 'openrouter', '/api/v1/chat/completions', 15000, 0.015, now() - interval '55 days'),
  ('11111111-1111-1111-1111-111111111111', 'elevenlabs', '/v1/text-to-speech', 5000, 0.025, now() - interval '50 days'),
  ('22222222-2222-2222-2222-222222222222', 'openrouter', '/api/v1/chat/completions', 25000, 0.025, now() - interval '85 days'),
  ('22222222-2222-2222-2222-222222222222', 'videodb', '/api/v1/search', 0, 0.010, now() - interval '80 days'),
  ('44444444-4444-4444-4444-444444444444', 'openrouter', '/api/v1/chat/completions', 30000, 0.030, now() - interval '115 days'),
  ('44444444-4444-4444-4444-444444444444', 'elevenlabs', '/v1/text-to-speech', 8000, 0.040, now() - interval '110 days'),
  ('66666666-6666-6666-6666-666666666666', 'openrouter', '/api/v1/chat/completions', 18000, 0.018, now() - interval '70 days'),
  ('99999999-9999-9999-9999-999999999999', 'openrouter', '/api/v1/chat/completions', 22000, 0.022, now() - interval '95 days'),
  ('99999999-9999-9999-9999-999999999999', 'videodb', '/api/v1/search', 0, 0.015, now() - interval '90 days');

-- 7. INSERT DEMO SUPPORT TICKETS
INSERT INTO support_tickets (user_id, subject, description, status, priority, created_at) VALUES
  ('33333333-3333-3333-3333-333333333333', 'Cannot upload video', 'Getting error when uploading MP4 files larger than 100MB', 'open', 'medium', now() - interval '3 days'),
  ('55555555-5555-5555-5555-555555555555', 'Render stuck at processing', 'My video has been processing for 2 hours', 'in_progress', 'high', now() - interval '1 day'),
  ('77777777-7777-7777-7777-777777777777', 'How to connect TikTok?', 'I need help connecting my TikTok account', 'resolved', 'low', now() - interval '5 days'),
  ('88888888-8888-8888-8888-888888888888', 'Credits not updating', 'I purchased credits but they are not showing', 'open', 'urgent', now() - interval '6 hours');

-- ============================================================
-- VERIFY DATA
-- ============================================================
SELECT 'profiles' as table_name, count(*) as rows FROM profiles
UNION ALL SELECT 'subscriptions', count(*) FROM subscriptions
UNION ALL SELECT 'transactions', count(*) FROM transactions
UNION ALL SELECT 'render_jobs', count(*) FROM render_jobs
UNION ALL SELECT 'user_sessions', count(*) FROM user_sessions
UNION ALL SELECT 'api_usage', count(*) FROM api_usage
UNION ALL SELECT 'support_tickets', count(*) FROM support_tickets;
