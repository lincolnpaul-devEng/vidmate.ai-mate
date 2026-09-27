


SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;


CREATE EXTENSION IF NOT EXISTS "pg_cron" WITH SCHEMA "pg_catalog";






COMMENT ON SCHEMA "public" IS 'standard public schema';



CREATE EXTENSION IF NOT EXISTS "pg_net" WITH SCHEMA "public";






CREATE EXTENSION IF NOT EXISTS "pg_stat_statements" WITH SCHEMA "extensions";






CREATE EXTENSION IF NOT EXISTS "pgcrypto" WITH SCHEMA "extensions";






CREATE EXTENSION IF NOT EXISTS "supabase_vault" WITH SCHEMA "vault";






CREATE EXTENSION IF NOT EXISTS "uuid-ossp" WITH SCHEMA "extensions";






CREATE EXTENSION IF NOT EXISTS "vector" WITH SCHEMA "public";






CREATE TYPE "public"."ai_clip_status" AS ENUM (
    'PENDING_RENDER',
    'COMPLETED',
    'FAILED'
);


ALTER TYPE "public"."ai_clip_status" OWNER TO "postgres";


CREATE TYPE "public"."platform_enum" AS ENUM (
    'yt',
    'fb',
    'ig'
);


ALTER TYPE "public"."platform_enum" OWNER TO "postgres";


CREATE TYPE "public"."reframe_status" AS ENUM (
    'pending',
    'processing',
    'success',
    'failed'
);


ALTER TYPE "public"."reframe_status" OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."accept_pending_organization_invites"() RETURNS integer
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
DECLARE
  current_user_id UUID := auth.uid();
  current_email TEXT;
  invite_record RECORD;
  new_member_id UUID;
  accepted_count INTEGER := 0;
BEGIN
  IF current_user_id IS NULL THEN
    RETURN 0;
  END IF;

  SELECT lower(email)
  INTO current_email
  FROM profiles
  WHERE id = current_user_id;

  IF current_email IS NULL THEN
    SELECT lower(email)
    INTO current_email
    FROM auth.users
    WHERE id = current_user_id;
  END IF;

  IF current_email IS NULL THEN
    RETURN 0;
  END IF;

  IF EXISTS (
    SELECT 1 FROM organization_members WHERE user_id = current_user_id
  ) THEN
    RETURN 0;
  END IF;

  FOR invite_record IN
    SELECT *
    FROM organization_invites
    WHERE lower(email) = current_email
      AND status = 'pending'
    ORDER BY created_at ASC
    LIMIT 1
  LOOP
    INSERT INTO organization_members (organization_id, user_id, role)
    VALUES (invite_record.organization_id, current_user_id, invite_record.role)
    RETURNING id INTO new_member_id;

    INSERT INTO member_permissions (member_id, permission_level)
    VALUES (
      new_member_id,
      CASE
        WHEN invite_record.role = 'View-Only' THEN 'Read-Only'
        WHEN invite_record.role = 'Super Admin' THEN 'Advanced'
        ELSE 'Editor'
      END
    );

    UPDATE organization_invites
    SET status = 'accepted'
    WHERE id = invite_record.id;

    accepted_count := accepted_count + 1;
  END LOOP;

  RETURN accepted_count;
END;
$$;


ALTER FUNCTION "public"."accept_pending_organization_invites"() OWNER TO "postgres";

SET default_tablespace = '';

SET default_table_access_method = "heap";


CREATE TABLE IF NOT EXISTS "public"."inbox_items" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "source_platform" "public"."platform_enum" NOT NULL,
    "content" "text",
    "author_meta" "jsonb",
    "is_replied" boolean DEFAULT false,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "external_id" "text",
    "organization_id" "uuid",
    "team_id" "uuid",
    "assigned_to" "uuid",
    "assigned_to_name" "text",
    "locked_by" "uuid",
    "locked_by_name" "text",
    "locked_at" timestamp with time zone
);


ALTER TABLE "public"."inbox_items" OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."acquire_inbox_lock"("p_inbox_item_id" "uuid") RETURNS "public"."inbox_items"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
DECLARE
  current_user_id UUID := auth.uid();
  actor_name TEXT;
  target inbox_items;
BEGIN
  IF current_user_id IS NULL THEN
    RAISE EXCEPTION 'Not authenticated';
  END IF;

  SELECT COALESCE(name, email, 'Member')
  INTO actor_name
  FROM profiles
  WHERE id = current_user_id;

  SELECT * INTO target FROM inbox_items WHERE id = p_inbox_item_id FOR UPDATE;
  IF NOT FOUND THEN
    RAISE EXCEPTION 'Inbox item not found';
  END IF;

  IF target.locked_by IS NOT NULL
     AND target.locked_by <> current_user_id
     AND target.locked_at > (now() - interval '5 minutes') THEN
    RAISE EXCEPTION 'Ticket is currently locked by another teammate';
  END IF;

  UPDATE inbox_items
  SET locked_by = current_user_id,
      locked_by_name = actor_name,
      locked_at = now()
  WHERE id = p_inbox_item_id
  RETURNING * INTO target;

  RETURN target;
END;
$$;


