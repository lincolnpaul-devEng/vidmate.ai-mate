-- Create profiles table
CREATE TABLE IF NOT EXISTS profiles (
  id UUID REFERENCES auth.users ON DELETE CASCADE PRIMARY KEY,
  name TEXT,
  email TEXT,
  avatar_url TEXT,
  bio TEXT,
  is_premium BOOLEAN DEFAULT FALSE,
  credits INTEGER DEFAULT 100,
  subscription_tier TEXT DEFAULT 'Free Plan',
  total_posts INTEGER DEFAULT 0,
  total_reach INTEGER DEFAULT 0,
  streak INTEGER DEFAULT 0,
  updated_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

-- Enable RLS for profiles
ALTER TABLE profiles ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Users can view their own profile" ON profiles;
CREATE POLICY "Users can view their own profile"
  ON profiles FOR SELECT
  USING (auth.uid() = id);

DROP POLICY IF EXISTS "Users can update their own profile" ON profiles;
CREATE POLICY "Users can update their own profile"
  ON profiles FOR UPDATE
  USING (auth.uid() = id);

-- Create linked_accounts table
CREATE TABLE IF NOT EXISTS linked_accounts (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE NOT NULL,
  platform TEXT NOT NULL,
  username TEXT,
  avatar_url TEXT,
  connected BOOLEAN DEFAULT FALSE,
  followers INTEGER DEFAULT 0,
  updated_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL,
  UNIQUE(user_id, platform)
);

-- Enable RLS for linked_accounts
ALTER TABLE linked_accounts ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Users can view their own accounts" ON linked_accounts;
CREATE POLICY "Users can view their own accounts"
  ON linked_accounts FOR SELECT
  USING (auth.uid() = user_id);

DROP POLICY IF EXISTS "Users can manage their own accounts" ON linked_accounts;
CREATE POLICY "Users can manage their own accounts"
  ON linked_accounts FOR ALL
  USING (auth.uid() = user_id);

-- Create posts table
CREATE TABLE IF NOT EXISTS posts (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE NOT NULL,
  caption TEXT,
  content TEXT,
  platforms TEXT[] DEFAULT '{}',
  status TEXT DEFAULT 'draft',
  media_urls TEXT[] DEFAULT '{}',
  analytics JSONB DEFAULT '{"impressions": 0, "engagements": 0, "clicks": 0, "shares": 0}',
  scheduled_at TIMESTAMP WITH TIME ZONE,
  cloudinary_public_id TEXT,
  cloudinary_version TEXT,
  platform_links JSONB DEFAULT '{}',
  metadata JSONB DEFAULT '{}',
  created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

-- Create scheduled_posts table for realtime scheduled post tracking
CREATE TABLE IF NOT EXISTS scheduled_posts (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  post_id UUID REFERENCES posts(id) ON DELETE CASCADE NOT NULL UNIQUE,
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE NOT NULL,
  status TEXT NOT NULL DEFAULT 'scheduled',
  platforms TEXT[] DEFAULT '{}',
  scheduled_at TIMESTAMP WITH TIME ZONE,
  last_run_at TIMESTAMP WITH TIME ZONE,
  published_at TIMESTAMP WITH TIME ZONE,
  error_message TEXT,
  metadata JSONB DEFAULT '{}',
  created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL,
  updated_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

CREATE OR REPLACE FUNCTION public.sync_scheduled_posts_from_posts()
RETURNS trigger AS $$
BEGIN
  IF NEW.status IN ('scheduled', 'pending', 'published', 'failed') AND NEW.scheduled_at IS NOT NULL THEN
    INSERT INTO scheduled_posts (
      post_id,
      user_id,
      status,
      platforms,
      scheduled_at,
      last_run_at,
      published_at,
      error_message,
      metadata,
      created_at,
      updated_at
    ) VALUES (
      NEW.id,
      NEW.user_id,
      NEW.status,
      COALESCE(NEW.platforms, '{}'),
      NEW.scheduled_at,
      CASE WHEN NEW.status IN ('pending', 'failed', 'published') THEN timezone('utc'::text, now()) ELSE NULL END,
      CASE WHEN NEW.status = 'published' THEN timezone('utc'::text, now()) ELSE NULL END,
      COALESCE(NEW.metadata->>'scheduler_last_error', NULL),
      COALESCE(NEW.metadata, '{}'::jsonb),
      timezone('utc'::text, now()),
      timezone('utc'::text, now())
    )
    ON CONFLICT (post_id) DO UPDATE
      SET status = EXCLUDED.status,
          platforms = EXCLUDED.platforms,
          scheduled_at = EXCLUDED.scheduled_at,
          last_run_at = CASE WHEN EXCLUDED.status IN ('pending', 'failed', 'published') THEN timezone('utc'::text, now()) ELSE scheduled_posts.last_run_at END,
          published_at = CASE WHEN EXCLUDED.status = 'published' THEN COALESCE(scheduled_posts.published_at, timezone('utc'::text, now())) ELSE scheduled_posts.published_at END,
          error_message = COALESCE(EXCLUDED.error_message, scheduled_posts.error_message),
          metadata = EXCLUDED.metadata,
          updated_at = timezone('utc'::text, now());
  END IF;

  RETURN NEW;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

CREATE TRIGGER sync_scheduled_posts_from_posts
AFTER INSERT OR UPDATE ON posts
FOR EACH ROW EXECUTE PROCEDURE public.sync_scheduled_posts_from_posts();

-- Safely add missing columns if the table existed before
DO $$ 
BEGIN 
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='cloudinary_public_id') THEN
    ALTER TABLE posts ADD COLUMN cloudinary_public_id TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='cloudinary_version') THEN
    ALTER TABLE posts ADD COLUMN cloudinary_version TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='youtube_video_id') THEN
    ALTER TABLE posts ADD COLUMN youtube_video_id TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='external_id') THEN
    ALTER TABLE posts ADD COLUMN external_id TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='platform') THEN
    ALTER TABLE posts ADD COLUMN platform TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='metrics') THEN
    ALTER TABLE posts ADD COLUMN metrics JSONB DEFAULT '{}';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='last_synced_at') THEN
    ALTER TABLE posts ADD COLUMN last_synced_at TIMESTAMP WITH TIME ZONE;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='platform_links') THEN
    ALTER TABLE posts ADD COLUMN platform_links JSONB DEFAULT '{}';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='metadata') THEN
    ALTER TABLE posts ADD COLUMN metadata JSONB DEFAULT '{}';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='rendered_output_url') THEN
    ALTER TABLE posts ADD COLUMN rendered_output_url TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='posts' AND column_name='rendered_at') THEN
    ALTER TABLE posts ADD COLUMN rendered_at TIMESTAMP WITH TIME ZONE;
  END IF;
