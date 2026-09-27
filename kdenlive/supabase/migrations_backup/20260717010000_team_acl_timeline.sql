-- Team ACL + collaborative calendar support
-- Extends posts for org/team workflow, adds per-account ACL and collaboration tables.

-- ─── posts workflow columns ───────────────────────────────────────────────────
ALTER TABLE public.posts
  ADD COLUMN IF NOT EXISTS organization_id uuid REFERENCES public.organizations(id) ON DELETE SET NULL,
  ADD COLUMN IF NOT EXISTS team_id uuid REFERENCES public.teams(id) ON DELETE SET NULL,
  ADD COLUMN IF NOT EXISTS campaign_name text,
  ADD COLUMN IF NOT EXISTS workflow_status text DEFAULT 'draft',
  ADD COLUMN IF NOT EXISTS assigned_member_ids uuid[] DEFAULT '{}'::uuid[],
  ADD COLUMN IF NOT EXISTS approver_member_ids uuid[] DEFAULT '{}'::uuid[],
  ADD COLUMN IF NOT EXISTS version integer DEFAULT 1 NOT NULL,
  ADD COLUMN IF NOT EXISTS comment_count integer DEFAULT 0 NOT NULL,
  ADD COLUMN IF NOT EXISTS locked_at timestamptz,
  ADD COLUMN IF NOT EXISTS error_flags text[] DEFAULT '{}'::text[];

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint WHERE conname = 'posts_workflow_status_check'
  ) THEN
    ALTER TABLE public.posts
      ADD CONSTRAINT posts_workflow_status_check
      CHECK (workflow_status = ANY (ARRAY[
        'draft'::text,
        'in_review'::text,
        'approved'::text,
        'scheduled'::text,
        'published'::text,
        'overdue'::text,
        'error'::text
      ]));
  END IF;
END $$;

CREATE INDEX IF NOT EXISTS posts_org_scheduled_idx
  ON public.posts (organization_id, scheduled_at DESC NULLS LAST);

CREATE INDEX IF NOT EXISTS posts_team_workflow_idx
  ON public.posts (team_id, workflow_status);

-- ─── member_account_access ────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS public.member_account_access (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  member_id uuid NOT NULL REFERENCES public.organization_members(id) ON DELETE CASCADE,
  platform text NOT NULL,
  social_account_id uuid,
  can_view boolean NOT NULL DEFAULT true,
  can_publish boolean NOT NULL DEFAULT false,
  can_inbox boolean NOT NULL DEFAULT false,
  created_at timestamptz NOT NULL DEFAULT timezone('utc'::text, now()),
  UNIQUE (member_id, platform, social_account_id)
);

CREATE INDEX IF NOT EXISTS member_account_access_member_idx
  ON public.member_account_access (member_id);

ALTER TABLE public.member_account_access ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "View member account access of own organization" ON public.member_account_access;
CREATE POLICY "View member account access of own organization"
  ON public.member_account_access
  FOR SELECT
  USING (
    EXISTS (
      SELECT 1
      FROM public.organization_members om
      WHERE om.id = member_account_access.member_id
        AND public.is_org_member_check(om.organization_id)
    )
  );

DROP POLICY IF EXISTS "Manage member account access of own organization" ON public.member_account_access;
CREATE POLICY "Manage member account access of own organization"
  ON public.member_account_access
  FOR ALL
  USING (
    EXISTS (
      SELECT 1
      FROM public.organization_members om
      WHERE om.id = member_account_access.member_id
        AND public.is_org_admin_check(om.organization_id)
    )
  )
  WITH CHECK (
    EXISTS (
      SELECT 1
      FROM public.organization_members om
      WHERE om.id = member_account_access.member_id
        AND public.is_org_admin_check(om.organization_id)
    )
  );

-- ─── post_comments ────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS public.post_comments (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  post_id uuid NOT NULL REFERENCES public.posts(id) ON DELETE CASCADE,
  author_user_id uuid NOT NULL REFERENCES public.profiles(id) ON DELETE CASCADE,
  body text NOT NULL,
  resolved boolean NOT NULL DEFAULT false,
  created_at timestamptz NOT NULL DEFAULT timezone('utc'::text, now())
);

CREATE INDEX IF NOT EXISTS post_comments_post_idx
  ON public.post_comments (post_id, created_at DESC);

ALTER TABLE public.post_comments ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Org members view post comments" ON public.post_comments;
CREATE POLICY "Org members view post comments"
  ON public.post_comments
  FOR SELECT
  USING (
    EXISTS (
      SELECT 1
      FROM public.posts p
      WHERE p.id = post_comments.post_id
        AND (
          p.user_id = auth.uid()
          OR (
            p.organization_id IS NOT NULL
            AND public.is_org_member_check(p.organization_id)
          )
        )
    )
  );

