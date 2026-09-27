-- Allow project owners to delete their AI clips (compose project delete).
-- FK already cascades on studio_projects delete; this enables explicit clip deletes too.

CREATE POLICY "Users can delete clips from their projects"
  ON public.ai_clips
  FOR DELETE
  TO authenticated
  USING (
    project_id IN (
      SELECT id FROM public.studio_projects WHERE user_id = auth.uid()
    )
  );