END $$;

-- Add Unique Constraint for efficient UPSERTS
ALTER TABLE posts DROP CONSTRAINT IF EXISTS posts_user_external_platform_idx;
ALTER TABLE posts ADD CONSTRAINT posts_user_external_platform_idx UNIQUE (user_id, external_id, platform);

-- Enable RLS for posts
ALTER TABLE posts ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Users can view their own posts" ON posts;
CREATE POLICY "Users can view their own posts"
  ON posts FOR SELECT
  USING (auth.uid() = user_id);

DROP POLICY IF EXISTS "Users can manage their own posts" ON posts;
CREATE POLICY "Users can manage their own posts"
  ON posts FOR ALL
  USING (auth.uid() = user_id);

DROP POLICY IF EXISTS "Users can view own scheduled posts" ON scheduled_posts;
CREATE POLICY "Users can view own scheduled posts"
  ON scheduled_posts FOR SELECT
  USING (auth.uid() = user_id);

DROP POLICY IF EXISTS "Users can manage own scheduled posts" ON scheduled_posts;
CREATE POLICY "Users can manage own scheduled posts"
  ON scheduled_posts FOR ALL
  USING (auth.uid() = user_id);

DROP POLICY IF EXISTS "Admins can view all scheduled posts" ON scheduled_posts;
CREATE POLICY "Admins can view all scheduled posts"
  ON scheduled_posts FOR SELECT
  USING (EXISTS (SELECT 1 FROM admin_users WHERE id = auth.uid()));

