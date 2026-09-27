-- Fix broken organizations RLS policies.
-- Bug: compared organization_members.organization_id = organization_members.id
-- (always false / nonsense), so members could not SELECT their own org → 406 on .single().

DROP POLICY IF EXISTS "Anyone can view organizations they belong to" ON public.organizations;
CREATE POLICY "Anyone can view organizations they belong to"
  ON public.organizations
  FOR SELECT
  USING (public.is_org_member_check(id));

DROP POLICY IF EXISTS "Members can update their organizations" ON public.organizations;
CREATE POLICY "Members can update their organizations"
  ON public.organizations
  FOR UPDATE
  USING (public.is_org_admin_check(id))
  WITH CHECK (public.is_org_admin_check(id));
