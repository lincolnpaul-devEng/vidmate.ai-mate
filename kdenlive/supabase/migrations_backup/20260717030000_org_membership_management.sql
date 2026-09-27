-- Leave / delete organization (SECURITY DEFINER so RLS does not block cleanup)

CREATE OR REPLACE FUNCTION public.leave_organization(p_org_id uuid DEFAULT NULL)
RETURNS void
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path TO public
AS $$
DECLARE
  current_user_id uuid := auth.uid();
  v_member_id uuid;
  v_org_id uuid;
  v_role text;
  super_admin_count int;
  member_count int;
BEGIN
  IF current_user_id IS NULL THEN
    RAISE EXCEPTION 'Not authenticated';
  END IF;

  SELECT id, organization_id, role
  INTO v_member_id, v_org_id, v_role
  FROM organization_members
  WHERE user_id = current_user_id
    AND (p_org_id IS NULL OR organization_id = p_org_id)
  LIMIT 1;

  IF v_member_id IS NULL THEN
    RAISE EXCEPTION 'You are not a member of this organization';
  END IF;

  IF p_org_id IS NOT NULL AND p_org_id <> v_org_id THEN
    RAISE EXCEPTION 'Organization mismatch';
  END IF;

  SELECT COUNT(*) INTO member_count
  FROM organization_members
  WHERE organization_id = v_org_id;

  IF member_count = 1 THEN
    RAISE EXCEPTION 'You are the only member. Delete the organization instead of leaving.';
  END IF;

  IF v_role = 'Super Admin' THEN
    SELECT COUNT(*) INTO super_admin_count
    FROM organization_members
    WHERE organization_id = v_org_id
      AND role = 'Super Admin';

    IF super_admin_count <= 1 THEN
      RAISE EXCEPTION 'You are the only Super Admin. Promote another member or delete the organization.';
    END IF;
  END IF;

  DELETE FROM organization_members WHERE id = v_member_id;
END;
$$;

CREATE OR REPLACE FUNCTION public.delete_organization(p_org_id uuid)
RETURNS void
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path TO public
AS $$
DECLARE
  current_user_id uuid := auth.uid();
BEGIN
  IF current_user_id IS NULL THEN
    RAISE EXCEPTION 'Not authenticated';
  END IF;

  IF p_org_id IS NULL THEN
    RAISE EXCEPTION 'Organization id is required';
  END IF;

  IF NOT EXISTS (
    SELECT 1
    FROM organization_members
    WHERE organization_id = p_org_id
      AND user_id = current_user_id
      AND role = 'Super Admin'
  ) THEN
    RAISE EXCEPTION 'Only a Super Admin can delete this organization';
  END IF;

  DELETE FROM organizations WHERE id = p_org_id;
END;
$$;

GRANT EXECUTE ON FUNCTION public.leave_organization(uuid) TO authenticated;
GRANT EXECUTE ON FUNCTION public.delete_organization(uuid) TO authenticated;
