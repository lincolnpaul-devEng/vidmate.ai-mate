-- Supabase Seed Script for SocialPulse
-- This script ensures consistent data across your app by inserting
-- a sample post and linked accounts for your recently authenticated user.

DO $$ 
DECLARE 
  current_user_id uuid;
  post_id uuid;
BEGIN 
  -- 1. Get the most recently signed up/authenticated user
  -- Since you just signed back in with Google, this will grab YOUR account ID.
  SELECT id INTO current_user_id FROM auth.users ORDER BY last_sign_in_at DESC NULLS LAST LIMIT 1;
  
  IF current_user_id IS NOT NULL THEN
    -- 2. Ensure their profile exists (though the auth trigger should have handled this)
    -- Using generic values instead of hardcoded names
    INSERT INTO public.profiles (id, name, total_posts, total_reach)
    VALUES (current_user_id, 'New User', 0, 0)
    ON CONFLICT (id) DO NOTHING;

    -- 3. We no longer insert mock linked accounts to ensure real data is used
    -- (Removed Lincoln-specific insertions)

    RAISE NOTICE 'Succesfully initialized SocialPulse profiles for user: %', current_user_id;
  ELSE
    RAISE NOTICE 'No authenticated users found. Please sign in via Google first, then run this script.';
  END IF;
END $$;
