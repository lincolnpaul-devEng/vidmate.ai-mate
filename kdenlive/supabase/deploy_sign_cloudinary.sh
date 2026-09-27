#!/usr/bin/env bash
# Deploy sign-cloudinary-upload function and set secrets in Supabase.
# Usage:
#   SUPABASE_PROJECT_REF=your-ref CLOUDINARY_API_KEY=... CLOUDINARY_API_SECRET=... CLOUDINARY_CLOUD_NAME=... ./supabase/deploy_sign_cloudinary.sh

set -euo pipefail

if ! command -v supabase >/dev/null 2>&1; then
  echo "supabase CLI is not installed. Install from https://supabase.com/docs/guides/cli"
  exit 1
fi

if [ -z "${SUPABASE_PROJECT_REF:-}" ]; then
  echo "Please set SUPABASE_PROJECT_REF environment variable (project ref)."
  exit 1
fi

if [ -z "${CLOUDINARY_API_KEY:-}" ] || [ -z "${CLOUDINARY_API_SECRET:-}" ] || [ -z "${CLOUDINARY_CLOUD_NAME:-}" ]; then
  echo "Please set CLOUDINARY_API_KEY, CLOUDINARY_API_SECRET, and CLOUDINARY_CLOUD_NAME environment variables."
  exit 1
fi

cd "$(dirname "$0")" || exit 1

FUNCTION_DIR="functions/sign-cloudinary-upload"
mkdir -p "$FUNCTION_DIR"

if [ ! -f "$FUNCTION_DIR/index.ts" ]; then
  echo "Creating function entrypoint at $FUNCTION_DIR/index.ts"
  cat > "$FUNCTION_DIR/index.ts" <<'EOF'
import { serve } from "https://deno.land/std@0.168.0/http/server.ts"

serve(async (req) => {
  return new Response(JSON.stringify({ ok: true, message: "Cloudinary signer placeholder" }), {
    headers: { "Content-Type": "application/json" },
    status: 200,
  })
})
EOF
fi

echo "Deploying Supabase Edge Function: sign-cloudinary-upload"
supabase functions deploy sign-cloudinary-upload --project-ref "$SUPABASE_PROJECT_REF"

echo "Setting Supabase secrets for Cloudinary"
supabase secrets set \
  CLOUDINARY_API_KEY="$CLOUDINARY_API_KEY" \
  CLOUDINARY_API_SECRET="$CLOUDINARY_API_SECRET" \
  CLOUDINARY_CLOUD_NAME="$CLOUDINARY_CLOUD_NAME" \
  --project-ref "$SUPABASE_PROJECT_REF"

echo "Done. Remember to remove any Cloudinary secrets from backend envs and restart services."
