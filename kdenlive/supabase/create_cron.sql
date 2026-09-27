-- IMPORTANT: pg_cron does not interpolate environment variables here.
-- Replace [YOUR_CRON_SECRET] with the actual secret value stored in Supabase function secrets.
SELECT cron.schedule(
  'socialpulse-scheduler',
  '* * * * *',
  $$
    SELECT net.http_post(
      url := 'https://mgurbmoubtqzsgfdlgur.supabase.co/functions/v1/scheduler',
      headers := '{"Content-Type":"application/json","Authorization":"Bearer ZyToUMySxIqfdN2yoUGDV0PN1vpgxD5tYn714hpQa1E="}'::jsonb,
      body := '{}'::jsonb
    )
  $$
);