ALTER FUNCTION "public"."acquire_inbox_lock"("p_inbox_item_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."assign_inbox_item"("p_inbox_item_id" "uuid", "p_member_id" "uuid") RETURNS "public"."inbox_items"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
DECLARE
  current_user_id UUID := auth.uid();
  target inbox_items;
  member_row organization_members;
  member_name TEXT;
BEGIN
  IF current_user_id IS NULL THEN
    RAISE EXCEPTION 'Not authenticated';
  END IF;

  SELECT * INTO target FROM inbox_items WHERE id = p_inbox_item_id;
  IF NOT FOUND THEN
    RAISE EXCEPTION 'Inbox item not found';
  END IF;

  IF p_member_id IS NULL THEN
    UPDATE inbox_items
    SET assigned_to = NULL,
        assigned_to_name = NULL
    WHERE id = p_inbox_item_id
    RETURNING * INTO target;
    RETURN target;
  END IF;

  SELECT om.* INTO member_row
  FROM organization_members om
  WHERE om.id = p_member_id;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'Team member not found';
  END IF;

  IF target.organization_id IS NOT NULL AND member_row.organization_id <> target.organization_id THEN
    RAISE EXCEPTION 'Member is not in the same organization';
  END IF;

  SELECT COALESCE(p.name, p.email, 'Member')
  INTO member_name
  FROM profiles p
  WHERE p.id = member_row.user_id;

  UPDATE inbox_items
  SET assigned_to = p_member_id,
      assigned_to_name = member_name,
      organization_id = COALESCE(organization_id, member_row.organization_id)
  WHERE id = p_inbox_item_id
  RETURNING * INTO target;

  RETURN target;
END;
$$;


ALTER FUNCTION "public"."assign_inbox_item"("p_inbox_item_id" "uuid", "p_member_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."can_view_profile"("p_target_user_id" "uuid") RETURNS boolean
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
BEGIN
  IF p_target_user_id = auth.uid() THEN
    RETURN TRUE;
  END IF;

  RETURN EXISTS (
    SELECT 1 FROM public.organization_members subject
    JOIN public.organization_members viewer ON viewer.organization_id = subject.organization_id
    WHERE subject.user_id = p_target_user_id
    AND viewer.user_id = auth.uid()
  );
END;
$$;


ALTER FUNCTION "public"."can_view_profile"("p_target_user_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."create_notification_preferences_for_new_user"() RETURNS "trigger"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
BEGIN
  INSERT INTO public.notification_preferences (user_id, email_enabled, in_app_enabled)
  VALUES (NEW.id, true, true)
  ON CONFLICT (user_id) DO NOTHING;
  RETURN NEW;
END;
$$;


ALTER FUNCTION "public"."create_notification_preferences_for_new_user"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."create_organization"("org_name" "text") RETURNS "uuid"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
DECLARE
  new_org_id UUID;
  new_member_id UUID;
  current_user_id UUID := auth.uid();
BEGIN
  IF current_user_id IS NULL THEN
    RAISE EXCEPTION 'Not authenticated';
  END IF;

  IF org_name IS NULL OR btrim(org_name) = '' THEN
    RAISE EXCEPTION 'Organization name is required';
  END IF;

  IF EXISTS (
    SELECT 1 FROM organization_members WHERE user_id = current_user_id
  ) THEN
    RAISE EXCEPTION 'You already belong to an organization';
  END IF;

  INSERT INTO organizations (name, created_by)
  VALUES (btrim(org_name), current_user_id)
  RETURNING id INTO new_org_id;

  INSERT INTO organization_members (organization_id, user_id, role)
  VALUES (new_org_id, current_user_id, 'Super Admin')
  RETURNING id INTO new_member_id;

  INSERT INTO member_permissions (member_id, permission_level)
  VALUES (new_member_id, 'Advanced');

  RETURN new_org_id;
END;
$$;


ALTER FUNCTION "public"."create_organization"("org_name" "text") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."delete_organization"("p_org_id" "uuid") RETURNS "void"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
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


ALTER FUNCTION "public"."delete_organization"("p_org_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."generate_user_insights"("p_user_id" "uuid") RETURNS "void"
    LANGUAGE "plpgsql" SECURITY DEFINER
    AS $$
BEGIN
  -- Insert default insights if they don't exist
  INSERT INTO user_insights (user_id, insight_type, title, description, category)
  VALUES
    (p_user_id, 'frequency', 'Low LinkedIn Post Frequency', 'Post 2 more times this week to drive visibility.', 'LinkedIn'),
    (p_user_id, 'trending', 'TikTok #AI Trend Alert', 'Create a short video composition to capitalize on the #AI trend.', 'TikTok'),
    (p_user_id, 'timing', 'Instagram Peak Engagement', 'Your engagement on Instagram peaked at 6 PM. Schedule your next post for optimal engagement.', 'Instagram'),
    (p_user_id, 'engagement', 'New Comment Alert', 'Reply to recent comments to boost your comment score.', 'Facebook')
  ON CONFLICT (user_id, insight_type, title) DO NOTHING;
END;
$$;


ALTER FUNCTION "public"."generate_user_insights"("p_user_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."handle_new_user"() RETURNS "trigger"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
DECLARE
  _sqlstate text;
  _message text;
  _detail text;
BEGIN
  INSERT INTO public.profiles (id, name, email, avatar_url)
  VALUES (
    new.id,
    COALESCE(new.raw_user_meta_data->>'full_name', new.raw_user_meta_data->>'name'),
    new.email,
    COALESCE(new.raw_user_meta_data->>'avatar_url', new.raw_user_meta_data->>'picture')
  )
  ON CONFLICT (id) DO UPDATE SET
    name = COALESCE(EXCLUDED.name, public.profiles.name),
    email = COALESCE(EXCLUDED.email, public.profiles.email),
    avatar_url = COALESCE(EXCLUDED.avatar_url, public.profiles.avatar_url),
    updated_at = timezone('utc'::text, now());
  RETURN new;
EXCEPTION WHEN OTHERS THEN
  GET STACKED DIAGNOSTICS _sqlstate = RETURNED_SQLSTATE, _message = MESSAGE_TEXT, _detail = PG_EXCEPTION_DETAIL;
  INSERT INTO public.trigger_debug_log (fn_name, error_message, error_detail, error_sqlstate, user_id, user_email)
  VALUES ('handle_new_user', _message, _detail, _sqlstate, new.id, new.email);
  RETURN new;
END;
$$;


ALTER FUNCTION "public"."handle_new_user"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."is_admin"() RETURNS boolean
    LANGUAGE "plpgsql" SECURITY DEFINER
    AS $$
BEGIN
  RETURN EXISTS (
    SELECT 1 FROM public.tedora_admins WHERE email = auth.jwt() ->> 'email'
  );
END;
$$;


ALTER FUNCTION "public"."is_admin"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."is_org_admin_check"("p_org_id" "uuid") RETURNS boolean
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
BEGIN
  RETURN EXISTS (
    SELECT 1 FROM public.organization_members
    WHERE organization_id = p_org_id
    AND user_id = auth.uid()
    AND role IN ('Super Admin', 'Admin')
  );
END;
$$;


ALTER FUNCTION "public"."is_org_admin_check"("p_org_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."is_org_member_check"("p_org_id" "uuid") RETURNS boolean
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
BEGIN
  RETURN EXISTS (
    SELECT 1 FROM public.organization_members
    WHERE organization_id = p_org_id
    AND user_id = auth.uid()
  );
END;
$$;


ALTER FUNCTION "public"."is_org_member_check"("p_org_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."leave_organization"("p_org_id" "uuid" DEFAULT NULL::"uuid") RETURNS "void"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
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


ALTER FUNCTION "public"."leave_organization"("p_org_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."log_application_event"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$
BEGIN
  IF TG_OP = 'INSERT' THEN
    INSERT INTO application_events (application_id, event_type, event_data)
    VALUES (NEW.id, 'submitted', jsonb_build_object('source', NEW.source));
  ELSIF TG_OP = 'UPDATE' AND OLD.status IS DISTINCT FROM NEW.status THEN
    INSERT INTO application_events (application_id, event_type, event_data)
    VALUES (NEW.id, 'status_changed', jsonb_build_object('from', OLD.status, 'to', NEW.status));
  END IF;
  RETURN NEW;
END;
$$;


ALTER FUNCTION "public"."log_application_event"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."match_hyperframes_skills"("query_embedding" "public"."vector", "match_threshold" double precision, "match_count" integer) RETURNS TABLE("id" "uuid", "title" "text", "code_snippet" "text", "category" "text", "similarity" double precision)
    LANGUAGE "sql" STABLE
    AS $$
  select
    hyperframes_skills.id,
    hyperframes_skills.title,
    hyperframes_skills.code_snippet,
    hyperframes_skills.category,
    1 - (hyperframes_skills.embedding <=> query_embedding) as similarity
  from hyperframes_skills
  where 1 - (hyperframes_skills.embedding <=> query_embedding) > match_threshold
  order by hyperframes_skills.embedding <=> query_embedding
  limit match_count;
$$;


ALTER FUNCTION "public"."match_hyperframes_skills"("query_embedding" "public"."vector", "match_threshold" double precision, "match_count" integer) OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."release_inbox_lock"("p_inbox_item_id" "uuid") RETURNS "public"."inbox_items"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
    AS $$
DECLARE
  current_user_id UUID := auth.uid();
  target inbox_items;
BEGIN
  IF current_user_id IS NULL THEN
    RAISE EXCEPTION 'Not authenticated';
  END IF;

  UPDATE inbox_items
  SET locked_by = NULL,
      locked_by_name = NULL,
      locked_at = NULL
  WHERE id = p_inbox_item_id
    AND (locked_by = current_user_id OR locked_by IS NULL)
  RETURNING * INTO target;

  IF NOT FOUND THEN
    SELECT * INTO target FROM inbox_items WHERE id = p_inbox_item_id;
  END IF;

  RETURN target;
END;
$$;


ALTER FUNCTION "public"."release_inbox_lock"("p_inbox_item_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."score_application"("p_application_id" "uuid") RETURNS "jsonb"
    LANGUAGE "plpgsql" SECURITY DEFINER
    AS $$
DECLARE
  app_record RECORD;
  result JSONB;
BEGIN
  SELECT * INTO app_record FROM engineering_applications WHERE id = p_application_id;
  
  IF NOT FOUND THEN
    RETURN jsonb_build_object('error', 'Application not found');
  END IF;
  
  -- Return the application data for AI scoring
  -- Actual AI scoring happens in the edge function (calls OpenRouter)
  RETURN jsonb_build_object(
    'id', app_record.id,
    'full_name', app_record.full_name,
    'tech_stack', app_record.tech_stack,
    'years_experience', app_record.years_experience,
    'q1_length', length(COALESCE(app_record.q1_system_design, '')),
    'q2_length', length(COALESCE(app_record.q2_pipeline_healing, '')),
    'q3_length', length(COALESCE(app_record.q3_pgvector_freetext, '')),
    'q4_length', length(COALESCE(app_record.q4_webhook_reliability, '')),
    'q5_length', length(COALESCE(app_record.q5_monorepo_arch, '')),
    'q6_length', length(COALESCE(app_record.q6_hardest_problem, '')),
    'avg_rating', (app_record.rating_video_processing + app_record.rating_ai_ml + app_record.rating_system_design) / 3.0,
    'status', app_record.status
  );
END;
$$;


ALTER FUNCTION "public"."score_application"("p_application_id" "uuid") OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."sync_post_comment_count"() RETURNS "trigger"
    LANGUAGE "plpgsql" SECURITY DEFINER
    SET "search_path" TO 'public'
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


ALTER FUNCTION "public"."sync_post_comment_count"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."sync_scheduled_posts_from_posts"() RETURNS "trigger"
    LANGUAGE "plpgsql" SECURITY DEFINER
    AS $$
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
$$;


ALTER FUNCTION "public"."sync_scheduled_posts_from_posts"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."update_ai_clips_updated_at"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$;


ALTER FUNCTION "public"."update_ai_clips_updated_at"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."update_ai_scores"("p_application_id" "uuid", "p_overall" numeric, "p_technical" numeric, "p_communication" numeric, "p_problem_solving" numeric, "p_culture_fit" numeric, "p_recommendation" "text", "p_summary" "text", "p_strengths" "text"[], "p_concerns" "text"[]) RETURNS "void"
    LANGUAGE "plpgsql" SECURITY DEFINER
    AS $$
BEGIN
  UPDATE engineering_applications
  SET
    ai_overall_score = p_overall,
    ai_technical_score = p_technical,
    ai_communication_score = p_communication,
    ai_problem_solving_score = p_problem_solving,
    ai_culture_fit_score = p_culture_fit,
    ai_recommendation = p_recommendation,
    ai_summary = p_summary,
    ai_strengths = p_strengths,
    ai_concerns = p_concerns,
    ai_scored_at = timezone('utc'::text, now())
  WHERE id = p_application_id;
  
  -- Log the scoring event
  INSERT INTO application_events (application_id, event_type, event_data)
  VALUES (p_application_id, 'scored', jsonb_build_object(
    'overall', p_overall,
    'recommendation', p_recommendation
  ));
END;
$$;


ALTER FUNCTION "public"."update_ai_scores"("p_application_id" "uuid", "p_overall" numeric, "p_technical" numeric, "p_communication" numeric, "p_problem_solving" numeric, "p_culture_fit" numeric, "p_recommendation" "text", "p_summary" "text", "p_strengths" "text"[], "p_concerns" "text"[]) OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."update_application_timestamp"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$
BEGIN
  NEW.updated_at = timezone('utc'::text, now());
  RETURN NEW;
END;
$$;


ALTER FUNCTION "public"."update_application_timestamp"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."update_render_jobs_updated_at"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$;


ALTER FUNCTION "public"."update_render_jobs_updated_at"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."update_studio_projects_updated_at"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$;


ALTER FUNCTION "public"."update_studio_projects_updated_at"() OWNER TO "postgres";


CREATE OR REPLACE FUNCTION "public"."update_updated_at_column"() RETURNS "trigger"
    LANGUAGE "plpgsql"
    AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$;


ALTER FUNCTION "public"."update_updated_at_column"() OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."ad_accounts" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "platform" "text" NOT NULL,
    "native_account_id" "text" NOT NULL,
    "account_name" "text",
    "status" "text" DEFAULT 'active'::"text",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."ad_accounts" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."ad_campaigns" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "campaign_id" "text" NOT NULL,
    "name" "text" NOT NULL,
    "platform" "text" NOT NULL,
    "objective" "text" NOT NULL,
    "status" "text" DEFAULT 'Pending'::"text",
    "spent" numeric DEFAULT 0,
    "budget" numeric DEFAULT 0,
    "impressions" integer DEFAULT 0,
    "clicks" integer DEFAULT 0,
    "ctr" numeric DEFAULT 0,
    "result_value" integer DEFAULT 0,
    "result_label" "text",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "ad_sets" "jsonb" DEFAULT '[]'::"jsonb",
    "creatives" "jsonb" DEFAULT '[]'::"jsonb",
    "demographics" "jsonb" DEFAULT '[]'::"jsonb",
    "total_revenue" numeric DEFAULT 0,
    "conversion_count" integer DEFAULT 0
);

ALTER TABLE ONLY "public"."ad_campaigns" REPLICA IDENTITY FULL;


ALTER TABLE "public"."ad_campaigns" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."admin_activity_log" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "admin_id" "uuid",
    "action" "text" NOT NULL,
    "target_type" "text",
    "target_id" "uuid",
    "details" "jsonb" DEFAULT '{}'::"jsonb",
    "ip_address" "text",
    "created_at" timestamp with time zone DEFAULT "now"()
);


ALTER TABLE "public"."admin_activity_log" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."admin_users" (
    "id" "uuid" NOT NULL,
    "role" "text" DEFAULT 'admin'::"text",
    "permissions" "jsonb" DEFAULT '{"users": true, "system": true, "billing": true, "renders": true, "support": true, "analytics": true}'::"jsonb",
    "created_at" timestamp with time zone DEFAULT "now"(),
    CONSTRAINT "admin_users_role_check" CHECK (("role" = ANY (ARRAY['super_admin'::"text", 'admin'::"text", 'support'::"text", 'viewer'::"text"])))
);


ALTER TABLE "public"."admin_users" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."ai_clips" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "project_id" "uuid" NOT NULL,
    "video_asset_id" "text" NOT NULL,
    "title" "text" NOT NULL,
    "start_time" numeric NOT NULL,
    "end_time" numeric NOT NULL,
    "duration_seconds" numeric GENERATED ALWAYS AS (("end_time" - "start_time")) STORED,
    "engagement_score" numeric DEFAULT 0.5,
    "layout_mode" "text" DEFAULT 'AUTO_FILL'::"text",
    "reasoning" "text",
    "status" "public"."ai_clip_status" DEFAULT 'PENDING_RENDER'::"public"."ai_clip_status",
    "portrait_reframe_status" "public"."reframe_status" DEFAULT 'pending'::"public"."reframe_status",
    "portrait_stream_url" "text",
    "reframe_job_id" "text",
    "reframe_error" "text",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"(),
    "thumbnail_url" "text",
    CONSTRAINT "valid_engagement_score" CHECK ((("engagement_score" >= (0)::numeric) AND ("engagement_score" <= (1)::numeric))),
    CONSTRAINT "valid_time_range" CHECK (("start_time" < "end_time"))
);


ALTER TABLE "public"."ai_clips" OWNER TO "postgres";


COMMENT ON TABLE "public"."ai_clips" IS 'Stores AI-generated video clips with async VideoDB reframe job tracking. Status progresses from PENDING_RENDER → COMPLETED/FAILED. Portrait reframe status tracks async webhook callback state.';



COMMENT ON COLUMN "public"."ai_clips"."portrait_reframe_status" IS 'Tracks async VideoDB reframe job: pending (initial) → processing (job started) → success (URL received) | failed (job failed)';



COMMENT ON COLUMN "public"."ai_clips"."portrait_stream_url" IS 'Populated by webhook callback when VideoDB reframe completes with 9:16 portrait video URL';



COMMENT ON COLUMN "public"."ai_clips"."reframe_job_id" IS 'VideoDB job ID returned when async reframe is dispatched, used to receive webhook callbacks';



CREATE TABLE IF NOT EXISTS "public"."ai_editor_feedback" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "preference_type" "text" NOT NULL,
    "preference_value" "text" NOT NULL,
    "meta_details" "jsonb",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."ai_editor_feedback" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."ai_scoring_cache" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "application_id" "uuid" NOT NULL,
    "model_version" "text" DEFAULT 'v1'::"text",
    "raw_prompt" "text",
    "raw_response" "jsonb",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."ai_scoring_cache" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."api_usage" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "service" "text" NOT NULL,
    "endpoint" "text",
    "tokens_used" integer DEFAULT 0,
    "cost_usd" numeric(10,6) DEFAULT 0,
    "status" "text" DEFAULT 'success'::"text",
    "created_at" timestamp with time zone DEFAULT "now"()
);


ALTER TABLE "public"."api_usage" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."application_events" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "application_id" "uuid" NOT NULL,
    "event_type" "text" NOT NULL,
    "event_data" "jsonb" DEFAULT '{}'::"jsonb",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "application_events_event_type_check" CHECK (("event_type" = ANY (ARRAY['submitted'::"text", 'viewed'::"text", 'scored'::"text", 'reviewed'::"text", 'status_changed'::"text", 'noted'::"text", 'interview_scheduled'::"text"])))
);


ALTER TABLE "public"."application_events" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."engineering_applications" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "full_name" "text" NOT NULL,
    "email" "text" NOT NULL,
    "phone" "text",
    "github_url" "text",
    "portfolio_url" "text",
    "years_experience" "text" NOT NULL,
    "tech_stack" "text"[] DEFAULT '{}'::"text"[],
    "q1_system_design" "text",
    "q2_pipeline_healing" "text",
    "q3_pgvector_optimization" "text"[] DEFAULT '{}'::"text"[],
    "q3_pgvector_freetext" "text",
    "q4_webhook_reliability" "text",
    "q5_monorepo_arch" "text",
    "q6_hardest_problem" "text",
    "rating_video_processing" integer,
    "rating_ai_ml" integer,
    "rating_system_design" integer,
    "ai_overall_score" numeric(4,2),
    "ai_technical_score" numeric(4,2),
    "ai_communication_score" numeric(4,2),
    "ai_problem_solving_score" numeric(4,2),
    "ai_culture_fit_score" numeric(4,2),
    "ai_recommendation" "text",
    "ai_summary" "text",
    "ai_strengths" "text"[] DEFAULT '{}'::"text"[],
    "ai_concerns" "text"[] DEFAULT '{}'::"text"[],
    "ai_scored_at" timestamp with time zone,
    "status" "text" DEFAULT 'pending'::"text",
    "reviewer_notes" "text",
    "reviewer_rating" integer,
    "source" "text" DEFAULT 'website'::"text",
    "ip_address" "text",
    "user_agent" "text",
    "submitted_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "engineering_applications_rating_ai_ml_check" CHECK ((("rating_ai_ml" >= 1) AND ("rating_ai_ml" <= 10))),
    CONSTRAINT "engineering_applications_rating_system_design_check" CHECK ((("rating_system_design" >= 1) AND ("rating_system_design" <= 10))),
    CONSTRAINT "engineering_applications_rating_video_processing_check" CHECK ((("rating_video_processing" >= 1) AND ("rating_video_processing" <= 10))),
    CONSTRAINT "engineering_applications_reviewer_rating_check" CHECK ((("reviewer_rating" >= 1) AND ("reviewer_rating" <= 10))),
    CONSTRAINT "engineering_applications_status_check" CHECK (("status" = ANY (ARRAY['pending'::"text", 'under_review'::"text", 'interview'::"text", 'offer'::"text", 'rejected'::"text", 'withdrawn'::"text"])))
);


ALTER TABLE "public"."engineering_applications" OWNER TO "postgres";


CREATE OR REPLACE VIEW "public"."application_summary" WITH ("security_invoker"='on') AS
 SELECT "id",
    "full_name",
    "email",
    "years_experience",
    "tech_stack",
    "status",
    "ai_overall_score",
    "ai_technical_score",
    "ai_recommendation",
    "ai_strengths",
    "ai_concerns",
    "rating_video_processing",
    "rating_ai_ml",
    "rating_system_design",
    "reviewer_rating",
    "submitted_at",
    "updated_at",
    ( SELECT "count"(*) AS "count"
           FROM "public"."application_events" "ae"
          WHERE ("ae"."application_id" = "ea"."id")) AS "event_count",
    ( SELECT "ae"."created_at"
           FROM "public"."application_events" "ae"
          WHERE ("ae"."application_id" = "ea"."id")
          ORDER BY "ae"."created_at" DESC
         LIMIT 1) AS "last_activity"
   FROM "public"."engineering_applications" "ea"
  ORDER BY "ai_overall_score" DESC NULLS LAST, "submitted_at" DESC;


ALTER VIEW "public"."application_summary" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."conversion_events" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "campaign_id" "text",
    "provider" "text" NOT NULL,
    "event_type" "text" NOT NULL,
    "revenue" numeric DEFAULT 0,
    "currency" "text" DEFAULT 'KES'::"text",
    "click_id" "text",
    "utm_source" "text",
    "utm_medium" "text",
    "utm_campaign" "text",
    "utm_content" "text",
    "utm_term" "text",
    "customer_email" "text",
    "order_id" "text",
    "raw_payload" "jsonb" DEFAULT '{}'::"jsonb",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "conversion_events_event_type_check" CHECK (("event_type" = ANY (ARRAY['purchase'::"text", 'signup'::"text", 'lead'::"text", 'add_to_cart'::"text", 'page_view'::"text", 'custom'::"text"]))),
    CONSTRAINT "conversion_events_provider_check" CHECK (("provider" = ANY (ARRAY['stripe'::"text", 'shopify'::"text", 'ga4'::"text", 'custom'::"text"])))
);


ALTER TABLE "public"."conversion_events" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."conversion_pixels" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "provider" "text" NOT NULL,
    "pixel_id" "text",
    "api_key" "text",
    "webhook_url" "text",
    "status" "text" DEFAULT 'pending'::"text",
    "last_event_at" timestamp with time zone,
    "config" "jsonb" DEFAULT '{}'::"jsonb",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "conversion_pixels_provider_check" CHECK (("provider" = ANY (ARRAY['stripe'::"text", 'shopify'::"text", 'ga4'::"text", 'custom'::"text"]))),
    CONSTRAINT "conversion_pixels_status_check" CHECK (("status" = ANY (ARRAY['pending'::"text", 'active'::"text", 'error'::"text"])))
);


ALTER TABLE "public"."conversion_pixels" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."coupons" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "code" "text" NOT NULL,
    "discount_type" "text",
    "discount_value" numeric(10,2) NOT NULL,
    "max_uses" integer,
    "uses_count" integer DEFAULT 0,
    "valid_from" timestamp with time zone DEFAULT "now"(),
    "expires_at" timestamp with time zone,
    "is_active" boolean DEFAULT true,
    "created_at" timestamp with time zone DEFAULT "now"(),
    CONSTRAINT "coupons_discount_type_check" CHECK (("discount_type" = ANY (ARRAY['percentage'::"text", 'fixed_kes'::"text", 'fixed_usd'::"text"])))
);


ALTER TABLE "public"."coupons" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."hyperframes_skills" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "title" "text" NOT NULL,
    "description" "text",
    "code_snippet" "text" NOT NULL,
    "category" "text" NOT NULL,
    "embedding" "public"."vector"(1536),
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "skill_name" "text",
    "metadata" "jsonb" DEFAULT '{}'::"jsonb",
    "prop_schema" "jsonb" DEFAULT '{}'::"jsonb"
);


ALTER TABLE "public"."hyperframes_skills" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."lead_intelligence" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "lead_id" "uuid" NOT NULL,
    "ai_score" integer DEFAULT 0,
    "priority_label" "text" DEFAULT 'Cold'::"text",
    "top_contributing_factors" "jsonb" DEFAULT '[]'::"jsonb",
    "manual_score_override" integer,
    "behavioral_logs" "jsonb" DEFAULT '[]'::"jsonb",
    "last_analyzed_at" timestamp with time zone DEFAULT "now"(),
    "created_at" timestamp with time zone DEFAULT "now"()
);


ALTER TABLE "public"."lead_intelligence" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."linked_accounts" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "platform" "text" NOT NULL,
    "username" "text",
    "avatar_url" "text",
    "connected" boolean DEFAULT false,
    "followers" integer DEFAULT 0,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "access_token" "text",
    "refresh_token" "text",
    "token_expires_at" timestamp with time zone,
    "external_id" "text"
);

ALTER TABLE ONLY "public"."linked_accounts" REPLICA IDENTITY FULL;


ALTER TABLE "public"."linked_accounts" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."member_account_access" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "member_id" "uuid" NOT NULL,
    "platform" "text" NOT NULL,
    "social_account_id" "uuid",
    "can_view" boolean DEFAULT true NOT NULL,
    "can_publish" boolean DEFAULT false NOT NULL,
    "can_inbox" boolean DEFAULT false NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."member_account_access" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."member_permissions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "member_id" "uuid" NOT NULL,
    "permission_level" "text" DEFAULT 'Read-Only'::"text" NOT NULL,
    "custom_rules" "jsonb" DEFAULT '{}'::"jsonb" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."member_permissions" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."notification_preferences" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "email_enabled" boolean DEFAULT true,
    "in_app_enabled" boolean DEFAULT true,
    "push_enabled" boolean DEFAULT false,
    "preferences" "jsonb" DEFAULT '{}'::"jsonb",
    "immediate_priority_only" boolean DEFAULT false,
    "daily_digest_enabled" boolean DEFAULT false,
    "digest_schedule" "text" DEFAULT 'daily'::"text",
    "digest_time" time without time zone DEFAULT '09:00:00'::time without time zone,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."notification_preferences" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."notification_types" (
    "id" "text" NOT NULL,
    "display_name" "text" NOT NULL,
    "description" "text",
    "category" "text" NOT NULL,
    "priority" "text" NOT NULL,
    "icon_emoji" "text",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "notification_types_category_check" CHECK (("category" = ANY (ARRAY['render'::"text", 'campaign'::"text", 'post'::"text", 'social'::"text", 'account'::"text", 'team'::"text"]))),
    CONSTRAINT "notification_types_priority_check" CHECK (("priority" = ANY (ARRAY['high'::"text", 'medium'::"text", 'low'::"text"])))
);


