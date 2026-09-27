-- Backfill AI clipping user settings into processing_metadata.settings for older projects.
-- This ensures the clip generator has a reliable settings payload even when projects were created before the new mapping was wired in.

UPDATE studio_projects
SET processing_metadata = COALESCE(processing_metadata, '{}'::jsonb) || jsonb_build_object(
  'settings',
  COALESCE(processing_metadata->'settings', '{}'::jsonb) || jsonb_build_object(
    'genre', COALESCE(NULLIF(processing_metadata->'settings'->>'genre', ''), 'Podcast'),
    'clipLength', COALESCE(NULLIF(processing_metadata->'settings'->>'clipLength', ''), 'Auto (0m-3m)'),
    'keywords', COALESCE(NULLIF(processing_metadata->'settings'->>'keywords', ''), ''),
    'startTime', COALESCE(NULLIF(processing_metadata->'settings'->>'startTime', ''), '0:00:00'),
    'endTime', COALESCE(NULLIF(processing_metadata->'settings'->>'endTime', ''), '0:01:15'),
    'numClips', CASE
      WHEN NULLIF(processing_metadata->'settings'->>'numClips', '') IS NULL THEN
        GREATEST(1, LEAST(10, COALESCE((processing_metadata->>'clips_requested')::int, 3)))
      ELSE
        GREATEST(1, LEAST(10, COALESCE((processing_metadata->'settings'->>'numClips')::int, 3)))
    END
  )
)
WHERE processing_metadata IS NULL
   OR processing_metadata->'settings' IS NULL
   OR processing_metadata->'settings'->>'genre' IS NULL
   OR processing_metadata->'settings'->>'clipLength' IS NULL
   OR processing_metadata->'settings'->>'numClips' IS NULL;
