ALTER TABLE public.ai_clips
ADD COLUMN IF NOT EXISTS thumbnail_url TEXT;