ALTER TABLE "public"."notification_types" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."notifications" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "notification_type_id" "text" NOT NULL,
    "title" "text" NOT NULL,
    "message" "text" NOT NULL,
    "data" "jsonb",
    "read_at" timestamp with time zone,
    "deleted_at" timestamp with time zone,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "notifications_read_status" CHECK (((("read_at" IS NULL) AND ("deleted_at" IS NULL)) OR (("read_at" IS NOT NULL) AND ("deleted_at" IS NULL)) OR ("deleted_at" IS NOT NULL)))
);


ALTER TABLE "public"."notifications" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."organization_invites" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "organization_id" "uuid" NOT NULL,
    "email" "text" NOT NULL,
    "display_name" "text",
    "role" "text" DEFAULT 'Admin'::"text" NOT NULL,
    "invited_by" "uuid",
    "status" "text" DEFAULT 'pending'::"text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "organization_invites_status_check" CHECK (("status" = ANY (ARRAY['pending'::"text", 'accepted'::"text", 'revoked'::"text"])))
);


ALTER TABLE "public"."organization_invites" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."organization_members" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "organization_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "role" "text" DEFAULT 'Admin'::"text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."organization_members" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."organizations" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "name" "text" NOT NULL,
    "created_by" "uuid",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."organizations" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."pending_approvals" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "organization_id" "uuid" NOT NULL,
    "team_id" "uuid",
    "post_id" "uuid",
    "submitted_by" "uuid" NOT NULL,
    "payload" "jsonb" DEFAULT '{}'::"jsonb" NOT NULL,
    "status" "text" DEFAULT 'pending'::"text" NOT NULL,
    "feedback" "text",
    "reviewed_by" "uuid",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "pending_approvals_status_check" CHECK (("status" = ANY (ARRAY['pending'::"text", 'approved'::"text", 'rejected'::"text"])))
);


ALTER TABLE "public"."pending_approvals" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."tedora_leads" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "client_name" "text" NOT NULL,
    "client_email" "text" NOT NULL,
    "company_name" "text",
    "problem_statement" "text",
    "goals" "text",
    "target_audience" "text",
    "budget_range" "text",
    "timeline" "text",
    "proposal_sent" boolean DEFAULT false,
    "status" "text" DEFAULT 'new'::"text",
    "scheduled_call" timestamp with time zone,
    "admin_notes" "text",
    "proposal_sent_at" timestamp with time zone,
    "proposal_msg_id" "text",
    "tech_stack" "text"[] DEFAULT '{}'::"text"[],
    "last_contacted_at" timestamp with time zone
);


ALTER TABLE "public"."tedora_leads" OWNER TO "postgres";


CREATE OR REPLACE VIEW "public"."pipeline_velocity" AS
 SELECT "count"(*) FILTER (WHERE ("created_at" >= ("now"() - '7 days'::interval))) AS "this_week",
    "count"(*) FILTER (WHERE (("created_at" < ("now"() - '7 days'::interval)) AND ("created_at" >= ("now"() - '14 days'::interval)))) AS "last_week"
   FROM "public"."tedora_leads";


ALTER VIEW "public"."pipeline_velocity" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."plans" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "name" "text" NOT NULL,
    "slug" "text" NOT NULL,
    "price_kes" integer DEFAULT 0 NOT NULL,
    "price_usd" numeric(10,2) DEFAULT 0,
    "interval" "text" DEFAULT 'month'::"text",
    "features" "jsonb" DEFAULT '{}'::"jsonb",
    "credits_included" integer DEFAULT 0,
    "is_active" boolean DEFAULT true,
    "created_at" timestamp with time zone DEFAULT "now"(),
    CONSTRAINT "plans_interval_check" CHECK (("interval" = ANY (ARRAY['month'::"text", 'year'::"text"])))
);


ALTER TABLE "public"."plans" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."post_comments" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "post_id" "uuid" NOT NULL,
    "author_user_id" "uuid" NOT NULL,
    "body" "text" NOT NULL,
    "resolved" boolean DEFAULT false NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."post_comments" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."post_versions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "post_id" "uuid" NOT NULL,
    "version" integer NOT NULL,
    "snapshot" "jsonb" DEFAULT '{}'::"jsonb" NOT NULL,
    "created_by" "uuid",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."post_versions" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."posts" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "caption" "text",
    "content" "text",
    "platforms" "text"[] DEFAULT '{}'::"text"[],
    "status" "text" DEFAULT 'draft'::"text",
    "media_urls" "text"[] DEFAULT '{}'::"text"[],
    "analytics" "jsonb" DEFAULT '{"clicks": 0, "shares": 0, "engagements": 0, "impressions": 0}'::"jsonb",
    "scheduled_at" timestamp with time zone,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "cloudinary_public_id" "text",
    "cloudinary_version" "text",
    "youtube_video_id" "text",
    "external_id" "text",
    "platform" "text",
    "metrics" "jsonb" DEFAULT '{}'::"jsonb",
    "last_synced_at" timestamp with time zone,
    "platform_links" "jsonb" DEFAULT '{}'::"jsonb",
    "metadata" "jsonb" DEFAULT '{}'::"jsonb",
    "rendered_output_url" "text",
    "rendered_at" timestamp with time zone,
    "organization_id" "uuid",
    "team_id" "uuid",
    "campaign_name" "text",
    "workflow_status" "text" DEFAULT 'draft'::"text",
    "assigned_member_ids" "uuid"[] DEFAULT '{}'::"uuid"[],
    "approver_member_ids" "uuid"[] DEFAULT '{}'::"uuid"[],
    "version" integer DEFAULT 1 NOT NULL,
    "comment_count" integer DEFAULT 0 NOT NULL,
    "locked_at" timestamp with time zone,
    "error_flags" "text"[] DEFAULT '{}'::"text"[],
    CONSTRAINT "posts_workflow_status_check" CHECK (("workflow_status" = ANY (ARRAY['draft'::"text", 'in_review'::"text", 'approved'::"text", 'scheduled'::"text", 'published'::"text", 'overdue'::"text", 'error'::"text"])))
);

ALTER TABLE ONLY "public"."posts" REPLICA IDENTITY FULL;


ALTER TABLE "public"."posts" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."profiles" (
    "id" "uuid" NOT NULL,
    "name" "text",
    "email" "text",
    "avatar_url" "text",
    "bio" "text",
    "is_premium" boolean DEFAULT false,
    "total_posts" integer DEFAULT 0,
    "total_reach" integer DEFAULT 0,
    "streak" integer DEFAULT 0,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "is_admin" boolean DEFAULT false,
    "role" "text",
    "location" "text",
    "company" "text",
    "portfolio" "text",
    "credits" integer DEFAULT 100,
    "subscription_tier" "text" DEFAULT 'Free Plan'::"text"
);


ALTER TABLE "public"."profiles" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."render_jobs" (
    "id" "uuid" NOT NULL,
    "status" "text" NOT NULL,
    "design" "jsonb",
    "options" "jsonb",
    "output_url" "text",
    "error" "text",
    "progress" integer DEFAULT 0,
    "callback_url" "text",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "completed_at" timestamp with time zone,
    "reasoning" "jsonb" DEFAULT '[]'::"jsonb",
    "job_id" "uuid" DEFAULT "gen_random_uuid"(),
    "user_id" "uuid",
    "script" "jsonb",
    "error_message" "text",
    "retry_count" integer DEFAULT 0,
    "updated_at" timestamp with time zone DEFAULT "now"()
);

ALTER TABLE ONLY "public"."render_jobs" REPLICA IDENTITY FULL;


ALTER TABLE "public"."render_jobs" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."scheduled_posts" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "post_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "status" "text" DEFAULT 'scheduled'::"text" NOT NULL,
    "platforms" "text"[] DEFAULT '{}'::"text"[],
    "scheduled_at" timestamp with time zone,
    "last_run_at" timestamp with time zone,
    "published_at" timestamp with time zone,
    "error_message" "text",
    "metadata" "jsonb" DEFAULT '{}'::"jsonb",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."scheduled_posts" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."studio_projects" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "title" "text" DEFAULT 'Untitled Video'::"text" NOT NULL,
    "timeline_state" "jsonb" DEFAULT '{}'::"jsonb" NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "status" "text" DEFAULT 'DRAFT'::"text" NOT NULL,
    "ai_analysis_complete" boolean DEFAULT false NOT NULL,
    "videodb_asset_id" "text",
    "error_details" "text",
    "processing_metadata" "jsonb",
    "thumbnail" "text",
    CONSTRAINT "studio_projects_status_check" CHECK (("status" = ANY (ARRAY['DRAFT'::"text", 'PROCESSING'::"text", 'READY'::"text", 'FAILED'::"text", 'INGESTING'::"text", 'ERROR'::"text"])))
);


ALTER TABLE "public"."studio_projects" OWNER TO "postgres";


COMMENT ON COLUMN "public"."studio_projects"."thumbnail" IS 'Poster/thumbnail URL for the source video, generated at manifest-resolution time via VideoDB generateThumbnail().';



CREATE TABLE IF NOT EXISTS "public"."subscriptions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "plan_id" "uuid",
    "status" "text" DEFAULT 'active'::"text",
    "started_at" timestamp with time zone DEFAULT "now"(),
    "expires_at" timestamp with time zone,
    "payment_provider" "text",
    "payment_id" "text",
    "metadata" "jsonb" DEFAULT '{}'::"jsonb",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"(),
    CONSTRAINT "subscriptions_status_check" CHECK (("status" = ANY (ARRAY['active'::"text", 'cancelled'::"text", 'expired'::"text", 'trialing'::"text", 'past_due'::"text"])))
);


ALTER TABLE "public"."subscriptions" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."support_tickets" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "subject" "text" NOT NULL,
    "description" "text",
    "status" "text" DEFAULT 'open'::"text",
    "priority" "text" DEFAULT 'medium'::"text",
    "assigned_to" "uuid",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"(),
    CONSTRAINT "support_tickets_priority_check" CHECK (("priority" = ANY (ARRAY['low'::"text", 'medium'::"text", 'high'::"text", 'urgent'::"text"]))),
    CONSTRAINT "support_tickets_status_check" CHECK (("status" = ANY (ARRAY['open'::"text", 'in_progress'::"text", 'resolved'::"text", 'closed'::"text"])))
);


ALTER TABLE "public"."support_tickets" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."team_members" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "team_id" "uuid" NOT NULL,
    "member_id" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."team_members" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."team_profiles" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "team_id" "uuid" NOT NULL,
    "platform" "text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."team_profiles" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."teams" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "organization_id" "uuid" NOT NULL,
    "name" "text" NOT NULL,
    "description" "text",
    "require_approval" boolean DEFAULT false,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."teams" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."tedora_admins" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "email" "text" NOT NULL,
    "name" "text",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "admin_name" "text",
    "avatar_url" "text"
);


ALTER TABLE "public"."tedora_admins" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."tedora_insights" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "title" "text" NOT NULL,
    "summary" "text",
    "content" "text",
    "link" "text",
    "tags" "text"[]
);


ALTER TABLE "public"."tedora_insights" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."tedora_interactions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "lead_id" "uuid" NOT NULL,
    "type" "text" NOT NULL,
    "notes" "text",
    "created_at" timestamp with time zone DEFAULT "now"()
);


ALTER TABLE "public"."tedora_interactions" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."tedora_projects" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "title" "text" NOT NULL,
    "category" "text" DEFAULT 'Software Development'::"text",
    "image_url" "text",
    "link" "text",
    "is_featured" boolean DEFAULT false
);


ALTER TABLE "public"."tedora_projects" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."tedora_proposals" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "lead_id" "uuid",
    "executive_summary" "text",
    "problem_statement" "text",
    "proposed_solution" "text",
    "scope_of_work" "jsonb" DEFAULT '[]'::"jsonb",
    "timeline" "jsonb" DEFAULT '[]'::"jsonb",
    "investment" "jsonb" DEFAULT '[]'::"jsonb",
    "terms" "text",
    "status" "text" DEFAULT 'draft'::"text",
    "signature_name" "text",
    "signature_data" "text",
    "signed_at" timestamp with time zone,
    "client_logo_url" "text",
    "client_pr_url" "text",
    "total_amount" numeric DEFAULT 0
);


ALTER TABLE "public"."tedora_proposals" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."transactions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "amount" numeric(10,2) NOT NULL,
    "currency" "text" DEFAULT 'KES'::"text",
    "type" "text",
    "status" "text" DEFAULT 'completed'::"text",
    "description" "text",
    "metadata" "jsonb" DEFAULT '{}'::"jsonb",
    "created_at" timestamp with time zone DEFAULT "now"(),
    CONSTRAINT "transactions_status_check" CHECK (("status" = ANY (ARRAY['pending'::"text", 'completed'::"text", 'failed'::"text", 'refunded'::"text"]))),
    CONSTRAINT "transactions_type_check" CHECK (("type" = ANY (ARRAY['payment'::"text", 'refund'::"text", 'credit_purchase'::"text", 'credit_usage'::"text", 'payout'::"text"])))
);


ALTER TABLE "public"."transactions" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."trigger_debug_log" (
    "id" integer NOT NULL,
    "fn_name" "text",
    "error_message" "text",
    "error_detail" "text",
    "error_sqlstate" "text",
    "user_id" "uuid",
    "user_email" "text",
    "created_at" timestamp with time zone DEFAULT "now"()
);


ALTER TABLE "public"."trigger_debug_log" OWNER TO "postgres";


CREATE SEQUENCE IF NOT EXISTS "public"."trigger_debug_log_id_seq"
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE "public"."trigger_debug_log_id_seq" OWNER TO "postgres";


ALTER SEQUENCE "public"."trigger_debug_log_id_seq" OWNED BY "public"."trigger_debug_log"."id";



CREATE TABLE IF NOT EXISTS "public"."user_assets" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "file_name" "text",
    "file_path" "text",
    "file_size" integer,
    "content_type" "text",
    "type" "text",
    "metadata" "jsonb",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."user_assets" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."user_insights" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "insight_type" "text" NOT NULL,
    "title" "text" NOT NULL,
    "description" "text" NOT NULL,
    "category" "text",
    "is_completed" boolean DEFAULT false,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "completed_at" timestamp with time zone
);


ALTER TABLE "public"."user_insights" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."user_preferences" (
    "user_id" "uuid" NOT NULL,
    "content_type" "text",
    "platforms" "text"[],
    "main_goal" "text",
    "caption_style_preset" "text",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"()
);


ALTER TABLE "public"."user_preferences" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."user_sessions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "started_at" timestamp with time zone DEFAULT "now"(),
    "ended_at" timestamp with time zone,
    "duration_seconds" integer DEFAULT 0,
    "ip_address" "text",
    "user_agent" "text",
    "page_visited" "text"
);


ALTER TABLE "public"."user_sessions" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."utm_links" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "campaign_id" "text" NOT NULL,
    "base_url" "text" NOT NULL,
    "utm_source" "text" NOT NULL,
    "utm_medium" "text" DEFAULT 'paid'::"text",
    "utm_campaign" "text" NOT NULL,
    "utm_content" "text",
    "utm_term" "text",
    "full_url" "text" NOT NULL,
    "clicks" integer DEFAULT 0,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."utm_links" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."viral_videos" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "title" "text" NOT NULL,
    "video_url" "text" NOT NULL,
    "thumbnail_url" "text",
    "platform" "text",
    "format" "text",
    "niche" "text",
    "duration" "text",
    "views" integer DEFAULT 0,
    "likes" integer DEFAULT 0,
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL
);


ALTER TABLE "public"."viral_videos" OWNER TO "postgres";


CREATE TABLE IF NOT EXISTS "public"."youtube_ingest_jobs" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "project_id" "uuid" NOT NULL,
    "youtube_url" "text" NOT NULL,
    "status" "text" DEFAULT 'PROCESSING'::"text" NOT NULL,
    "video_id" "text",
    "error_message" "text",
    "created_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    "updated_at" timestamp with time zone DEFAULT "timezone"('utc'::"text", "now"()) NOT NULL,
    CONSTRAINT "youtube_ingest_jobs_status_check" CHECK (("status" = ANY (ARRAY['PROCESSING'::"text", 'READY'::"text", 'FAILED'::"text"])))
);