DROP POLICY IF EXISTS "Org members insert post comments" ON public.post_comments;
CREATE POLICY "Org members insert post comments"
  ON public.post_comments
  FOR INSERT
  WITH CHECK (
    author_user_id = auth.uid()
    AND EXISTS (
      SELECT 1
      FROM public.posts p
      WHERE p.id = post_comments.post_id
        AND (
          p.user_id = auth.uid()
          OR (
            p.organization_id IS NOT NULL
            AND public.is_org_member_check(p.organization_id)
          )
        )
    )
  );

DROP POLICY IF EXISTS "Authors or admins update post comments" ON public.post_comments;
CREATE POLICY "Authors or admins update post comments"
  ON public.post_comments
  FOR UPDATE
  USING (
    author_user_id = auth.uid()
    OR EXISTS (
      SELECT 1
      FROM public.posts p
      WHERE p.id = post_comments.post_id
        AND p.organization_id IS NOT NULL
        AND public.is_org_admin_check(p.organization_id)
    )
  );

-- Keep posts.comment_count in sync for unresolved comments
CREATE OR REPLACE FUNCTION public.sync_post_comment_count()
RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
  target_post uuid;
BEGIN
  target_post := COALESCE(NEW.post_id, OLD.post_id);
  UPDATE public.posts
  SET comment_count = (
    SELECT COUNT(*)::integer
    FROM public.post_comments c
    WHERE c.post_id = target_post AND c.resolved = false
  )
  WHERE id = target_post;
  RETURN COALESCE(NEW, OLD);
END;
$$;

DROP TRIGGER IF EXISTS trg_sync_post_comment_count ON public.post_comments;
CREATE TRIGGER trg_sync_post_comment_count
  AFTER INSERT OR UPDATE OR DELETE ON public.post_comments
  FOR EACH ROW
  EXECUTE FUNCTION public.sync_post_comment_count();

-- ─── post_versions ────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS public.post_versions (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  post_id uuid NOT NULL REFERENCES public.posts(id) ON DELETE CASCADE,
  version integer NOT NULL,
  snapshot jsonb NOT NULL DEFAULT '{}'::jsonb,
  created_by uuid REFERENCES public.profiles(id) ON DELETE SET NULL,
  created_at timestamptz NOT NULL DEFAULT timezone('utc'::text, now()),
  UNIQUE (post_id, version)
);

CREATE INDEX IF NOT EXISTS post_versions_post_idx
  ON public.post_versions (post_id, version DESC);

ALTER TABLE public.post_versions ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Org members view post versions" ON public.post_versions;
CREATE POLICY "Org members view post versions"
  ON public.post_versions
  FOR SELECT
  USING (
    EXISTS (
      SELECT 1
      FROM public.posts p
      WHERE p.id = post_versions.post_id
        AND (
          p.user_id = auth.uid()
          OR (
            p.organization_id IS NOT NULL
            AND public.is_org_member_check(p.organization_id)
          )
        )
    )
  );

DROP POLICY IF EXISTS "Org members insert post versions" ON public.post_versions;
CREATE POLICY "Org members insert post versions"
  ON public.post_versions
  FOR INSERT
  WITH CHECK (
    EXISTS (
      SELECT 1
      FROM public.posts p
      WHERE p.id = post_versions.post_id
        AND (
          p.user_id = auth.uid()
          OR (
            p.organization_id IS NOT NULL
            AND public.is_org_member_check(p.organization_id)
          )
        )
    )
  );

-- Org members can update/select team-scoped posts
DROP POLICY IF EXISTS "Org members view team posts" ON public.posts;
CREATE POLICY "Org members view team posts"
  ON public.posts
  FOR SELECT
  USING (
    organization_id IS NOT NULL
    AND public.is_org_member_check(organization_id)
  );

DROP POLICY IF EXISTS "Org members update team posts" ON public.posts;
CREATE POLICY "Org members update team posts"
  ON public.posts
  FOR UPDATE
  USING (
    organization_id IS NOT NULL
    AND public.is_org_member_check(organization_id)
  )
  WITH CHECK (
    organization_id IS NOT NULL
    AND public.is_org_member_check(organization_id)
  );

DROP POLICY IF EXISTS "Org members insert team posts" ON public.posts;
CREATE POLICY "Org members insert team posts"
  ON public.posts
  FOR INSERT
  WITH CHECK (
    user_id = auth.uid()
    AND (
      organization_id IS NULL
      OR public.is_org_member_check(organization_id)
    )
  );

GRANT ALL ON TABLE public.member_account_access TO authenticated, service_role;
GRANT ALL ON TABLE public.post_comments TO authenticated, service_role;
GRANT ALL ON TABLE public.post_versions TO authenticated, service_role;
