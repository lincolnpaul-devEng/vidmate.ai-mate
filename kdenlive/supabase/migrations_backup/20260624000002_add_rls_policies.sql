-- ============================================================
-- RLS POLICY: Allow anyone to INSERT applications (from apply form)
-- and allow SELECT for admin dashboard
-- ============================================================

-- 1. Enable RLS on the table (already enabled from migration, but just in case)
ALTER TABLE engineering_applications ENABLE ROW LEVEL SECURITY;

-- 2. Allow anyone to submit applications (INSERT)
DROP POLICY IF EXISTS "Anyone can submit applications" ON engineering_applications;
CREATE POLICY "Anyone can submit applications"
  ON engineering_applications
  FOR INSERT
  TO anon, authenticated
  WITH CHECK (true);

-- 3. Allow anyone to read applications (SELECT) for admin dashboard
--    In production, you'd restrict this to service_role only
DROP POLICY IF EXISTS "Anyone can read applications" ON engineering_applications;
CREATE POLICY "Anyone can read applications"
  ON engineering_applications
  FOR SELECT
  TO anon, authenticated
  USING (true);

-- 4. Allow anyone to update applications (for status changes, scoring)
DROP POLICY IF EXISTS "Anyone can update applications" ON engineering_applications;
CREATE POLICY "Anyone can update applications"
  ON engineering_applications
  FOR UPDATE
  TO anon, authenticated
  USING (true)
  WITH CHECK (true);

-- 5. Allow anyone to delete applications
DROP POLICY IF EXISTS "Anyone can delete applications" ON engineering_applications;
CREATE POLICY "Anyone can delete applications"
  ON engineering_applications
  FOR DELETE
  TO anon, authenticated
  USING (true);

-- ============================================================
-- VERIFY: Test the policies
-- ============================================================

-- Check current policies
SELECT policyname, cmd, roles 
FROM pg_policies 
WHERE tablename = 'engineering_applications';