ALTER TABLE "public"."youtube_ingest_jobs" OWNER TO "postgres";


ALTER TABLE ONLY "public"."trigger_debug_log" ALTER COLUMN "id" SET DEFAULT "nextval"('"public"."trigger_debug_log_id_seq"'::"regclass");



ALTER TABLE ONLY "public"."ad_accounts"
    ADD CONSTRAINT "ad_accounts_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."ad_accounts"
    ADD CONSTRAINT "ad_accounts_user_id_platform_native_account_id_key" UNIQUE ("user_id", "platform", "native_account_id");



ALTER TABLE ONLY "public"."ad_campaigns"
    ADD CONSTRAINT "ad_campaigns_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."ad_campaigns"
    ADD CONSTRAINT "ad_campaigns_user_id_platform_campaign_id_key" UNIQUE ("user_id", "platform", "campaign_id");



ALTER TABLE ONLY "public"."admin_activity_log"
    ADD CONSTRAINT "admin_activity_log_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."admin_users"
    ADD CONSTRAINT "admin_users_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."ai_clips"
    ADD CONSTRAINT "ai_clips_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."ai_editor_feedback"
    ADD CONSTRAINT "ai_editor_feedback_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."ai_scoring_cache"
    ADD CONSTRAINT "ai_scoring_cache_application_id_key" UNIQUE ("application_id");



ALTER TABLE ONLY "public"."ai_scoring_cache"
    ADD CONSTRAINT "ai_scoring_cache_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."api_usage"
    ADD CONSTRAINT "api_usage_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."application_events"
    ADD CONSTRAINT "application_events_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."conversion_events"
    ADD CONSTRAINT "conversion_events_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."conversion_pixels"
    ADD CONSTRAINT "conversion_pixels_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."conversion_pixels"
    ADD CONSTRAINT "conversion_pixels_user_id_provider_key" UNIQUE ("user_id", "provider");



ALTER TABLE ONLY "public"."coupons"
    ADD CONSTRAINT "coupons_code_key" UNIQUE ("code");



ALTER TABLE ONLY "public"."coupons"
    ADD CONSTRAINT "coupons_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."engineering_applications"
    ADD CONSTRAINT "engineering_applications_email_key" UNIQUE ("email");



ALTER TABLE ONLY "public"."engineering_applications"
    ADD CONSTRAINT "engineering_applications_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."hyperframes_skills"
    ADD CONSTRAINT "hyperframes_skills_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."inbox_items"
    ADD CONSTRAINT "inbox_items_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."inbox_items"
    ADD CONSTRAINT "inbox_items_user_external_platform_idx" UNIQUE ("user_id", "external_id", "source_platform");



ALTER TABLE ONLY "public"."lead_intelligence"
    ADD CONSTRAINT "lead_intelligence_lead_id_key" UNIQUE ("lead_id");



ALTER TABLE ONLY "public"."lead_intelligence"
    ADD CONSTRAINT "lead_intelligence_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."linked_accounts"
    ADD CONSTRAINT "linked_accounts_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."linked_accounts"
    ADD CONSTRAINT "linked_accounts_user_platform_unique" UNIQUE ("user_id", "platform");



ALTER TABLE ONLY "public"."member_account_access"
    ADD CONSTRAINT "member_account_access_member_id_platform_social_account_id_key" UNIQUE ("member_id", "platform", "social_account_id");



ALTER TABLE ONLY "public"."member_account_access"
    ADD CONSTRAINT "member_account_access_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."member_permissions"
    ADD CONSTRAINT "member_permissions_member_id_key" UNIQUE ("member_id");



ALTER TABLE ONLY "public"."member_permissions"
    ADD CONSTRAINT "member_permissions_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."notification_preferences"
    ADD CONSTRAINT "notification_preferences_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."notification_preferences"
    ADD CONSTRAINT "notification_preferences_user_id_key" UNIQUE ("user_id");



ALTER TABLE ONLY "public"."notification_types"
    ADD CONSTRAINT "notification_types_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_user_created_idx" UNIQUE ("user_id", "created_at");



ALTER TABLE ONLY "public"."organization_invites"
    ADD CONSTRAINT "organization_invites_organization_id_email_key" UNIQUE ("organization_id", "email");



ALTER TABLE ONLY "public"."organization_invites"
    ADD CONSTRAINT "organization_invites_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."organization_members"
    ADD CONSTRAINT "organization_members_organization_id_user_id_key" UNIQUE ("organization_id", "user_id");



ALTER TABLE ONLY "public"."organization_members"
    ADD CONSTRAINT "organization_members_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."organizations"
    ADD CONSTRAINT "organizations_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."pending_approvals"
    ADD CONSTRAINT "pending_approvals_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."plans"
    ADD CONSTRAINT "plans_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."plans"
    ADD CONSTRAINT "plans_slug_key" UNIQUE ("slug");



ALTER TABLE ONLY "public"."post_comments"
    ADD CONSTRAINT "post_comments_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."post_versions"
    ADD CONSTRAINT "post_versions_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."post_versions"
    ADD CONSTRAINT "post_versions_post_id_version_key" UNIQUE ("post_id", "version");



ALTER TABLE ONLY "public"."posts"
    ADD CONSTRAINT "posts_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."posts"
    ADD CONSTRAINT "posts_user_external_platform_idx" UNIQUE ("user_id", "external_id", "platform");



ALTER TABLE ONLY "public"."profiles"
    ADD CONSTRAINT "profiles_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."render_jobs"
    ADD CONSTRAINT "render_jobs_job_id_key" UNIQUE ("job_id");



ALTER TABLE ONLY "public"."render_jobs"
    ADD CONSTRAINT "render_jobs_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."scheduled_posts"
    ADD CONSTRAINT "scheduled_posts_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."scheduled_posts"
    ADD CONSTRAINT "scheduled_posts_post_id_key" UNIQUE ("post_id");



ALTER TABLE ONLY "public"."studio_projects"
    ADD CONSTRAINT "studio_projects_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."subscriptions"
    ADD CONSTRAINT "subscriptions_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."support_tickets"
    ADD CONSTRAINT "support_tickets_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."team_members"
    ADD CONSTRAINT "team_members_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."team_members"
    ADD CONSTRAINT "team_members_team_id_member_id_key" UNIQUE ("team_id", "member_id");



ALTER TABLE ONLY "public"."team_profiles"
    ADD CONSTRAINT "team_profiles_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."team_profiles"
    ADD CONSTRAINT "team_profiles_team_id_platform_key" UNIQUE ("team_id", "platform");



ALTER TABLE ONLY "public"."teams"
    ADD CONSTRAINT "teams_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."tedora_admins"
    ADD CONSTRAINT "tedora_admins_email_key" UNIQUE ("email");



ALTER TABLE ONLY "public"."tedora_admins"
    ADD CONSTRAINT "tedora_admins_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."tedora_insights"
    ADD CONSTRAINT "tedora_insights_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."tedora_interactions"
    ADD CONSTRAINT "tedora_interactions_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."tedora_leads"
    ADD CONSTRAINT "tedora_leads_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."tedora_projects"
    ADD CONSTRAINT "tedora_projects_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."tedora_proposals"
    ADD CONSTRAINT "tedora_proposals_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."transactions"
    ADD CONSTRAINT "transactions_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."trigger_debug_log"
    ADD CONSTRAINT "trigger_debug_log_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."user_assets"
    ADD CONSTRAINT "user_assets_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."user_insights"
    ADD CONSTRAINT "user_insights_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."user_insights"
    ADD CONSTRAINT "user_insights_user_id_insight_type_title_key" UNIQUE ("user_id", "insight_type", "title");



ALTER TABLE ONLY "public"."user_preferences"
    ADD CONSTRAINT "user_preferences_pkey" PRIMARY KEY ("user_id");



ALTER TABLE ONLY "public"."user_sessions"
    ADD CONSTRAINT "user_sessions_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."utm_links"
    ADD CONSTRAINT "utm_links_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."viral_videos"
    ADD CONSTRAINT "viral_videos_pkey" PRIMARY KEY ("id");



ALTER TABLE ONLY "public"."youtube_ingest_jobs"
    ADD CONSTRAINT "youtube_ingest_jobs_pkey" PRIMARY KEY ("id");



CREATE INDEX "idx_ai_clips_created_at" ON "public"."ai_clips" USING "btree" ("created_at" DESC);



CREATE INDEX "idx_ai_clips_portrait_reframe_status" ON "public"."ai_clips" USING "btree" ("portrait_reframe_status");



CREATE INDEX "idx_ai_clips_project_id" ON "public"."ai_clips" USING "btree" ("project_id");



CREATE INDEX "idx_ai_clips_project_status" ON "public"."ai_clips" USING "btree" ("project_id", "status");



CREATE INDEX "idx_ai_clips_status" ON "public"."ai_clips" USING "btree" ("status");



CREATE INDEX "idx_ai_clips_video_asset_id" ON "public"."ai_clips" USING "btree" ("video_asset_id");



CREATE INDEX "idx_applications_ai_score" ON "public"."engineering_applications" USING "btree" ("ai_overall_score" DESC NULLS LAST);



CREATE INDEX "idx_applications_email" ON "public"."engineering_applications" USING "btree" ("email");



CREATE INDEX "idx_applications_status" ON "public"."engineering_applications" USING "btree" ("status");



CREATE INDEX "idx_applications_submitted" ON "public"."engineering_applications" USING "btree" ("submitted_at" DESC);



CREATE INDEX "idx_conversion_events_campaign" ON "public"."conversion_events" USING "btree" ("campaign_id");



CREATE INDEX "idx_conversion_events_user" ON "public"."conversion_events" USING "btree" ("user_id");



CREATE INDEX "idx_conversion_events_utm" ON "public"."conversion_events" USING "btree" ("utm_campaign");



CREATE INDEX "idx_events_application" ON "public"."application_events" USING "btree" ("application_id", "created_at" DESC);



CREATE INDEX "idx_hyperframes_skills_category" ON "public"."hyperframes_skills" USING "btree" ("category");



CREATE INDEX "idx_hyperframes_skills_skill_name" ON "public"."hyperframes_skills" USING "btree" ("skill_name");



CREATE INDEX "idx_notification_preferences_user_id" ON "public"."notification_preferences" USING "btree" ("user_id");



CREATE INDEX "idx_notifications_user_id_created_at" ON "public"."notifications" USING "btree" ("user_id", "created_at" DESC);



CREATE INDEX "idx_notifications_user_id_read_at" ON "public"."notifications" USING "btree" ("user_id", "read_at");



CREATE INDEX "idx_notifications_user_id_type" ON "public"."notifications" USING "btree" ("user_id", "notification_type_id");



CREATE INDEX "idx_render_jobs_created_at" ON "public"."render_jobs" USING "btree" ("created_at" DESC);



CREATE INDEX "idx_render_jobs_job_id" ON "public"."render_jobs" USING "btree" ("job_id");



CREATE INDEX "idx_render_jobs_status" ON "public"."render_jobs" USING "btree" ("status");



CREATE INDEX "idx_render_jobs_user_id" ON "public"."render_jobs" USING "btree" ("user_id") WHERE ("user_id" IS NOT NULL);



CREATE INDEX "idx_user_preferences_user_id" ON "public"."user_preferences" USING "btree" ("user_id");



CREATE INDEX "inbox_items_assigned_to_idx" ON "public"."inbox_items" USING "btree" ("assigned_to");



CREATE INDEX "inbox_items_locked_by_idx" ON "public"."inbox_items" USING "btree" ("locked_by");



CREATE INDEX "member_account_access_member_idx" ON "public"."member_account_access" USING "btree" ("member_id");



CREATE INDEX "pending_approvals_org_status_idx" ON "public"."pending_approvals" USING "btree" ("organization_id", "status", "created_at" DESC);



CREATE INDEX "post_comments_post_idx" ON "public"."post_comments" USING "btree" ("post_id", "created_at" DESC);



CREATE INDEX "post_versions_post_idx" ON "public"."post_versions" USING "btree" ("post_id", "version" DESC);



CREATE INDEX "posts_org_scheduled_idx" ON "public"."posts" USING "btree" ("organization_id", "scheduled_at" DESC NULLS LAST);



CREATE INDEX "posts_team_workflow_idx" ON "public"."posts" USING "btree" ("team_id", "workflow_status");



CREATE INDEX "studio_projects_active_processing_idx" ON "public"."studio_projects" USING "btree" ("id", "updated_at" DESC) WHERE ("processing_metadata" IS NOT NULL);



CREATE INDEX "studio_projects_ai_analysis_idx" ON "public"."studio_projects" USING "btree" ("ai_analysis_complete") WHERE ("ai_analysis_complete" = false);



CREATE INDEX "studio_projects_failed_idx" ON "public"."studio_projects" USING "btree" ("status", "error_details") WHERE (("status" = 'FAILED'::"text") AND ("error_details" IS NOT NULL));



CREATE INDEX "studio_projects_processing_metadata_idx" ON "public"."studio_projects" USING "gin" ("processing_metadata") WHERE ("processing_metadata" IS NOT NULL);



CREATE INDEX "studio_projects_status_idx" ON "public"."studio_projects" USING "btree" ("status", "updated_at" DESC);



CREATE INDEX "studio_projects_videodb_asset_idx" ON "public"."studio_projects" USING "btree" ("videodb_asset_id");



CREATE OR REPLACE TRIGGER "studio_projects_updated_at_trigger" BEFORE UPDATE ON "public"."studio_projects" FOR EACH ROW EXECUTE FUNCTION "public"."update_studio_projects_updated_at"();



CREATE OR REPLACE TRIGGER "sync_scheduled_posts_from_posts" AFTER INSERT OR UPDATE ON "public"."posts" FOR EACH ROW EXECUTE FUNCTION "public"."sync_scheduled_posts_from_posts"();



CREATE OR REPLACE TRIGGER "trg_sync_post_comment_count" AFTER INSERT OR DELETE OR UPDATE ON "public"."post_comments" FOR EACH ROW EXECUTE FUNCTION "public"."sync_post_comment_count"();



CREATE OR REPLACE TRIGGER "trigger_create_notification_preferences" AFTER INSERT ON "public"."profiles" FOR EACH ROW EXECUTE FUNCTION "public"."create_notification_preferences_for_new_user"();



CREATE OR REPLACE TRIGGER "trigger_log_application_event" AFTER INSERT OR UPDATE ON "public"."engineering_applications" FOR EACH ROW EXECUTE FUNCTION "public"."log_application_event"();



CREATE OR REPLACE TRIGGER "trigger_update_ai_clips_updated_at" BEFORE UPDATE ON "public"."ai_clips" FOR EACH ROW EXECUTE FUNCTION "public"."update_ai_clips_updated_at"();



CREATE OR REPLACE TRIGGER "trigger_update_application_timestamp" BEFORE UPDATE ON "public"."engineering_applications" FOR EACH ROW EXECUTE FUNCTION "public"."update_application_timestamp"();



CREATE OR REPLACE TRIGGER "update_render_jobs_updated_at_trigger" BEFORE UPDATE ON "public"."render_jobs" FOR EACH ROW EXECUTE FUNCTION "public"."update_render_jobs_updated_at"();



CREATE OR REPLACE TRIGGER "update_tedora_leads_modtime" BEFORE UPDATE ON "public"."tedora_leads" FOR EACH ROW EXECUTE FUNCTION "public"."update_updated_at_column"();



