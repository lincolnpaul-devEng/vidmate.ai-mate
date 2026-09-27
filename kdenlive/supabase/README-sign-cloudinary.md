# Deploy `sign-cloudinary-upload` Supabase Function

This repository includes a Supabase Edge Function at `supabase/functions/sign-cloudinary-upload` that signs Cloudinary upload parameters.

Purpose
- Centralize Cloudinary secrets in Supabase Secrets.
- Avoid calling the backend service for signing uploads.

Prerequisites
- Install the Supabase CLI: https://supabase.com/docs/guides/cli
- Authenticate with `supabase login`
- Ensure you have access to the target Supabase project (project ref or via `supabase login`).

Deploy the function

1. Build and deploy the function:

```bash
# from project root
cd supabase
# Deploy function (adjust --project-ref if needed)
supabase functions deploy sign-cloudinary-upload --project-ref $SUPABASE_PROJECT_REF
```

2. Set the required secrets in Supabase (never commit secrets to repo):

```bash
# set these in your environment or replace values inline
supabase secrets set \
  CLOUDINARY_API_KEY="$CLOUDINARY_API_KEY" \
  CLOUDINARY_API_SECRET="$CLOUDINARY_API_SECRET" \
  CLOUDINARY_CLOUD_NAME="$CLOUDINARY_CLOUD_NAME" \
  --project-ref $SUPABASE_PROJECT_REF
```

3. Verify the function is reachable locally (optional):

```bash
# Serve functions locally for testing
supabase functions serve
# Then POST to http://127.0.0.1:54321/functions/v1/sign-cloudinary-upload with JSON body
```

Notes
- The function code expects the secrets `CLOUDINARY_API_SECRET`, `CLOUDINARY_API_KEY`, and `CLOUDINARY_CLOUD_NAME` to exist in Supabase secrets.
- Client-side code uses `supabase.functions.invoke('sign-cloudinary-upload')` to request signatures.
- After deploying and setting secrets, remove Cloudinary secrets from any backend processes and restart services.