-- Trigger to create profile on signup
CREATE OR REPLACE FUNCTION public.handle_new_user()
RETURNS trigger 
SET search_path = public
AS $$
BEGIN
  INSERT INTO public.profiles (id, name, email, avatar_url)
  VALUES (
    new.id, 
    COALESCE(new.raw_user_meta_data->>'full_name', new.raw_user_meta_data->>'name'), 
    new.email, 
    COALESCE(new.raw_user_meta_data->>'avatar_url', new.raw_user_meta_data->>'picture')
  )
  ON CONFLICT (id) DO UPDATE SET
    name = EXCLUDED.name,
    email = EXCLUDED.email,
    avatar_url = EXCLUDED.avatar_url,
    updated_at = timezone('utc'::text, now());
  RETURN new;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

DROP TRIGGER IF EXISTS on_auth_user_created ON auth.users;
CREATE TRIGGER on_auth_user_created
  AFTER INSERT ON auth.users
  FOR EACH ROW EXECUTE PROCEDURE public.handle_new_user();

-- Module 1: Unified Inbox
DO $$ BEGIN
    CREATE TYPE platform_enum AS ENUM ('yt', 'fb', 'ig');
EXCEPTION
    WHEN duplicate_object THEN null;
END $$;

CREATE TABLE IF NOT EXISTS inbox_items (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE NOT NULL,
  source_platform platform_enum NOT NULL,
  content TEXT,
  author_meta JSONB,
  is_replied BOOLEAN DEFAULT FALSE,
  external_id TEXT, -- For tracking external comments
  created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

-- Safely add external_id if missing
DO $$ 
BEGIN 
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='inbox_items' AND column_name='external_id') THEN
    ALTER TABLE inbox_items ADD COLUMN external_id TEXT;
  END IF;
END $$;

-- Add Unique Constraint for efficient UPSERTS
ALTER TABLE inbox_items DROP CONSTRAINT IF EXISTS inbox_items_user_external_platform_idx;
ALTER TABLE inbox_items ADD CONSTRAINT inbox_items_user_external_platform_idx UNIQUE (user_id, external_id, source_platform);

-- Enable RLS for inbox_items
ALTER TABLE inbox_items ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Users can view their own inbox items" ON inbox_items;
CREATE POLICY "Users can view their own inbox items"
  ON inbox_items FOR SELECT
  USING (auth.uid() = user_id);

DROP POLICY IF EXISTS "Users can manage their own inbox items" ON inbox_items;
CREATE POLICY "Users can manage their own inbox items"
  ON inbox_items FOR ALL
  USING (auth.uid() = user_id);

-- Safely add OAuth token columns to linked_accounts (The Keyring)
DO $$ 
BEGIN 
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='linked_accounts' AND column_name='access_token') THEN
    ALTER TABLE linked_accounts ADD COLUMN access_token TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='linked_accounts' AND column_name='refresh_token') THEN
    ALTER TABLE linked_accounts ADD COLUMN refresh_token TEXT;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='linked_accounts' AND column_name='token_expires_at') THEN
    ALTER TABLE linked_accounts ADD COLUMN token_expires_at TIMESTAMP WITH TIME ZONE;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='linked_accounts' AND column_name='external_id') THEN
    ALTER TABLE linked_accounts ADD COLUMN external_id TEXT;
  END IF;
END $$;