ALTER TABLE ONLY "public"."ad_accounts"
    ADD CONSTRAINT "ad_accounts_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."ad_campaigns"
    ADD CONSTRAINT "ad_campaigns_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."admin_activity_log"
    ADD CONSTRAINT "admin_activity_log_admin_id_fkey" FOREIGN KEY ("admin_id") REFERENCES "public"."admin_users"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."admin_users"
    ADD CONSTRAINT "admin_users_id_fkey" FOREIGN KEY ("id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."ai_clips"
    ADD CONSTRAINT "ai_clips_project_id_fkey" FOREIGN KEY ("project_id") REFERENCES "public"."studio_projects"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."ai_editor_feedback"
    ADD CONSTRAINT "ai_editor_feedback_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."ai_scoring_cache"
    ADD CONSTRAINT "ai_scoring_cache_application_id_fkey" FOREIGN KEY ("application_id") REFERENCES "public"."engineering_applications"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."api_usage"
    ADD CONSTRAINT "api_usage_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."application_events"
    ADD CONSTRAINT "application_events_application_id_fkey" FOREIGN KEY ("application_id") REFERENCES "public"."engineering_applications"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."conversion_events"
    ADD CONSTRAINT "conversion_events_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."conversion_pixels"
    ADD CONSTRAINT "conversion_pixels_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."inbox_items"
    ADD CONSTRAINT "inbox_items_assigned_to_fkey" FOREIGN KEY ("assigned_to") REFERENCES "public"."organization_members"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."inbox_items"
    ADD CONSTRAINT "inbox_items_locked_by_fkey" FOREIGN KEY ("locked_by") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."inbox_items"
    ADD CONSTRAINT "inbox_items_organization_id_fkey" FOREIGN KEY ("organization_id") REFERENCES "public"."organizations"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."inbox_items"
    ADD CONSTRAINT "inbox_items_team_id_fkey" FOREIGN KEY ("team_id") REFERENCES "public"."teams"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."inbox_items"
    ADD CONSTRAINT "inbox_items_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."lead_intelligence"
    ADD CONSTRAINT "lead_intelligence_lead_id_fkey" FOREIGN KEY ("lead_id") REFERENCES "public"."tedora_leads"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."linked_accounts"
    ADD CONSTRAINT "linked_accounts_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."member_account_access"
    ADD CONSTRAINT "member_account_access_member_id_fkey" FOREIGN KEY ("member_id") REFERENCES "public"."organization_members"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."member_permissions"
    ADD CONSTRAINT "member_permissions_member_id_fkey" FOREIGN KEY ("member_id") REFERENCES "public"."organization_members"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."notification_preferences"
    ADD CONSTRAINT "notification_preferences_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_notification_type_id_fkey" FOREIGN KEY ("notification_type_id") REFERENCES "public"."notification_types"("id") ON DELETE RESTRICT;



ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."organization_invites"
    ADD CONSTRAINT "organization_invites_invited_by_fkey" FOREIGN KEY ("invited_by") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."organization_invites"
    ADD CONSTRAINT "organization_invites_organization_id_fkey" FOREIGN KEY ("organization_id") REFERENCES "public"."organizations"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."organization_members"
    ADD CONSTRAINT "organization_members_organization_id_fkey" FOREIGN KEY ("organization_id") REFERENCES "public"."organizations"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."organization_members"
    ADD CONSTRAINT "organization_members_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."organizations"
    ADD CONSTRAINT "organizations_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."pending_approvals"
    ADD CONSTRAINT "pending_approvals_organization_id_fkey" FOREIGN KEY ("organization_id") REFERENCES "public"."organizations"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."pending_approvals"
    ADD CONSTRAINT "pending_approvals_post_id_fkey" FOREIGN KEY ("post_id") REFERENCES "public"."posts"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."pending_approvals"
    ADD CONSTRAINT "pending_approvals_reviewed_by_fkey" FOREIGN KEY ("reviewed_by") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."pending_approvals"
    ADD CONSTRAINT "pending_approvals_submitted_by_fkey" FOREIGN KEY ("submitted_by") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."pending_approvals"
    ADD CONSTRAINT "pending_approvals_team_id_fkey" FOREIGN KEY ("team_id") REFERENCES "public"."teams"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."post_comments"
    ADD CONSTRAINT "post_comments_author_user_id_fkey" FOREIGN KEY ("author_user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."post_comments"
    ADD CONSTRAINT "post_comments_post_id_fkey" FOREIGN KEY ("post_id") REFERENCES "public"."posts"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."post_versions"
    ADD CONSTRAINT "post_versions_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."post_versions"
    ADD CONSTRAINT "post_versions_post_id_fkey" FOREIGN KEY ("post_id") REFERENCES "public"."posts"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."posts"
    ADD CONSTRAINT "posts_organization_id_fkey" FOREIGN KEY ("organization_id") REFERENCES "public"."organizations"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."posts"
    ADD CONSTRAINT "posts_team_id_fkey" FOREIGN KEY ("team_id") REFERENCES "public"."teams"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."posts"
    ADD CONSTRAINT "posts_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."profiles"
    ADD CONSTRAINT "profiles_id_fkey" FOREIGN KEY ("id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."render_jobs"
    ADD CONSTRAINT "render_jobs_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."scheduled_posts"
    ADD CONSTRAINT "scheduled_posts_post_id_fkey" FOREIGN KEY ("post_id") REFERENCES "public"."posts"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."scheduled_posts"
    ADD CONSTRAINT "scheduled_posts_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."studio_projects"
    ADD CONSTRAINT "studio_projects_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."subscriptions"
    ADD CONSTRAINT "subscriptions_plan_id_fkey" FOREIGN KEY ("plan_id") REFERENCES "public"."plans"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."subscriptions"
    ADD CONSTRAINT "subscriptions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."support_tickets"
    ADD CONSTRAINT "support_tickets_assigned_to_fkey" FOREIGN KEY ("assigned_to") REFERENCES "public"."admin_users"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."support_tickets"
    ADD CONSTRAINT "support_tickets_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."team_members"
    ADD CONSTRAINT "team_members_member_id_fkey" FOREIGN KEY ("member_id") REFERENCES "public"."organization_members"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."team_members"
    ADD CONSTRAINT "team_members_team_id_fkey" FOREIGN KEY ("team_id") REFERENCES "public"."teams"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."team_profiles"
    ADD CONSTRAINT "team_profiles_team_id_fkey" FOREIGN KEY ("team_id") REFERENCES "public"."teams"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."teams"
    ADD CONSTRAINT "teams_organization_id_fkey" FOREIGN KEY ("organization_id") REFERENCES "public"."organizations"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."tedora_interactions"
    ADD CONSTRAINT "tedora_interactions_lead_id_fkey" FOREIGN KEY ("lead_id") REFERENCES "public"."tedora_leads"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."tedora_proposals"
    ADD CONSTRAINT "tedora_proposals_lead_id_fkey" FOREIGN KEY ("lead_id") REFERENCES "public"."tedora_leads"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."transactions"
    ADD CONSTRAINT "transactions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE SET NULL;



ALTER TABLE ONLY "public"."user_assets"
    ADD CONSTRAINT "user_assets_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."user_insights"
    ADD CONSTRAINT "user_insights_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."user_preferences"
    ADD CONSTRAINT "user_preferences_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."user_sessions"
    ADD CONSTRAINT "user_sessions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."utm_links"
    ADD CONSTRAINT "utm_links_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."profiles"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."youtube_ingest_jobs"
    ADD CONSTRAINT "youtube_ingest_jobs_project_id_fkey" FOREIGN KEY ("project_id") REFERENCES "public"."studio_projects"("id") ON DELETE CASCADE;



ALTER TABLE ONLY "public"."youtube_ingest_jobs"
    ADD CONSTRAINT "youtube_ingest_jobs_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "auth"."users"("id") ON DELETE CASCADE;



CREATE POLICY "Admin Full Access" ON "public"."tedora_proposals" USING (("auth"."role"() = 'authenticated'::"text"));



CREATE POLICY "Admin read admin_users" ON "public"."admin_users" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read api_usage" ON "public"."api_usage" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read coupons" ON "public"."coupons" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read plans" ON "public"."plans" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read profiles" ON "public"."profiles" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read render_jobs" ON "public"."render_jobs" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read subscriptions" ON "public"."subscriptions" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read support_tickets" ON "public"."support_tickets" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read transactions" ON "public"."transactions" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admin read user_sessions" ON "public"."user_sessions" FOR SELECT TO "authenticated" USING (true);



CREATE POLICY "Admins can manage coupons" ON "public"."coupons" TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can manage plans" ON "public"."plans" TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can manage subscriptions" ON "public"."subscriptions" TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can manage tickets" ON "public"."support_tickets" TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can update all profiles" ON "public"."profiles" FOR UPDATE TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can update own profile" ON "public"."tedora_admins" FOR UPDATE USING ((("auth"."jwt"() ->> 'email'::"text") = "email"));



CREATE POLICY "Admins can view activity log" ON "public"."admin_activity_log" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all ai_clips" ON "public"."ai_clips" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all api_usage" ON "public"."api_usage" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all inbox" ON "public"."inbox_items" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all leads" ON "public"."tedora_leads" FOR SELECT USING ((("auth"."jwt"() ->> 'email'::"text") IN ( SELECT "tedora_admins"."email"
   FROM "public"."tedora_admins")));



CREATE POLICY "Admins can view all linked_accounts" ON "public"."linked_accounts" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all notifications" ON "public"."notifications" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all posts" ON "public"."posts" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all profiles" ON "public"."profiles" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all projects" ON "public"."studio_projects" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all render_jobs" ON "public"."render_jobs" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all scheduled posts" ON "public"."scheduled_posts" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all subscriptions" ON "public"."subscriptions" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all tickets" ON "public"."support_tickets" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all transactions" ON "public"."transactions" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view all user_sessions" ON "public"."user_sessions" FOR SELECT TO "authenticated" USING ((EXISTS ( SELECT 1
   FROM "public"."admin_users"
  WHERE ("admin_users"."id" = "auth"."uid"()))));



CREATE POLICY "Admins can view and edit intelligence" ON "public"."lead_intelligence" USING ("public"."is_admin"());



CREATE POLICY "Admins can view and edit leads" ON "public"."tedora_leads" USING ("public"."is_admin"());



CREATE POLICY "Admins can view and edit proposals" ON "public"."tedora_proposals" USING ("public"."is_admin"());



CREATE POLICY "Admins can view and log interactions" ON "public"."tedora_interactions" USING ("public"."is_admin"());



CREATE POLICY "Admins can view own profile" ON "public"."tedora_admins" FOR SELECT USING ((("auth"."jwt"() ->> 'email'::"text") = "email"));



CREATE POLICY "Allow admins to manage leads" ON "public"."tedora_leads" USING (("auth"."role"() = 'authenticated'::"text"));



CREATE POLICY "Allow all profile operations" ON "public"."profiles" TO "authenticated", "anon", "service_role" USING (true) WITH CHECK (true);



CREATE POLICY "Allow public read access for verification" ON "public"."tedora_admins" FOR SELECT TO "anon" USING (true);



CREATE POLICY "Allow public read access to admins" ON "public"."tedora_admins" FOR SELECT USING (true);



CREATE POLICY "Allow public read-only access to insights" ON "public"."tedora_insights" FOR SELECT USING (true);



CREATE POLICY "Allow public read-only access to projects" ON "public"."tedora_projects" FOR SELECT USING (true);



CREATE POLICY "Allow public to submit inquiries" ON "public"."tedora_leads" FOR INSERT WITH CHECK (true);



CREATE POLICY "Anon can insert leads" ON "public"."tedora_leads" FOR INSERT WITH CHECK (true);



CREATE POLICY "Anyone can delete applications" ON "public"."engineering_applications" FOR DELETE TO "authenticated", "anon" USING (true);



CREATE POLICY "Anyone can read applications" ON "public"."engineering_applications" FOR SELECT TO "authenticated", "anon" USING (true);



CREATE POLICY "Anyone can submit applications" ON "public"."engineering_applications" FOR INSERT TO "authenticated", "anon" WITH CHECK (true);



CREATE POLICY "Anyone can update applications" ON "public"."engineering_applications" FOR UPDATE TO "authenticated", "anon" USING (true) WITH CHECK (true);



CREATE POLICY "Anyone can view active coupons" ON "public"."coupons" FOR SELECT TO "authenticated", "anon" USING (("is_active" = true));



CREATE POLICY "Anyone can view organizations they belong to" ON "public"."organizations" FOR SELECT USING ("public"."is_org_member_check"("id"));



CREATE POLICY "Anyone can view plans" ON "public"."plans" FOR SELECT TO "authenticated", "anon" USING (true);



CREATE POLICY "Authenticated users can manage viral videos" ON "public"."viral_videos" USING (("auth"."role"() = 'authenticated'::"text"));



CREATE POLICY "Authors or admins update post comments" ON "public"."post_comments" FOR UPDATE USING ((("author_user_id" = "auth"."uid"()) OR (EXISTS ( SELECT 1
   FROM "public"."posts" "p"
  WHERE (("p"."id" = "post_comments"."post_id") AND ("p"."organization_id" IS NOT NULL) AND "public"."is_org_admin_check"("p"."organization_id"))))));



CREATE POLICY "Manage invites of own organization" ON "public"."organization_invites" USING ("public"."is_org_admin_check"("organization_id"));



CREATE POLICY "Manage member account access of own organization" ON "public"."member_account_access" USING ((EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."id" = "member_account_access"."member_id") AND "public"."is_org_admin_check"("om"."organization_id"))))) WITH CHECK ((EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."id" = "member_account_access"."member_id") AND "public"."is_org_admin_check"("om"."organization_id")))));



CREATE POLICY "Manage members of own organization" ON "public"."organization_members" USING ("public"."is_org_admin_check"("organization_id"));



CREATE POLICY "Manage permissions of own organization" ON "public"."member_permissions" USING ((EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."id" = "member_permissions"."member_id") AND "public"."is_org_admin_check"("om"."organization_id")))));



CREATE POLICY "Manage team members of own organization" ON "public"."team_members" USING ((EXISTS ( SELECT 1
   FROM "public"."teams" "t"
  WHERE (("t"."id" = "team_members"."team_id") AND "public"."is_org_admin_check"("t"."organization_id")))));



CREATE POLICY "Manage team profiles of own organization" ON "public"."team_profiles" USING ((EXISTS ( SELECT 1
   FROM "public"."teams" "t"
  WHERE (("t"."id" = "team_profiles"."team_id") AND "public"."is_org_admin_check"("t"."organization_id")))));



CREATE POLICY "Manage teams of own organization" ON "public"."teams" USING ("public"."is_org_admin_check"("organization_id"));



CREATE POLICY "Members can update their organizations" ON "public"."organizations" FOR UPDATE USING ("public"."is_org_admin_check"("id")) WITH CHECK ("public"."is_org_admin_check"("id"));



CREATE POLICY "Org members can view co-member profiles" ON "public"."profiles" FOR SELECT USING ((("auth"."uid"() = "id") OR (EXISTS ( SELECT 1
   FROM ("public"."organization_members" "viewer"
     JOIN "public"."organization_members" "subject" ON (("subject"."organization_id" = "viewer"."organization_id")))
  WHERE (("viewer"."user_id" = "auth"."uid"()) AND ("subject"."user_id" = "profiles"."id"))))));



CREATE POLICY "Org members insert post comments" ON "public"."post_comments" FOR INSERT WITH CHECK ((("author_user_id" = "auth"."uid"()) AND (EXISTS ( SELECT 1
   FROM "public"."posts" "p"
  WHERE (("p"."id" = "post_comments"."post_id") AND (("p"."user_id" = "auth"."uid"()) OR (("p"."organization_id" IS NOT NULL) AND "public"."is_org_member_check"("p"."organization_id"))))))));



CREATE POLICY "Org members insert post versions" ON "public"."post_versions" FOR INSERT WITH CHECK ((EXISTS ( SELECT 1
   FROM "public"."posts" "p"
  WHERE (("p"."id" = "post_versions"."post_id") AND (("p"."user_id" = "auth"."uid"()) OR (("p"."organization_id" IS NOT NULL) AND "public"."is_org_member_check"("p"."organization_id")))))));



CREATE POLICY "Org members insert team posts" ON "public"."posts" FOR INSERT WITH CHECK ((("user_id" = "auth"."uid"()) AND (("organization_id" IS NULL) OR "public"."is_org_member_check"("organization_id"))));



CREATE POLICY "Org members manage shared inbox items" ON "public"."inbox_items" USING ((("auth"."uid"() = "user_id") OR (("organization_id" IS NOT NULL) AND (EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."organization_id" = "inbox_items"."organization_id") AND ("om"."user_id" = "auth"."uid"())))))));



CREATE POLICY "Org members submit pending approvals" ON "public"."pending_approvals" FOR INSERT WITH CHECK ((("submitted_by" = "auth"."uid"()) AND (EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."organization_id" = "pending_approvals"."organization_id") AND ("om"."user_id" = "auth"."uid"()))))));



CREATE POLICY "Org members update team posts" ON "public"."posts" FOR UPDATE USING ((("organization_id" IS NOT NULL) AND "public"."is_org_member_check"("organization_id"))) WITH CHECK ((("organization_id" IS NOT NULL) AND "public"."is_org_member_check"("organization_id")));



CREATE POLICY "Org members view pending approvals" ON "public"."pending_approvals" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."organization_id" = "pending_approvals"."organization_id") AND ("om"."user_id" = "auth"."uid"())))));



CREATE POLICY "Org members view post comments" ON "public"."post_comments" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."posts" "p"
  WHERE (("p"."id" = "post_comments"."post_id") AND (("p"."user_id" = "auth"."uid"()) OR (("p"."organization_id" IS NOT NULL) AND "public"."is_org_member_check"("p"."organization_id")))))));



