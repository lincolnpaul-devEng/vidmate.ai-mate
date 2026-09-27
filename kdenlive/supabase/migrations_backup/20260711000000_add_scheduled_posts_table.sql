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

-- Create trigger function to sync scheduled_posts from posts table
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

-- Create trigger on posts table
DROP TRIGGER IF EXISTS sync_scheduled_posts_from_posts ON posts;
CREATE TRIGGER sync_scheduled_posts_from_posts
AFTER INSERT OR UPDATE ON posts
FOR EACH ROW EXECUTE PROCEDURE public.sync_scheduled_posts_from_posts();

-- Add columns to posts table to track rendered output
ALTER TABLE posts
ADD COLUMN IF NOT EXISTS rendered_output_url TEXT,
ADD COLUMN IF NOT EXISTS rendered_at TIMESTAMP WITH TIME ZONE;

-- Enable RLS for scheduled_posts
ALTER TABLE scheduled_posts ENABLE ROW LEVEL SECURITY;

-- RLS policies for scheduled_posts
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

-- Add scheduled_posts to realtime publication
ALTER PUBLICATION supabase_realtime ADD TABLE scheduled_posts;