-- Create viral_videos table
CREATE TABLE IF NOT EXISTS viral_videos (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  title TEXT NOT NULL,
  video_url TEXT NOT NULL,
  thumbnail_url TEXT,
  platform TEXT,
  format TEXT,
  niche TEXT,
  duration TEXT,
  views INTEGER DEFAULT 0,
  likes INTEGER DEFAULT 0,
  created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

-- Enable RLS for viral_videos
ALTER TABLE viral_videos ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Public can view viral videos" ON viral_videos;
CREATE POLICY "Public can view viral videos"
  ON viral_videos FOR SELECT
  USING (true);

DROP POLICY IF EXISTS "Authenticated users can manage viral videos" ON viral_videos;
CREATE POLICY "Authenticated users can manage viral videos"
  ON viral_videos FOR ALL
  USING (auth.role() = 'authenticated');

-- Create security_logs table
CREATE TABLE IF NOT EXISTS security_logs (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE NOT NULL,
  event TEXT NOT NULL,
  description TEXT,
  ip_address TEXT,
  user_agent TEXT,
  created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

-- Enable RLS for security_logs
ALTER TABLE security_logs ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Users can view their own security logs" ON security_logs;
CREATE POLICY "Users can view their own security logs"
  ON security_logs FOR SELECT
  USING (auth.uid() = user_id);

-- Enable Realtime for all interactive tables
begin; 
  drop publication if exists supabase_realtime; 
  create publication supabase_realtime; 
commit; 

alter publication supabase_realtime add table posts;
alter publication supabase_realtime add table profiles;
alter publication supabase_realtime add table linked_accounts;
alter publication supabase_realtime add table inbox_items;
alter publication supabase_realtime add table viral_videos;
alter publication supabase_realtime add table security_logs;
alter publication supabase_realtime add table scheduled_posts;

-- Extension setup for Cron & HTTP Requests
CREATE EXTENSION IF NOT EXISTS pg_cron;
CREATE EXTENSION IF NOT EXISTS pg_net;

-- Create post_comments table
CREATE TABLE IF NOT EXISTS post_comments (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  post_id UUID REFERENCES posts(id) ON DELETE CASCADE NOT NULL,
  user_id UUID REFERENCES profiles(id) ON DELETE CASCADE NOT NULL,
  content TEXT NOT NULL,
  is_internal BOOLEAN DEFAULT TRUE,
  created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

-- Enable RLS for post_comments
ALTER TABLE post_comments ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Users can view comments on their own posts" ON post_comments;
CREATE POLICY "Users can view comments on their own posts"
  ON post_comments FOR SELECT
  USING (
    EXISTS (
      SELECT 1 FROM posts
      WHERE posts.id = post_comments.post_id
      AND posts.user_id = auth.uid()
    )
  );

DROP POLICY IF EXISTS "Users can insert comments on their own posts" ON post_comments;
CREATE POLICY "Users can insert comments on their own posts"
  ON post_comments FOR INSERT
  WITH CHECK (
    EXISTS (
      SELECT 1 FROM posts
      WHERE posts.id = post_comments.post_id
      AND posts.user_id = auth.uid()
    )
  );

-- Add to Realtime
ALTER PUBLICATION supabase_realtime ADD TABLE post_comments;

-- Create AI Editor Feedback table for tracking user editing history to optimize AI suggestions over time
CREATE TABLE IF NOT EXISTS public.ai_editor_feedback (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID REFERENCES public.profiles(id) ON DELETE CASCADE NOT NULL,
  preference_type TEXT NOT NULL, -- 'font_family', 'layout_mode', 'text_color', 'duration'
  preference_value TEXT NOT NULL,
  meta_details JSONB, -- stores extra context like original suggestions vs user choice
  created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('utc'::text, now()) NOT NULL
);

-- Enable Row Level Security (RLS)
ALTER TABLE public.ai_editor_feedback ENABLE ROW LEVEL SECURITY;

-- Create policy for user access
DROP POLICY IF EXISTS "Users can insert their own AI feedback" ON public.ai_editor_feedback;
CREATE POLICY "Users can insert their own AI feedback"
  ON public.ai_editor_feedback FOR INSERT
  WITH CHECK (auth.uid() = user_id);

DROP POLICY IF EXISTS "Users can view their own AI feedback" ON public.ai_editor_feedback;
CREATE POLICY "Users can view their own AI feedback"
  ON public.ai_editor_feedback FOR SELECT
  USING (auth.uid() = user_id);

-- Note: In a production Supabase instance, uncomment and configure the below:
/*
SELECT cron.schedule(
  'socialpulse-scheduler',
  '* * * * *',
  $$
    SELECT net.http_post(
      url:='https://[YOUR_SUPABASE_PROJECT_REF].supabase.co/functions/v1/scheduler',
      headers:='{"Content-Type": "application/json", "Authorization": "Bearer [YOUR_CRON_SECRET]"}'::jsonb,
      body:='{}'::jsonb
    )
  $$
);
*/