CREATE POLICY "Org members view post versions" ON "public"."post_versions" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."posts" "p"
  WHERE (("p"."id" = "post_versions"."post_id") AND (("p"."user_id" = "auth"."uid"()) OR (("p"."organization_id" IS NOT NULL) AND "public"."is_org_member_check"("p"."organization_id")))))));



CREATE POLICY "Org members view shared inbox items" ON "public"."inbox_items" FOR SELECT USING ((("auth"."uid"() = "user_id") OR (("organization_id" IS NOT NULL) AND (EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."organization_id" = "inbox_items"."organization_id") AND ("om"."user_id" = "auth"."uid"())))))));



CREATE POLICY "Org members view team posts" ON "public"."posts" FOR SELECT USING ((("organization_id" IS NOT NULL) AND "public"."is_org_member_check"("organization_id")));



CREATE POLICY "Public Read Proposal" ON "public"."tedora_proposals" FOR SELECT USING (true);



CREATE POLICY "Public Sign Proposal" ON "public"."tedora_proposals" FOR UPDATE USING (("status" = 'sent'::"text")) WITH CHECK (("status" = 'signed'::"text"));



CREATE POLICY "Public can view viral videos" ON "public"."viral_videos" FOR SELECT USING (true);



CREATE POLICY "Reviewers manage pending approvals" ON "public"."pending_approvals" FOR UPDATE USING ((EXISTS ( SELECT 1
   FROM ("public"."organization_members" "om"
     JOIN "public"."member_permissions" "mp" ON (("mp"."member_id" = "om"."id")))
  WHERE (("om"."organization_id" = "pending_approvals"."organization_id") AND ("om"."user_id" = "auth"."uid"()) AND (("om"."role" = ANY (ARRAY['Super Admin'::"text", 'Admin'::"text"])) OR ("mp"."permission_level" = ANY (ARRAY['Editor'::"text", 'Advanced'::"text"])))))));



CREATE POLICY "Service role can insert events" ON "public"."conversion_events" FOR INSERT WITH CHECK (true);



CREATE POLICY "Service role can manage ai_clips" ON "public"."ai_clips" USING (("auth"."role"() = 'service_role'::"text")) WITH CHECK (("auth"."role"() = 'service_role'::"text"));



CREATE POLICY "Service role full access" ON "public"."render_jobs" USING (("auth"."role"() = 'service_role'::"text"));



CREATE POLICY "Service role full access on youtube ingest jobs" ON "public"."youtube_ingest_jobs" USING (true);



CREATE POLICY "Users can create tickets" ON "public"."support_tickets" FOR INSERT TO "authenticated" WITH CHECK (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can delete clips from their projects" ON "public"."ai_clips" FOR DELETE TO "authenticated" USING (("project_id" IN ( SELECT "studio_projects"."id"
   FROM "public"."studio_projects"
  WHERE ("studio_projects"."user_id" = "auth"."uid"()))));



CREATE POLICY "Users can delete own insights" ON "public"."user_insights" FOR DELETE USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can delete their own assets" ON "public"."user_assets" FOR DELETE USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can insert own insights" ON "public"."user_insights" FOR INSERT WITH CHECK (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can insert their own AI feedback" ON "public"."ai_editor_feedback" FOR INSERT WITH CHECK (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can insert their own assets" ON "public"."user_assets" FOR INSERT WITH CHECK (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage own pixels" ON "public"."conversion_pixels" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage own scheduled posts" ON "public"."scheduled_posts" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage own utm_links" ON "public"."utm_links" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage their own accounts" ON "public"."linked_accounts" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage their own ad accounts" ON "public"."ad_accounts" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage their own ad campaigns" ON "public"."ad_campaigns" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage their own inbox items" ON "public"."inbox_items" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage their own posts" ON "public"."posts" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage their own preferences" ON "public"."user_preferences" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can manage their own studio projects" ON "public"."studio_projects" USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can update own insights" ON "public"."user_insights" FOR UPDATE USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can update own profile" ON "public"."profiles" FOR UPDATE TO "authenticated" USING (("id" = "auth"."uid"())) WITH CHECK (("id" = "auth"."uid"()));



CREATE POLICY "Users can verify their own admin status" ON "public"."tedora_admins" FOR SELECT USING ((("auth"."jwt"() ->> 'email'::"text") = "email"));



CREATE POLICY "Users can view clips from their projects" ON "public"."ai_clips" FOR SELECT USING (("project_id" IN ( SELECT "studio_projects"."id"
   FROM "public"."studio_projects"
  WHERE ("studio_projects"."user_id" = "auth"."uid"()))));



CREATE POLICY "Users can view own api_usage" ON "public"."api_usage" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own events" ON "public"."conversion_events" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view own inbox" ON "public"."inbox_items" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own insights" ON "public"."user_insights" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view own linked_accounts" ON "public"."linked_accounts" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own notifications" ON "public"."notifications" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own pixels" ON "public"."conversion_pixels" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view own posts" ON "public"."posts" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own profile" ON "public"."profiles" FOR SELECT TO "authenticated", "anon" USING (("id" = "auth"."uid"()));



CREATE POLICY "Users can view own projects" ON "public"."studio_projects" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own render_jobs" ON "public"."render_jobs" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own renders" ON "public"."render_jobs" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own scheduled posts" ON "public"."scheduled_posts" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view own sessions" ON "public"."user_sessions" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own subscription" ON "public"."subscriptions" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own subscriptions" ON "public"."subscriptions" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own tickets" ON "public"."support_tickets" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own transactions" ON "public"."transactions" FOR SELECT TO "authenticated" USING (("user_id" = "auth"."uid"()));



CREATE POLICY "Users can view own utm_links" ON "public"."utm_links" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own AI feedback" ON "public"."ai_editor_feedback" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own accounts" ON "public"."linked_accounts" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own ad accounts" ON "public"."ad_accounts" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own ad campaigns" ON "public"."ad_campaigns" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own assets" ON "public"."user_assets" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own inbox items" ON "public"."inbox_items" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own posts" ON "public"."posts" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own preferences" ON "public"."user_preferences" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "Users can view their own render jobs" ON "public"."render_jobs" FOR SELECT USING ((("user_id" = "auth"."uid"()) OR ("user_id" IS NULL)));



CREATE POLICY "Users can view their own youtube ingest jobs" ON "public"."youtube_ingest_jobs" FOR SELECT USING ((("user_id" = "auth"."uid"()) OR ("user_id" IS NULL)));



CREATE POLICY "View invites of own organization" ON "public"."organization_invites" FOR SELECT USING ("public"."is_org_member_check"("organization_id"));



CREATE POLICY "View member account access of own organization" ON "public"."member_account_access" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."id" = "member_account_access"."member_id") AND "public"."is_org_member_check"("om"."organization_id")))));



CREATE POLICY "View members of own organization" ON "public"."organization_members" FOR SELECT USING ((("user_id" = "auth"."uid"()) OR "public"."is_org_member_check"("organization_id")));



CREATE POLICY "View permissions of own organization" ON "public"."member_permissions" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."organization_members" "om"
  WHERE (("om"."id" = "member_permissions"."member_id") AND "public"."is_org_member_check"("om"."organization_id")))));



CREATE POLICY "View team members of own organization" ON "public"."team_members" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."teams" "t"
  WHERE (("t"."id" = "team_members"."team_id") AND "public"."is_org_member_check"("t"."organization_id")))));



CREATE POLICY "View team profiles of own organization" ON "public"."team_profiles" FOR SELECT USING ((EXISTS ( SELECT 1
   FROM "public"."teams" "t"
  WHERE (("t"."id" = "team_profiles"."team_id") AND "public"."is_org_member_check"("t"."organization_id")))));



CREATE POLICY "View teams of own organization" ON "public"."teams" FOR SELECT USING ("public"."is_org_member_check"("organization_id"));



ALTER TABLE "public"."ad_accounts" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."ad_campaigns" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."admin_activity_log" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."admin_users" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."ai_clips" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."ai_editor_feedback" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."ai_scoring_cache" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."api_usage" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."application_events" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."conversion_events" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."conversion_pixels" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."coupons" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."engineering_applications" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."hyperframes_skills" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."inbox_items" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."lead_intelligence" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."linked_accounts" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."member_account_access" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."member_permissions" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."notification_preferences" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "notification_preferences_insert_own" ON "public"."notification_preferences" FOR INSERT WITH CHECK (("auth"."uid"() = "user_id"));



CREATE POLICY "notification_preferences_select_own" ON "public"."notification_preferences" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "notification_preferences_update_own" ON "public"."notification_preferences" FOR UPDATE USING (("auth"."uid"() = "user_id")) WITH CHECK (("auth"."uid"() = "user_id"));



ALTER TABLE "public"."notification_types" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "notification_types_select_public" ON "public"."notification_types" FOR SELECT USING (true);



ALTER TABLE "public"."notifications" ENABLE ROW LEVEL SECURITY;


CREATE POLICY "notifications_delete_own" ON "public"."notifications" FOR DELETE USING (("auth"."uid"() = "user_id"));



CREATE POLICY "notifications_insert_own" ON "public"."notifications" FOR INSERT WITH CHECK (("auth"."uid"() = "user_id"));



CREATE POLICY "notifications_select_own" ON "public"."notifications" FOR SELECT USING (("auth"."uid"() = "user_id"));



CREATE POLICY "notifications_update_own" ON "public"."notifications" FOR UPDATE USING (("auth"."uid"() = "user_id")) WITH CHECK (("auth"."uid"() = "user_id"));



ALTER TABLE "public"."organization_invites" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."organization_members" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."organizations" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."pending_approvals" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."plans" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."post_comments" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."post_versions" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."posts" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."profiles" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."render_jobs" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."scheduled_posts" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."studio_projects" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."subscriptions" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."support_tickets" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."team_members" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."team_profiles" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."teams" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."tedora_admins" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."tedora_insights" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."tedora_interactions" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."tedora_leads" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."tedora_projects" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."tedora_proposals" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."transactions" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."user_assets" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."user_insights" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."user_preferences" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."user_sessions" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."utm_links" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."viral_videos" ENABLE ROW LEVEL SECURITY;


ALTER TABLE "public"."youtube_ingest_jobs" ENABLE ROW LEVEL SECURITY;




ALTER PUBLICATION "supabase_realtime" OWNER TO "postgres";






ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."ad_accounts";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."ad_campaigns";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."conversion_events";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."conversion_pixels";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."inbox_items";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."linked_accounts";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."member_permissions";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."organization_invites";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."organization_members";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."organizations";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."pending_approvals";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."posts";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."profiles";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."render_jobs";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."scheduled_posts";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."studio_projects";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."team_members";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."team_profiles";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."teams";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."utm_links";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."viral_videos";



ALTER PUBLICATION "supabase_realtime" ADD TABLE ONLY "public"."youtube_ingest_jobs";






GRANT USAGE ON SCHEMA "public" TO "postgres";
GRANT USAGE ON SCHEMA "public" TO "anon";
GRANT USAGE ON SCHEMA "public" TO "authenticated";
GRANT USAGE ON SCHEMA "public" TO "service_role";






