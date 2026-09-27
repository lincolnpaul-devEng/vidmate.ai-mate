-- ============================================================
-- STORAGE BUCKETS & RLS POLICIES
-- Run this in Supabase SQL Editor to fix all storage issues
-- ============================================================

-- 1. Create avatars bucket (for profile pictures)
INSERT INTO storage.buckets (id, name, public, created_at, updated_at)
VALUES ('avatars', 'avatars', true, now(), now())
ON CONFLICT (id) DO UPDATE SET public = true, updated_at = now();

-- 2. Create renders bucket (for clipped/rendered videos)
INSERT INTO storage.buckets (id, name, public, created_at, updated_at)
VALUES ('renders', 'renders', true, now(), now())
ON CONFLICT (id) DO UPDATE SET public = true, updated_at = now();

-- 3. Create media bucket (for general uploads - images, videos)
INSERT INTO storage.buckets (id, name, public, created_at, updated_at)
VALUES ('media', 'media', true, now(), now())
ON CONFLICT (id) DO UPDATE SET public = true, updated_at = now();

-- ============================================================
-- AVATARS BUCKET POLICIES
-- ============================================================

CREATE POLICY "Anyone can read avatars"
  ON storage.objects FOR SELECT
  USING (bucket_id = 'avatars');

CREATE POLICY "Authenticated users can upload avatars"
  ON storage.objects FOR INSERT
  TO authenticated
  WITH CHECK (bucket_id = 'avatars');

CREATE POLICY "Users can update own avatars"
  ON storage.objects FOR UPDATE
  TO authenticated
  USING (bucket_id = 'avatars')
  WITH CHECK (bucket_id = 'avatars');

CREATE POLICY "Users can delete own avatars"
  ON storage.objects FOR DELETE
  TO authenticated
  USING (bucket_id = 'avatars');

-- ============================================================
-- RENDERS BUCKET POLICIES (clipped videos)
-- ============================================================

CREATE POLICY "Anyone can read renders"
  ON storage.objects FOR SELECT
  USING (bucket_id = 'renders');

CREATE POLICY "Authenticated users can upload renders"
  ON storage.objects FOR INSERT
  TO authenticated, service_role
  WITH CHECK (bucket_id = 'renders');

CREATE POLICY "Users can update own renders"
  ON storage.objects FOR UPDATE
  TO authenticated, service_role
  USING (bucket_id = 'renders')
  WITH CHECK (bucket_id = 'renders');

CREATE POLICY "Users can delete own renders"
  ON storage.objects FOR DELETE
  TO authenticated, service_role
  USING (bucket_id = 'renders');

-- ============================================================
-- MEDIA BUCKET POLICIES (general uploads)
-- ============================================================

CREATE POLICY "Anyone can read media"
  ON storage.objects FOR SELECT
  USING (bucket_id = 'media');

CREATE POLICY "Authenticated users can upload media"
  ON storage.objects FOR INSERT
  TO authenticated, service_role
  WITH CHECK (bucket_id = 'media');

CREATE POLICY "Users can update own media"
  ON storage.objects FOR UPDATE
  TO authenticated, service_role
  USING (bucket_id = 'media')
  WITH CHECK (bucket_id = 'media');

CREATE POLICY "Users can delete own media"
  ON storage.objects FOR DELETE
  TO authenticated, service_role
  USING (bucket_id = 'media');

-- ============================================================
-- VERIFY BUCKETS EXIST
-- ============================================================
SELECT id, name, public FROM storage.buckets WHERE id IN ('avatars', 'renders', 'media');