GRANT ALL ON FUNCTION "public"."halfvec_in"("cstring", "oid", integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_in"("cstring", "oid", integer) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_in"("cstring", "oid", integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_in"("cstring", "oid", integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_out"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_out"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_out"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_out"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_recv"("internal", "oid", integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_recv"("internal", "oid", integer) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_recv"("internal", "oid", integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_recv"("internal", "oid", integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_send"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_send"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_send"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_send"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_typmod_in"("cstring"[]) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_typmod_in"("cstring"[]) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_typmod_in"("cstring"[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_typmod_in"("cstring"[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_in"("cstring", "oid", integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_in"("cstring", "oid", integer) TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_in"("cstring", "oid", integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_in"("cstring", "oid", integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_out"("public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_out"("public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_out"("public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_out"("public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_recv"("internal", "oid", integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_recv"("internal", "oid", integer) TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_recv"("internal", "oid", integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_recv"("internal", "oid", integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_send"("public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_send"("public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_send"("public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_send"("public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_typmod_in"("cstring"[]) TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_typmod_in"("cstring"[]) TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_typmod_in"("cstring"[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_typmod_in"("cstring"[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_in"("cstring", "oid", integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_in"("cstring", "oid", integer) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_in"("cstring", "oid", integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_in"("cstring", "oid", integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_out"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_out"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_out"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_out"("public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_recv"("internal", "oid", integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_recv"("internal", "oid", integer) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_recv"("internal", "oid", integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_recv"("internal", "oid", integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_send"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_send"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_send"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_send"("public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_typmod_in"("cstring"[]) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_typmod_in"("cstring"[]) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_typmod_in"("cstring"[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_typmod_in"("cstring"[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_halfvec"(real[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(real[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(real[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(real[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(real[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(real[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(real[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(real[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_vector"(real[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_vector"(real[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_vector"(real[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_vector"(real[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_halfvec"(double precision[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(double precision[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(double precision[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(double precision[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(double precision[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(double precision[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(double precision[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(double precision[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_vector"(double precision[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_vector"(double precision[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_vector"(double precision[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_vector"(double precision[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_halfvec"(integer[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(integer[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(integer[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(integer[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(integer[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(integer[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(integer[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(integer[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_vector"(integer[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_vector"(integer[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_vector"(integer[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_vector"(integer[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_halfvec"(numeric[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(numeric[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(numeric[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_halfvec"(numeric[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(numeric[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(numeric[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(numeric[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_sparsevec"(numeric[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."array_to_vector"(numeric[], integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."array_to_vector"(numeric[], integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."array_to_vector"(numeric[], integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."array_to_vector"(numeric[], integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_to_float4"("public"."halfvec", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_to_float4"("public"."halfvec", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_to_float4"("public"."halfvec", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_to_float4"("public"."halfvec", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec"("public"."halfvec", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec"("public"."halfvec", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec"("public"."halfvec", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec"("public"."halfvec", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_to_sparsevec"("public"."halfvec", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_to_sparsevec"("public"."halfvec", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_to_sparsevec"("public"."halfvec", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_to_sparsevec"("public"."halfvec", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_to_vector"("public"."halfvec", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_to_vector"("public"."halfvec", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_to_vector"("public"."halfvec", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_to_vector"("public"."halfvec", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_to_halfvec"("public"."sparsevec", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_to_halfvec"("public"."sparsevec", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_to_halfvec"("public"."sparsevec", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_to_halfvec"("public"."sparsevec", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec"("public"."sparsevec", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec"("public"."sparsevec", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec"("public"."sparsevec", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec"("public"."sparsevec", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_to_vector"("public"."sparsevec", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_to_vector"("public"."sparsevec", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_to_vector"("public"."sparsevec", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_to_vector"("public"."sparsevec", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_to_float4"("public"."vector", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_to_float4"("public"."vector", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_to_float4"("public"."vector", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_to_float4"("public"."vector", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_to_halfvec"("public"."vector", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_to_halfvec"("public"."vector", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_to_halfvec"("public"."vector", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_to_halfvec"("public"."vector", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_to_sparsevec"("public"."vector", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_to_sparsevec"("public"."vector", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_to_sparsevec"("public"."vector", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_to_sparsevec"("public"."vector", integer, boolean) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector"("public"."vector", integer, boolean) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector"("public"."vector", integer, boolean) TO "anon";
GRANT ALL ON FUNCTION "public"."vector"("public"."vector", integer, boolean) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector"("public"."vector", integer, boolean) TO "service_role";











































































































































































GRANT ALL ON FUNCTION "public"."accept_pending_organization_invites"() TO "anon";
GRANT ALL ON FUNCTION "public"."accept_pending_organization_invites"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."accept_pending_organization_invites"() TO "service_role";



GRANT ALL ON TABLE "public"."inbox_items" TO "anon";
GRANT ALL ON TABLE "public"."inbox_items" TO "authenticated";
GRANT ALL ON TABLE "public"."inbox_items" TO "service_role";



GRANT ALL ON FUNCTION "public"."acquire_inbox_lock"("p_inbox_item_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."acquire_inbox_lock"("p_inbox_item_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."acquire_inbox_lock"("p_inbox_item_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."assign_inbox_item"("p_inbox_item_id" "uuid", "p_member_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."assign_inbox_item"("p_inbox_item_id" "uuid", "p_member_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."assign_inbox_item"("p_inbox_item_id" "uuid", "p_member_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."binary_quantize"("public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."can_view_profile"("p_target_user_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."can_view_profile"("p_target_user_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."can_view_profile"("p_target_user_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."cosine_distance"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."create_notification_preferences_for_new_user"() TO "anon";
GRANT ALL ON FUNCTION "public"."create_notification_preferences_for_new_user"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."create_notification_preferences_for_new_user"() TO "service_role";



GRANT ALL ON FUNCTION "public"."create_organization"("org_name" "text") TO "anon";
GRANT ALL ON FUNCTION "public"."create_organization"("org_name" "text") TO "authenticated";
GRANT ALL ON FUNCTION "public"."create_organization"("org_name" "text") TO "service_role";



GRANT ALL ON FUNCTION "public"."delete_organization"("p_org_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."delete_organization"("p_org_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."delete_organization"("p_org_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."generate_user_insights"("p_user_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."generate_user_insights"("p_user_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."generate_user_insights"("p_user_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_accum"(double precision[], "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_accum"(double precision[], "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_accum"(double precision[], "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_accum"(double precision[], "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_add"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_add"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_add"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_add"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_avg"(double precision[]) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_avg"(double precision[]) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_avg"(double precision[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_avg"(double precision[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_cmp"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_cmp"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_cmp"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_cmp"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_combine"(double precision[], double precision[]) TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_combine"(double precision[], double precision[]) TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_combine"(double precision[], double precision[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_combine"(double precision[], double precision[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_concat"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_concat"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_concat"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_concat"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_eq"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_eq"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_eq"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_eq"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_ge"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_ge"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_ge"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_ge"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_gt"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_gt"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_gt"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_gt"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_l2_squared_distance"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_l2_squared_distance"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_l2_squared_distance"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_l2_squared_distance"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_le"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_le"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_le"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_le"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_lt"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_lt"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_lt"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_lt"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_mul"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_mul"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_mul"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_mul"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_ne"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_ne"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_ne"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_ne"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_negative_inner_product"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_negative_inner_product"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_negative_inner_product"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_negative_inner_product"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_spherical_distance"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_spherical_distance"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_spherical_distance"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_spherical_distance"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."halfvec_sub"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."halfvec_sub"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."halfvec_sub"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."halfvec_sub"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."hamming_distance"(bit, bit) TO "postgres";
GRANT ALL ON FUNCTION "public"."hamming_distance"(bit, bit) TO "anon";
GRANT ALL ON FUNCTION "public"."hamming_distance"(bit, bit) TO "authenticated";
GRANT ALL ON FUNCTION "public"."hamming_distance"(bit, bit) TO "service_role";



GRANT ALL ON FUNCTION "public"."handle_new_user"() TO "anon";
GRANT ALL ON FUNCTION "public"."handle_new_user"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."handle_new_user"() TO "service_role";



GRANT ALL ON FUNCTION "public"."hnsw_bit_support"("internal") TO "postgres";
GRANT ALL ON FUNCTION "public"."hnsw_bit_support"("internal") TO "anon";
GRANT ALL ON FUNCTION "public"."hnsw_bit_support"("internal") TO "authenticated";
GRANT ALL ON FUNCTION "public"."hnsw_bit_support"("internal") TO "service_role";



GRANT ALL ON FUNCTION "public"."hnsw_halfvec_support"("internal") TO "postgres";
GRANT ALL ON FUNCTION "public"."hnsw_halfvec_support"("internal") TO "anon";
GRANT ALL ON FUNCTION "public"."hnsw_halfvec_support"("internal") TO "authenticated";
GRANT ALL ON FUNCTION "public"."hnsw_halfvec_support"("internal") TO "service_role";



GRANT ALL ON FUNCTION "public"."hnsw_sparsevec_support"("internal") TO "postgres";
GRANT ALL ON FUNCTION "public"."hnsw_sparsevec_support"("internal") TO "anon";
GRANT ALL ON FUNCTION "public"."hnsw_sparsevec_support"("internal") TO "authenticated";
GRANT ALL ON FUNCTION "public"."hnsw_sparsevec_support"("internal") TO "service_role";



GRANT ALL ON FUNCTION "public"."hnswhandler"("internal") TO "postgres";
GRANT ALL ON FUNCTION "public"."hnswhandler"("internal") TO "anon";
GRANT ALL ON FUNCTION "public"."hnswhandler"("internal") TO "authenticated";
GRANT ALL ON FUNCTION "public"."hnswhandler"("internal") TO "service_role";



GRANT ALL ON FUNCTION "public"."inner_product"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."inner_product"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."inner_product"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."inner_product"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."is_admin"() TO "anon";
GRANT ALL ON FUNCTION "public"."is_admin"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."is_admin"() TO "service_role";



GRANT ALL ON FUNCTION "public"."is_org_admin_check"("p_org_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."is_org_admin_check"("p_org_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."is_org_admin_check"("p_org_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."is_org_member_check"("p_org_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."is_org_member_check"("p_org_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."is_org_member_check"("p_org_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."ivfflat_bit_support"("internal") TO "postgres";
GRANT ALL ON FUNCTION "public"."ivfflat_bit_support"("internal") TO "anon";
GRANT ALL ON FUNCTION "public"."ivfflat_bit_support"("internal") TO "authenticated";
GRANT ALL ON FUNCTION "public"."ivfflat_bit_support"("internal") TO "service_role";



GRANT ALL ON FUNCTION "public"."ivfflat_halfvec_support"("internal") TO "postgres";
GRANT ALL ON FUNCTION "public"."ivfflat_halfvec_support"("internal") TO "anon";
GRANT ALL ON FUNCTION "public"."ivfflat_halfvec_support"("internal") TO "authenticated";
GRANT ALL ON FUNCTION "public"."ivfflat_halfvec_support"("internal") TO "service_role";



GRANT ALL ON FUNCTION "public"."ivfflathandler"("internal") TO "postgres";
GRANT ALL ON FUNCTION "public"."ivfflathandler"("internal") TO "anon";
GRANT ALL ON FUNCTION "public"."ivfflathandler"("internal") TO "authenticated";
GRANT ALL ON FUNCTION "public"."ivfflathandler"("internal") TO "service_role";



GRANT ALL ON FUNCTION "public"."jaccard_distance"(bit, bit) TO "postgres";
GRANT ALL ON FUNCTION "public"."jaccard_distance"(bit, bit) TO "anon";
GRANT ALL ON FUNCTION "public"."jaccard_distance"(bit, bit) TO "authenticated";
GRANT ALL ON FUNCTION "public"."jaccard_distance"(bit, bit) TO "service_role";



GRANT ALL ON FUNCTION "public"."l1_distance"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l1_distance"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l1_distance"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l1_distance"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_distance"("public"."halfvec", "public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."halfvec", "public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."halfvec", "public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."halfvec", "public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_distance"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_distance"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_distance"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_norm"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_norm"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_norm"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_norm"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_norm"("public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_norm"("public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_norm"("public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_norm"("public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."l2_normalize"("public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."leave_organization"("p_org_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."leave_organization"("p_org_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."leave_organization"("p_org_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."log_application_event"() TO "anon";
GRANT ALL ON FUNCTION "public"."log_application_event"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."log_application_event"() TO "service_role";



GRANT ALL ON FUNCTION "public"."match_hyperframes_skills"("query_embedding" "public"."vector", "match_threshold" double precision, "match_count" integer) TO "anon";
GRANT ALL ON FUNCTION "public"."match_hyperframes_skills"("query_embedding" "public"."vector", "match_threshold" double precision, "match_count" integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."match_hyperframes_skills"("query_embedding" "public"."vector", "match_threshold" double precision, "match_count" integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."release_inbox_lock"("p_inbox_item_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."release_inbox_lock"("p_inbox_item_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."release_inbox_lock"("p_inbox_item_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."score_application"("p_application_id" "uuid") TO "anon";
GRANT ALL ON FUNCTION "public"."score_application"("p_application_id" "uuid") TO "authenticated";
GRANT ALL ON FUNCTION "public"."score_application"("p_application_id" "uuid") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_cmp"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_cmp"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_cmp"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_cmp"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_eq"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_eq"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_eq"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_eq"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_ge"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_ge"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_ge"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_ge"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_gt"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_gt"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_gt"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_gt"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_l2_squared_distance"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_l2_squared_distance"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_l2_squared_distance"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_l2_squared_distance"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_le"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_le"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_le"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_le"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_lt"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_lt"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_lt"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_lt"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_ne"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_ne"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_ne"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_ne"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sparsevec_negative_inner_product"("public"."sparsevec", "public"."sparsevec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sparsevec_negative_inner_product"("public"."sparsevec", "public"."sparsevec") TO "anon";
GRANT ALL ON FUNCTION "public"."sparsevec_negative_inner_product"("public"."sparsevec", "public"."sparsevec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sparsevec_negative_inner_product"("public"."sparsevec", "public"."sparsevec") TO "service_role";



GRANT ALL ON FUNCTION "public"."subvector"("public"."halfvec", integer, integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."subvector"("public"."halfvec", integer, integer) TO "anon";
GRANT ALL ON FUNCTION "public"."subvector"("public"."halfvec", integer, integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."subvector"("public"."halfvec", integer, integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."subvector"("public"."vector", integer, integer) TO "postgres";
GRANT ALL ON FUNCTION "public"."subvector"("public"."vector", integer, integer) TO "anon";
GRANT ALL ON FUNCTION "public"."subvector"("public"."vector", integer, integer) TO "authenticated";
GRANT ALL ON FUNCTION "public"."subvector"("public"."vector", integer, integer) TO "service_role";



GRANT ALL ON FUNCTION "public"."sync_post_comment_count"() TO "anon";
GRANT ALL ON FUNCTION "public"."sync_post_comment_count"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."sync_post_comment_count"() TO "service_role";



GRANT ALL ON FUNCTION "public"."sync_scheduled_posts_from_posts"() TO "anon";
GRANT ALL ON FUNCTION "public"."sync_scheduled_posts_from_posts"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."sync_scheduled_posts_from_posts"() TO "service_role";



GRANT ALL ON FUNCTION "public"."update_ai_clips_updated_at"() TO "anon";
GRANT ALL ON FUNCTION "public"."update_ai_clips_updated_at"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."update_ai_clips_updated_at"() TO "service_role";



GRANT ALL ON FUNCTION "public"."update_ai_scores"("p_application_id" "uuid", "p_overall" numeric, "p_technical" numeric, "p_communication" numeric, "p_problem_solving" numeric, "p_culture_fit" numeric, "p_recommendation" "text", "p_summary" "text", "p_strengths" "text"[], "p_concerns" "text"[]) TO "anon";
GRANT ALL ON FUNCTION "public"."update_ai_scores"("p_application_id" "uuid", "p_overall" numeric, "p_technical" numeric, "p_communication" numeric, "p_problem_solving" numeric, "p_culture_fit" numeric, "p_recommendation" "text", "p_summary" "text", "p_strengths" "text"[], "p_concerns" "text"[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."update_ai_scores"("p_application_id" "uuid", "p_overall" numeric, "p_technical" numeric, "p_communication" numeric, "p_problem_solving" numeric, "p_culture_fit" numeric, "p_recommendation" "text", "p_summary" "text", "p_strengths" "text"[], "p_concerns" "text"[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."update_application_timestamp"() TO "anon";
GRANT ALL ON FUNCTION "public"."update_application_timestamp"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."update_application_timestamp"() TO "service_role";



GRANT ALL ON FUNCTION "public"."update_render_jobs_updated_at"() TO "anon";
GRANT ALL ON FUNCTION "public"."update_render_jobs_updated_at"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."update_render_jobs_updated_at"() TO "service_role";



GRANT ALL ON FUNCTION "public"."update_studio_projects_updated_at"() TO "anon";
GRANT ALL ON FUNCTION "public"."update_studio_projects_updated_at"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."update_studio_projects_updated_at"() TO "service_role";



GRANT ALL ON FUNCTION "public"."update_updated_at_column"() TO "anon";
GRANT ALL ON FUNCTION "public"."update_updated_at_column"() TO "authenticated";
GRANT ALL ON FUNCTION "public"."update_updated_at_column"() TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_accum"(double precision[], "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_accum"(double precision[], "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_accum"(double precision[], "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_accum"(double precision[], "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_add"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_add"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_add"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_add"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_avg"(double precision[]) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_avg"(double precision[]) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_avg"(double precision[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_avg"(double precision[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_cmp"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_cmp"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_cmp"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_cmp"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_combine"(double precision[], double precision[]) TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_combine"(double precision[], double precision[]) TO "anon";
GRANT ALL ON FUNCTION "public"."vector_combine"(double precision[], double precision[]) TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_combine"(double precision[], double precision[]) TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_concat"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_concat"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_concat"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_concat"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_dims"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_dims"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_dims"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_dims"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_dims"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_dims"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_dims"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_dims"("public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_eq"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_eq"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_eq"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_eq"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_ge"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_ge"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_ge"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_ge"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_gt"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_gt"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_gt"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_gt"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_l2_squared_distance"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_l2_squared_distance"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_l2_squared_distance"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_l2_squared_distance"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_le"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_le"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_le"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_le"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_lt"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_lt"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_lt"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_lt"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_mul"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_mul"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_mul"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_mul"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_ne"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_ne"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_ne"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_ne"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_negative_inner_product"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_negative_inner_product"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_negative_inner_product"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_negative_inner_product"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_norm"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_norm"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_norm"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_norm"("public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_spherical_distance"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_spherical_distance"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_spherical_distance"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_spherical_distance"("public"."vector", "public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."vector_sub"("public"."vector", "public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."vector_sub"("public"."vector", "public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."vector_sub"("public"."vector", "public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."vector_sub"("public"."vector", "public"."vector") TO "service_role";












GRANT ALL ON FUNCTION "public"."avg"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."avg"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."avg"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."avg"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."avg"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."avg"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."avg"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."avg"("public"."vector") TO "service_role";



GRANT ALL ON FUNCTION "public"."sum"("public"."halfvec") TO "postgres";
GRANT ALL ON FUNCTION "public"."sum"("public"."halfvec") TO "anon";
GRANT ALL ON FUNCTION "public"."sum"("public"."halfvec") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sum"("public"."halfvec") TO "service_role";



GRANT ALL ON FUNCTION "public"."sum"("public"."vector") TO "postgres";
GRANT ALL ON FUNCTION "public"."sum"("public"."vector") TO "anon";
GRANT ALL ON FUNCTION "public"."sum"("public"."vector") TO "authenticated";
GRANT ALL ON FUNCTION "public"."sum"("public"."vector") TO "service_role";















GRANT ALL ON TABLE "public"."ad_accounts" TO "anon";
GRANT ALL ON TABLE "public"."ad_accounts" TO "authenticated";
GRANT ALL ON TABLE "public"."ad_accounts" TO "service_role";



GRANT ALL ON TABLE "public"."ad_campaigns" TO "anon";
GRANT ALL ON TABLE "public"."ad_campaigns" TO "authenticated";
GRANT ALL ON TABLE "public"."ad_campaigns" TO "service_role";



GRANT ALL ON TABLE "public"."admin_activity_log" TO "anon";
GRANT ALL ON TABLE "public"."admin_activity_log" TO "authenticated";
GRANT ALL ON TABLE "public"."admin_activity_log" TO "service_role";



GRANT ALL ON TABLE "public"."admin_users" TO "anon";
GRANT ALL ON TABLE "public"."admin_users" TO "authenticated";
GRANT ALL ON TABLE "public"."admin_users" TO "service_role";



GRANT ALL ON TABLE "public"."ai_clips" TO "anon";
GRANT ALL ON TABLE "public"."ai_clips" TO "authenticated";
GRANT ALL ON TABLE "public"."ai_clips" TO "service_role";



GRANT ALL ON TABLE "public"."ai_editor_feedback" TO "anon";
GRANT ALL ON TABLE "public"."ai_editor_feedback" TO "authenticated";
GRANT ALL ON TABLE "public"."ai_editor_feedback" TO "service_role";



GRANT ALL ON TABLE "public"."ai_scoring_cache" TO "anon";
GRANT ALL ON TABLE "public"."ai_scoring_cache" TO "authenticated";
GRANT ALL ON TABLE "public"."ai_scoring_cache" TO "service_role";



GRANT ALL ON TABLE "public"."api_usage" TO "anon";
GRANT ALL ON TABLE "public"."api_usage" TO "authenticated";
GRANT ALL ON TABLE "public"."api_usage" TO "service_role";



GRANT ALL ON TABLE "public"."application_events" TO "anon";
GRANT ALL ON TABLE "public"."application_events" TO "authenticated";
GRANT ALL ON TABLE "public"."application_events" TO "service_role";



GRANT ALL ON TABLE "public"."engineering_applications" TO "anon";
GRANT ALL ON TABLE "public"."engineering_applications" TO "authenticated";
GRANT ALL ON TABLE "public"."engineering_applications" TO "service_role";



GRANT ALL ON TABLE "public"."application_summary" TO "anon";
GRANT ALL ON TABLE "public"."application_summary" TO "authenticated";
GRANT ALL ON TABLE "public"."application_summary" TO "service_role";



GRANT ALL ON TABLE "public"."conversion_events" TO "anon";
GRANT ALL ON TABLE "public"."conversion_events" TO "authenticated";
GRANT ALL ON TABLE "public"."conversion_events" TO "service_role";



GRANT ALL ON TABLE "public"."conversion_pixels" TO "anon";
GRANT ALL ON TABLE "public"."conversion_pixels" TO "authenticated";
GRANT ALL ON TABLE "public"."conversion_pixels" TO "service_role";



GRANT ALL ON TABLE "public"."coupons" TO "anon";
GRANT ALL ON TABLE "public"."coupons" TO "authenticated";
GRANT ALL ON TABLE "public"."coupons" TO "service_role";



GRANT ALL ON TABLE "public"."hyperframes_skills" TO "anon";
GRANT ALL ON TABLE "public"."hyperframes_skills" TO "authenticated";
GRANT ALL ON TABLE "public"."hyperframes_skills" TO "service_role";



GRANT ALL ON TABLE "public"."lead_intelligence" TO "anon";
GRANT ALL ON TABLE "public"."lead_intelligence" TO "authenticated";
GRANT ALL ON TABLE "public"."lead_intelligence" TO "service_role";



GRANT ALL ON TABLE "public"."linked_accounts" TO "anon";
GRANT ALL ON TABLE "public"."linked_accounts" TO "authenticated";
GRANT ALL ON TABLE "public"."linked_accounts" TO "service_role";



GRANT ALL ON TABLE "public"."member_account_access" TO "anon";
GRANT ALL ON TABLE "public"."member_account_access" TO "authenticated";
GRANT ALL ON TABLE "public"."member_account_access" TO "service_role";



GRANT ALL ON TABLE "public"."member_permissions" TO "anon";
GRANT ALL ON TABLE "public"."member_permissions" TO "authenticated";
GRANT ALL ON TABLE "public"."member_permissions" TO "service_role";



GRANT ALL ON TABLE "public"."notification_preferences" TO "anon";
GRANT ALL ON TABLE "public"."notification_preferences" TO "authenticated";
GRANT ALL ON TABLE "public"."notification_preferences" TO "service_role";



GRANT ALL ON TABLE "public"."notification_types" TO "anon";
GRANT ALL ON TABLE "public"."notification_types" TO "authenticated";
GRANT ALL ON TABLE "public"."notification_types" TO "service_role";



GRANT ALL ON TABLE "public"."notifications" TO "anon";
GRANT ALL ON TABLE "public"."notifications" TO "authenticated";
GRANT ALL ON TABLE "public"."notifications" TO "service_role";



GRANT ALL ON TABLE "public"."organization_invites" TO "anon";
GRANT ALL ON TABLE "public"."organization_invites" TO "authenticated";
GRANT ALL ON TABLE "public"."organization_invites" TO "service_role";



GRANT ALL ON TABLE "public"."organization_members" TO "anon";
GRANT ALL ON TABLE "public"."organization_members" TO "authenticated";
GRANT ALL ON TABLE "public"."organization_members" TO "service_role";



GRANT ALL ON TABLE "public"."organizations" TO "anon";
GRANT ALL ON TABLE "public"."organizations" TO "authenticated";
GRANT ALL ON TABLE "public"."organizations" TO "service_role";



GRANT ALL ON TABLE "public"."pending_approvals" TO "anon";
GRANT ALL ON TABLE "public"."pending_approvals" TO "authenticated";
GRANT ALL ON TABLE "public"."pending_approvals" TO "service_role";



GRANT ALL ON TABLE "public"."tedora_leads" TO "anon";
GRANT ALL ON TABLE "public"."tedora_leads" TO "authenticated";
GRANT ALL ON TABLE "public"."tedora_leads" TO "service_role";



GRANT ALL ON TABLE "public"."pipeline_velocity" TO "anon";
GRANT ALL ON TABLE "public"."pipeline_velocity" TO "authenticated";
GRANT ALL ON TABLE "public"."pipeline_velocity" TO "service_role";



GRANT ALL ON TABLE "public"."plans" TO "anon";
GRANT ALL ON TABLE "public"."plans" TO "authenticated";
GRANT ALL ON TABLE "public"."plans" TO "service_role";



GRANT ALL ON TABLE "public"."post_comments" TO "anon";
GRANT ALL ON TABLE "public"."post_comments" TO "authenticated";
GRANT ALL ON TABLE "public"."post_comments" TO "service_role";



GRANT ALL ON TABLE "public"."post_versions" TO "anon";
GRANT ALL ON TABLE "public"."post_versions" TO "authenticated";
GRANT ALL ON TABLE "public"."post_versions" TO "service_role";



GRANT ALL ON TABLE "public"."posts" TO "anon";
GRANT ALL ON TABLE "public"."posts" TO "authenticated";
GRANT ALL ON TABLE "public"."posts" TO "service_role";



GRANT ALL ON TABLE "public"."profiles" TO "anon";
GRANT ALL ON TABLE "public"."profiles" TO "authenticated";
GRANT ALL ON TABLE "public"."profiles" TO "service_role";



GRANT ALL ON TABLE "public"."render_jobs" TO "anon";
GRANT ALL ON TABLE "public"."render_jobs" TO "authenticated";
GRANT ALL ON TABLE "public"."render_jobs" TO "service_role";



GRANT ALL ON TABLE "public"."scheduled_posts" TO "anon";
GRANT ALL ON TABLE "public"."scheduled_posts" TO "authenticated";
GRANT ALL ON TABLE "public"."scheduled_posts" TO "service_role";



GRANT ALL ON TABLE "public"."studio_projects" TO "anon";
GRANT ALL ON TABLE "public"."studio_projects" TO "authenticated";
GRANT ALL ON TABLE "public"."studio_projects" TO "service_role";



GRANT ALL ON TABLE "public"."subscriptions" TO "anon";
GRANT ALL ON TABLE "public"."subscriptions" TO "authenticated";
GRANT ALL ON TABLE "public"."subscriptions" TO "service_role";



GRANT ALL ON TABLE "public"."support_tickets" TO "anon";
GRANT ALL ON TABLE "public"."support_tickets" TO "authenticated";
GRANT ALL ON TABLE "public"."support_tickets" TO "service_role";



GRANT ALL ON TABLE "public"."team_members" TO "anon";
GRANT ALL ON TABLE "public"."team_members" TO "authenticated";
GRANT ALL ON TABLE "public"."team_members" TO "service_role";



GRANT ALL ON TABLE "public"."team_profiles" TO "anon";
GRANT ALL ON TABLE "public"."team_profiles" TO "authenticated";
GRANT ALL ON TABLE "public"."team_profiles" TO "service_role";



GRANT ALL ON TABLE "public"."teams" TO "anon";
GRANT ALL ON TABLE "public"."teams" TO "authenticated";
GRANT ALL ON TABLE "public"."teams" TO "service_role";



GRANT ALL ON TABLE "public"."tedora_admins" TO "anon";
GRANT ALL ON TABLE "public"."tedora_admins" TO "authenticated";
GRANT ALL ON TABLE "public"."tedora_admins" TO "service_role";



GRANT ALL ON TABLE "public"."tedora_insights" TO "anon";
GRANT ALL ON TABLE "public"."tedora_insights" TO "authenticated";
GRANT ALL ON TABLE "public"."tedora_insights" TO "service_role";



GRANT ALL ON TABLE "public"."tedora_interactions" TO "anon";
GRANT ALL ON TABLE "public"."tedora_interactions" TO "authenticated";
GRANT ALL ON TABLE "public"."tedora_interactions" TO "service_role";



GRANT ALL ON TABLE "public"."tedora_projects" TO "anon";
GRANT ALL ON TABLE "public"."tedora_projects" TO "authenticated";
GRANT ALL ON TABLE "public"."tedora_projects" TO "service_role";



GRANT ALL ON TABLE "public"."tedora_proposals" TO "anon";
GRANT ALL ON TABLE "public"."tedora_proposals" TO "authenticated";
GRANT ALL ON TABLE "public"."tedora_proposals" TO "service_role";



GRANT ALL ON TABLE "public"."transactions" TO "anon";
GRANT ALL ON TABLE "public"."transactions" TO "authenticated";
GRANT ALL ON TABLE "public"."transactions" TO "service_role";



GRANT ALL ON TABLE "public"."trigger_debug_log" TO "anon";
GRANT ALL ON TABLE "public"."trigger_debug_log" TO "authenticated";
GRANT ALL ON TABLE "public"."trigger_debug_log" TO "service_role";



GRANT ALL ON SEQUENCE "public"."trigger_debug_log_id_seq" TO "anon";
GRANT ALL ON SEQUENCE "public"."trigger_debug_log_id_seq" TO "authenticated";
GRANT ALL ON SEQUENCE "public"."trigger_debug_log_id_seq" TO "service_role";



GRANT ALL ON TABLE "public"."user_assets" TO "anon";
GRANT ALL ON TABLE "public"."user_assets" TO "authenticated";
GRANT ALL ON TABLE "public"."user_assets" TO "service_role";



GRANT ALL ON TABLE "public"."user_insights" TO "anon";
GRANT ALL ON TABLE "public"."user_insights" TO "authenticated";
GRANT ALL ON TABLE "public"."user_insights" TO "service_role";



GRANT ALL ON TABLE "public"."user_preferences" TO "anon";
GRANT ALL ON TABLE "public"."user_preferences" TO "authenticated";
GRANT ALL ON TABLE "public"."user_preferences" TO "service_role";



GRANT ALL ON TABLE "public"."user_sessions" TO "anon";
GRANT ALL ON TABLE "public"."user_sessions" TO "authenticated";
GRANT ALL ON TABLE "public"."user_sessions" TO "service_role";



GRANT ALL ON TABLE "public"."utm_links" TO "anon";
GRANT ALL ON TABLE "public"."utm_links" TO "authenticated";
GRANT ALL ON TABLE "public"."utm_links" TO "service_role";



GRANT ALL ON TABLE "public"."viral_videos" TO "anon";
GRANT ALL ON TABLE "public"."viral_videos" TO "authenticated";
GRANT ALL ON TABLE "public"."viral_videos" TO "service_role";



GRANT ALL ON TABLE "public"."youtube_ingest_jobs" TO "anon";
GRANT ALL ON TABLE "public"."youtube_ingest_jobs" TO "authenticated";
GRANT ALL ON TABLE "public"."youtube_ingest_jobs" TO "service_role";









ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "postgres";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "anon";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "authenticated";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON SEQUENCES TO "service_role";






ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "postgres";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "anon";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "authenticated";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON FUNCTIONS TO "service_role";






ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON TABLES TO "postgres";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON TABLES TO "anon";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON TABLES TO "authenticated";
ALTER DEFAULT PRIVILEGES FOR ROLE "postgres" IN SCHEMA "public" GRANT ALL ON TABLES TO "service_role";































drop extension if exists "pg_net";

create extension if not exists "pg_net" with schema "public";

drop policy "Anyone can delete applications" on "public"."engineering_applications";

drop policy "Anyone can read applications" on "public"."engineering_applications";

drop policy "Anyone can submit applications" on "public"."engineering_applications";

drop policy "Anyone can update applications" on "public"."engineering_applications";

drop policy "Allow all profile operations" on "public"."profiles";

drop policy "Users can view own profile" on "public"."profiles";


  create policy "Anyone can delete applications"
  on "public"."engineering_applications"
  as permissive
  for delete
  to anon, authenticated
using (true);



  create policy "Anyone can read applications"
  on "public"."engineering_applications"
  as permissive
  for select
  to anon, authenticated
using (true);



  create policy "Anyone can submit applications"
  on "public"."engineering_applications"
  as permissive
  for insert
  to anon, authenticated
with check (true);



  create policy "Anyone can update applications"
  on "public"."engineering_applications"
  as permissive
  for update
  to anon, authenticated
using (true)
with check (true);



  create policy "Allow all profile operations"
  on "public"."profiles"
  as permissive
  for all
  to anon, authenticated, service_role
using (true)
with check (true);



  create policy "Users can view own profile"
  on "public"."profiles"
  as permissive
  for select
  to anon, authenticated
using ((id = auth.uid()));


CREATE TRIGGER on_auth_user_created AFTER INSERT ON auth.users FOR EACH ROW EXECUTE FUNCTION public.handle_new_user();


  create policy "Admin Uploads 1npzng3_0"
  on "storage"."objects"
  as permissive
  for insert
  to public
with check (((bucket_id = 'admin_assets'::text) AND ( SELECT public.is_admin() AS is_admin)));



  create policy "Anyone can read avatars"
  on "storage"."objects"
  as permissive
  for select
  to public
using ((bucket_id = 'avatars'::text));



  create policy "Anyone can read media"
  on "storage"."objects"
  as permissive
  for select
  to public
using ((bucket_id = 'media'::text));



  create policy "Anyone can read renders"
  on "storage"."objects"
  as permissive
  for select
  to public
using ((bucket_id = 'renders'::text));



  create policy "Authenticated users can upload avatars"
  on "storage"."objects"
  as permissive
  for insert
  to authenticated
with check ((bucket_id = 'avatars'::text));



  create policy "Authenticated users can upload media"
  on "storage"."objects"
  as permissive
  for insert
  to authenticated, service_role
with check ((bucket_id = 'media'::text));



  create policy "Authenticated users can upload renders"
  on "storage"."objects"
  as permissive
  for insert
  to authenticated, service_role
with check ((bucket_id = 'renders'::text));



  create policy "Public Access i2e9i5_0"
  on "storage"."objects"
  as permissive
  for insert
  to public
with check ((bucket_id = 'renders'::text));



  create policy "Public Access i2e9i5_1"
  on "storage"."objects"
  as permissive
  for delete
  to public
using ((bucket_id = 'renders'::text));



  create policy "Public Access i2e9i5_2"
  on "storage"."objects"
  as permissive
  for select
  to public
using ((bucket_id = 'renders'::text));



  create policy "Public Access to Proposal Assets jxwokw_0"
  on "storage"."objects"
  as permissive
  for select
  to public
using ((bucket_id = 'proposal_assets'::text));



  create policy "Public Upload to Proposal Assets jxwokw_0"
  on "storage"."objects"
  as permissive
  for insert
  to public
with check ((bucket_id = 'proposal_assets'::text));



  create policy "Public can read avatars"
  on "storage"."objects"
  as permissive
  for select
  to public
using ((bucket_id = 'avatars'::text));



  create policy "Public read access for avatars"
  on "storage"."objects"
  as permissive
  for select
  to public
using ((bucket_id = 'avatars'::text));



  create policy "Users can delete own avatars"
  on "storage"."objects"
  as permissive
  for delete
  to authenticated
using ((bucket_id = 'avatars'::text));



  create policy "Users can delete own media"
  on "storage"."objects"
  as permissive
  for delete
  to authenticated, service_role
using ((bucket_id = 'media'::text));



  create policy "Users can delete own renders"
  on "storage"."objects"
  as permissive
  for delete
  to authenticated, service_role
using ((bucket_id = 'renders'::text));



  create policy "Users can update own avatars"
  on "storage"."objects"
  as permissive
  for update
  to authenticated
using ((bucket_id = 'avatars'::text))
with check ((bucket_id = 'avatars'::text));



  create policy "Users can update own media"
  on "storage"."objects"
  as permissive
  for update
  to authenticated, service_role
using ((bucket_id = 'media'::text))
with check ((bucket_id = 'media'::text));



  create policy "Users can update own renders"
  on "storage"."objects"
  as permissive
  for update
  to authenticated, service_role
using ((bucket_id = 'renders'::text))
with check ((bucket_id = 'renders'::text));



  create policy "Users can update their own avatar"
  on "storage"."objects"
  as permissive
  for update
  to authenticated
using (((bucket_id = 'avatars'::text) AND ((auth.uid())::text = (storage.foldername(name))[1])));



  create policy "Users can upload their own avatar"
  on "storage"."objects"
  as permissive
  for insert
  to authenticated
with check (((bucket_id = 'avatars'::text) AND ((auth.uid())::text = (storage.foldername(name))[1])));



  create policy "uploads_insert_authenticated"
  on "storage"."objects"
  as permissive
  for insert
  to authenticated
with check ((bucket_id = 'uploads'::text));



  create policy "uploads_select_authenticated"
  on "storage"."objects"
  as permissive
  for select
  to authenticated
using ((bucket_id = 'uploads'::text));



