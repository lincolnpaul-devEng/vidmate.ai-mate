/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * AI Tool Registry — All Velo-equivalent tool definitions for the autonomous
 * video editing agent harness, ported from the original TypeScript/Electron Velo
 * editor to C++ Kdenlive + Natron.
 */

#include "aitoolregistry.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static QJsonObject stringParam(const QString &desc)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("string");
    p[QStringLiteral("description")] = desc;
    return p;
}

static QJsonObject intParam(const QString &desc)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("integer");
    p[QStringLiteral("description")] = desc;
    return p;
}

static QJsonObject numberParam(const QString &desc)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("number");
    p[QStringLiteral("description")] = desc;
    return p;
}

static QJsonObject boolParam(const QString &desc)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("boolean");
    p[QStringLiteral("description")] = desc;
    return p;
}

static QJsonObject enumParam(const QString &desc, const QStringList &values)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("string");
    p[QStringLiteral("description")] = desc;
    QJsonArray enumArr;
    for (const auto &v : values) enumArr.append(v);
    p[QStringLiteral("enum")] = enumArr;
    return p;
}

static QJsonObject makeParams(const QJsonObject &properties, const QStringList &required)
{
    QJsonObject params;
    params[QStringLiteral("type")] = QStringLiteral("object");
    params[QStringLiteral("properties")] = properties;
    QJsonArray reqArr;
    for (const auto &r : required) reqArr.append(r);
    params[QStringLiteral("required")] = reqArr;
    return params;
}

// ── AIToolRegistry ──────────────────────────────────────────────────────────

AIToolRegistry::AIToolRegistry()
{
    registerAllTools();
}

void AIToolRegistry::addTool(const QString &name, const QString &description,
                             const QJsonObject &parameters, const QString &targetEngine)
{
    m_tools.append({name, description, parameters, targetEngine});
}

void AIToolRegistry::registerAllTools()
{
    // ════════════════════════════════════════════════════════════════════════
    // TIMELINE EDITING TOOLS (ported from Velo manage_timelines / edit_item)
    // ════════════════════════════════════════════════════════════════════════

    // 1. cut_at_playhead — slice all clips under cursor
    {
        QJsonObject props;
        props[QStringLiteral("position")] = intParam(QStringLiteral(
            "Frame position to cut at. Use -1 for current playhead (default)."));
        addTool(QStringLiteral("cut_at_playhead"),
                QStringLiteral("Cut/split all clips at the given frame position (or current playhead)."),
                makeParams(props, {}));
    }

    // 2. delete_clips — remove selected or specified clips
    {
        QJsonObject props;
        props[QStringLiteral("clip_ids")] = stringParam(QStringLiteral(
            "Comma-separated clip IDs to delete. Empty = delete current selection."));
        addTool(QStringLiteral("delete_clips"),
                QStringLiteral("Delete clips from the timeline by ID or current selection."),
                makeParams(props, {}));
    }

    // 3. trim_clip — adjust clip in/out points (ripple or rolling)
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral("Clip ID to trim."));
        props[QStringLiteral("delta_in")] = intParam(QStringLiteral(
            "Frames to add/subtract from clip's in-point. Positive = trim from start."));
        props[QStringLiteral("delta_out")] = intParam(QStringLiteral(
            "Frames to add/subtract from clip's out-point. Positive = extend, negative = shrink."));
        props[QStringLiteral("ripple")] = boolParam(QStringLiteral(
            "If true, shift subsequent clips to fill/accommodate the trim."));
        addTool(QStringLiteral("trim_clip"),
                QStringLiteral("Trim a clip's in-point and/or out-point by a frame delta. Supports ripple mode."),
                makeParams(props, {QStringLiteral("clip_id")}));
    }

    // 4. move_clip — reposition clip on timeline
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral("Clip ID to move."));
        props[QStringLiteral("target_track")] = intParam(QStringLiteral(
            "Target track ID. Use -1 to keep on same track."));
        props[QStringLiteral("target_position")] = intParam(QStringLiteral(
            "Target frame position for the clip's start."));
        addTool(QStringLiteral("move_clip"),
                QStringLiteral("Move a clip to a different track and/or position on the timeline."),
                makeParams(props, {QStringLiteral("clip_id"), QStringLiteral("target_position")}));
    }

    // 5. set_clip_speed — variable speed / speed ramp
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral("Clip ID."));
        props[QStringLiteral("speed")] = numberParam(QStringLiteral(
            "Speed multiplier (e.g., 0.5 = half speed, 2.0 = double speed)."));
        addTool(QStringLiteral("set_clip_speed"),
                QStringLiteral("Change playback speed of a clip (slow motion or fast forward)."),
                makeParams(props, {QStringLiteral("clip_id"), QStringLiteral("speed")}));
    }

    // 6. insert_clip — add a clip from project bin to timeline
    {
        QJsonObject props;
        props[QStringLiteral("bin_id")] = stringParam(QStringLiteral(
            "Project bin clip ID (UUID) to insert."));
        props[QStringLiteral("track_id")] = intParam(QStringLiteral(
            "Track ID where the clip should be inserted."));
        props[QStringLiteral("position")] = intParam(QStringLiteral(
            "Frame position to insert at. -1 = end of track."));
        addTool(QStringLiteral("insert_clip"),
                QStringLiteral("Insert a clip from the project bin onto a timeline track at a specific position."),
                makeParams(props, {QStringLiteral("bin_id"), QStringLiteral("track_id"), QStringLiteral("position")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // EFFECTS & FILTERS (ported from Velo edit_item / apply_effect)
    // ════════════════════════════════════════════════════════════════════════

    // 7. add_effect — apply effect/filter to clip
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral(
            "Clip ID. Use -1 to apply to current selection."));
        props[QStringLiteral("effect_id")] = stringParam(QStringLiteral(
            "Effect identifier, e.g. 'frei0r.glow', 'frei0r.glitch0r', 'boxblur', 'volume', "
            "'brightness', 'charcoal', 'sepia', 'frei0r.coloradj_RGB'."));
        addTool(QStringLiteral("add_effect"),
                QStringLiteral("Apply a video/audio effect to a clip. Supports frei0r, MLT built-in, and LADSPA effects."),
                makeParams(props, {QStringLiteral("effect_id")}));
    }

    // 8. remove_effect — remove effect from clip
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral("Clip ID."));
        props[QStringLiteral("effect_index")] = intParam(QStringLiteral(
            "Index of the effect in the clip's effect stack (0-based)."));
        addTool(QStringLiteral("remove_effect"),
                QStringLiteral("Remove a specific effect from a clip's effect stack by index."),
                makeParams(props, {QStringLiteral("clip_id"), QStringLiteral("effect_index")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // TRACK MANAGEMENT (ported from Velo manage_timelines)
    // ════════════════════════════════════════════════════════════════════════

    // 9. add_track — insert new video or audio track
    {
        QJsonObject props;
        props[QStringLiteral("track_type")] = enumParam(
            QStringLiteral("Type of track to add."),
            {QStringLiteral("video"), QStringLiteral("audio")});
        props[QStringLiteral("position")] = intParam(QStringLiteral(
            "Position index for the new track. -1 = append."));
        props[QStringLiteral("name")] = stringParam(QStringLiteral(
            "Display name for the new track."));
        addTool(QStringLiteral("add_track"),
                QStringLiteral("Add a new video or audio track to the timeline."),
                makeParams(props, {QStringLiteral("track_type")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // TRANSITIONS & COMPOSITIONS (ported from Velo manage_fusion)
    // ════════════════════════════════════════════════════════════════════════

    // 10. add_transition — insert transition/composition between clips
    {
        QJsonObject props;
        props[QStringLiteral("track_id")] = intParam(QStringLiteral("Track ID for the composition."));
        props[QStringLiteral("position")] = intParam(QStringLiteral(
            "Frame position to place the transition."));
        props[QStringLiteral("transition_id")] = stringParam(QStringLiteral(
            "Transition type: 'wipe', 'dissolve', 'slide', 'luma', 'composite'."));
        props[QStringLiteral("duration")] = intParam(QStringLiteral(
            "Duration in frames. Default = 30 (1 second at 30fps)."));
        addTool(QStringLiteral("add_transition"),
                QStringLiteral("Add a transition/composition between overlapping clips on a track."),
                makeParams(props, {QStringLiteral("track_id"), QStringLiteral("position"), QStringLiteral("transition_id")}));
    }

    // 11. add_mix — create cross-dissolve mix between adjacent clips
    {
        QJsonObject props;
        props[QStringLiteral("track_id")] = intParam(QStringLiteral("Track ID."));
        props[QStringLiteral("position")] = intParam(QStringLiteral(
            "Frame position at the cut point between two adjacent clips."));
        props[QStringLiteral("transition_id")] = stringParam(QStringLiteral(
            "Mix transition type (e.g., 'luma')."));
        addTool(QStringLiteral("add_mix"),
                QStringLiteral("Create a cross-dissolve mix between two adjacent clips at a cut point."),
                makeParams(props, {QStringLiteral("track_id"), QStringLiteral("position")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // AUDIO TOOLS (ported from Velo audio intelligence)
    // ════════════════════════════════════════════════════════════════════════

    // 12. set_volume — adjust clip audio level
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral("Clip ID."));
        props[QStringLiteral("volume_db")] = numberParam(QStringLiteral(
            "Volume level in dB. 0 = unity, -inf = silence, +6 = boost."));
        addTool(QStringLiteral("set_volume"),
                QStringLiteral("Set the audio volume level of a clip in decibels."),
                makeParams(props, {QStringLiteral("clip_id"), QStringLiteral("volume_db")}));
    }

    // 13. audio_ducking — auto-duck music under speech
    {
        QJsonObject props;
        props[QStringLiteral("speech_track")] = intParam(QStringLiteral(
            "Track ID containing speech/dialogue."));
        props[QStringLiteral("music_track")] = intParam(QStringLiteral(
            "Track ID containing background music."));
        props[QStringLiteral("duck_level_db")] = numberParam(QStringLiteral(
            "Volume to duck music to during speech (default: -24 dB)."));
        addTool(QStringLiteral("audio_ducking"),
                QStringLiteral("Automatically duck background music volume when speech is detected on another track."),
                makeParams(props, {QStringLiteral("speech_track"), QStringLiteral("music_track")}));
    }

    // 14. remove_silence — detect and cut silent segments
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral(
            "Clip ID. -1 = process all clips on selected tracks."));
        props[QStringLiteral("threshold_db")] = numberParam(QStringLiteral(
            "Silence threshold in dB (default: -40). Audio below this is considered silence."));
        props[QStringLiteral("min_duration_ms")] = intParam(QStringLiteral(
            "Minimum silence duration in milliseconds to cut (default: 500)."));
        addTool(QStringLiteral("remove_silence"),
                QStringLiteral("Detect and remove/shorten silent segments in clips (dead-air trimming)."),
                makeParams(props, {}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // SUBTITLE & TITLE TOOLS
    // ════════════════════════════════════════════════════════════════════════

    // 15. add_subtitle — add subtitle text entry
    {
        QJsonObject props;
        props[QStringLiteral("text")] = stringParam(QStringLiteral("Subtitle text content."));
        props[QStringLiteral("start_frame")] = intParam(QStringLiteral("Start frame for the subtitle."));
        props[QStringLiteral("end_frame")] = intParam(QStringLiteral("End frame for the subtitle."));
        addTool(QStringLiteral("add_subtitle"),
                QStringLiteral("Add a subtitle text entry to the subtitle track with specified timing."),
                makeParams(props, {QStringLiteral("text"), QStringLiteral("start_frame"), QStringLiteral("end_frame")}));
    }

    // 15b. style_subtitles — customize and animate subtitle styling across the timeline
    {
        QJsonObject props;
        props[QStringLiteral("preset")] = enumParam(
            QStringLiteral("Visual preset style for video captions."),
            {QStringLiteral("tiktok_viral"), QStringLiteral("mrbeast_pop"), QStringLiteral("clean_cinema"),
             QStringLiteral("netflix_boxed"), QStringLiteral("neon_cyber"), QStringLiteral("custom")});
        props[QStringLiteral("font_family")] = stringParam(QStringLiteral("Font family name (e.g. Impact, Montserrat, Arial, Inter)."));
        props[QStringLiteral("font_size")] = numberParam(QStringLiteral("Font point size (default: 28)."));
        props[QStringLiteral("primary_color")] = stringParam(QStringLiteral("Text fill color in hex (e.g. #FFFF00 for yellow, #FFFFFF for white)."));
        props[QStringLiteral("outline_color")] = stringParam(QStringLiteral("Text outline/stroke color in hex (e.g. #000000)."));
        props[QStringLiteral("outline_width")] = numberParam(QStringLiteral("Outline stroke thickness in pixels (e.g. 3.0)."));
        props[QStringLiteral("shadow_width")] = numberParam(QStringLiteral("Drop shadow distance in pixels (e.g. 2.0)."));
        props[QStringLiteral("bold")] = boolParam(QStringLiteral("Whether text should be extra bold."));
        props[QStringLiteral("alignment")] = enumParam(
            QStringLiteral("Screen alignment."),
            {QStringLiteral("bottom"), QStringLiteral("center"), QStringLiteral("top")});
        props[QStringLiteral("margin_v")] = intParam(QStringLiteral("Vertical margin from edge in pixels (default: 35)."));
        props[QStringLiteral("custom_style")] = stringParam(QStringLiteral("Raw ASS force_style string override."));
        addTool(QStringLiteral("style_subtitles"),
                QStringLiteral("Change the global visual style and animation appearance of all timeline captions (TikTok bold yellow, MrBeast pop, Netflix boxed, Neon, etc.)."),
                makeParams(props, {}));
    }

    // 15c. edit_subtitle — modify text or timing of a specific caption
    {
        QJsonObject props;
        props[QStringLiteral("subtitle_id")] = intParam(QStringLiteral("ID of the subtitle to edit (-1 to match by search_text or playhead)."));
        props[QStringLiteral("search_text")] = stringParam(QStringLiteral("Text substring to find and replace/modify in the subtitle track."));
        props[QStringLiteral("new_text")] = stringParam(QStringLiteral("New replacement caption text."));
        props[QStringLiteral("start_frame")] = intParam(QStringLiteral("New start frame timing (-1 to keep current)."));
        props[QStringLiteral("end_frame")] = intParam(QStringLiteral("New end frame timing (-1 to keep current)."));
        addTool(QStringLiteral("edit_subtitle"),
                QStringLiteral("Modify text content, wording, or timing of subtitles on the timeline."),
                makeParams(props, {QStringLiteral("new_text")}));
    }

    // 16. insert_title — add styled title card
    {
        QJsonObject props;
        props[QStringLiteral("text")] = stringParam(QStringLiteral("Title text."));
        props[QStringLiteral("style")] = enumParam(
            QStringLiteral("Visual style preset."),
            {QStringLiteral("minimal"), QStringLiteral("cinematic"), QStringLiteral("bold"),
             QStringLiteral("lower_third"), QStringLiteral("full_screen")});
        props[QStringLiteral("duration_frames")] = intParam(QStringLiteral(
            "Duration of the title card in frames (default: 90 = 3s at 30fps)."));
        props[QStringLiteral("position")] = intParam(QStringLiteral(
            "Frame position on timeline. -1 = at playhead."));
        addTool(QStringLiteral("insert_title"),
                QStringLiteral("Insert a styled title card or lower-third caption onto the timeline."),
                makeParams(props, {QStringLiteral("text")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // NATRON VFX (ported from Velo manage_fusion)
    // ════════════════════════════════════════════════════════════════════════

    // 17. natron_vfx — run headless Natron compositing pipeline
    {
        QJsonObject props;
        props[QStringLiteral("pipeline")] = enumParam(
            QStringLiteral("Pre-built VFX pipeline template."),
            {QStringLiteral("chroma_key"), QStringLiteral("rotoscope"), QStringLiteral("planar_track"),
             QStringLiteral("particle_system"), QStringLiteral("light_wrap"), QStringLiteral("glitch_composite"),
             QStringLiteral("text_behind_subject"), QStringLiteral("color_grade"), QStringLiteral("custom")});
        props[QStringLiteral("input_clip")] = stringParam(QStringLiteral(
            "Path to the input media file for VFX processing."));
        props[QStringLiteral("output_path")] = stringParam(QStringLiteral(
            "Output path for rendered VFX result."));
        props[QStringLiteral("vfx_script")] = stringParam(QStringLiteral(
            "Custom Python/Natron script for the 'custom' pipeline type."));
        props[QStringLiteral("params")] = stringParam(QStringLiteral(
            "JSON string of pipeline-specific parameters (e.g., key color for chroma_key)."));
        addTool(QStringLiteral("natron_vfx"),
                QStringLiteral("Execute a headless Natron VFX compositing pipeline. Supports pre-built templates "
                               "(chroma key, rotoscope, tracking, particles) or custom Python scripts."),
                makeParams(props, {QStringLiteral("pipeline")}),
                QStringLiteral("natron"));
    }

    // ════════════════════════════════════════════════════════════════════════
    // VERIFICATION & INSPECTION (ported from Velo view_timeline_frames / probe_quality)
    // ════════════════════════════════════════════════════════════════════════

    // 18. view_timeline_frames — grab frame snapshot for visual verification
    {
        QJsonObject props;
        props[QStringLiteral("frame")] = intParam(QStringLiteral(
            "Frame number to capture. -1 = current playhead."));
        props[QStringLiteral("count")] = intParam(QStringLiteral(
            "Number of evenly-spaced frames to capture (default: 1, max: 5)."));
        addTool(QStringLiteral("view_timeline_frames"),
                QStringLiteral("Capture frame snapshot(s) from the timeline for visual verification. "
                               "Returns base64-encoded images for vision model inspection."),
                makeParams(props, {}));
    }

    // 19. get_timeline_state — query full timeline state
    {
        QJsonObject props;
        props[QStringLiteral("include_effects")] = boolParam(QStringLiteral(
            "Include effect stacks in the output (default: false)."));
        addTool(QStringLiteral("get_timeline_state"),
                QStringLiteral("Query the full timeline state: all tracks, clips, positions, durations, and optionally effect stacks."),
                makeParams(props, {}));
    }

    // 20. probe_quality — audio/video quality check
    {
        QJsonObject props;
        props[QStringLiteral("checks")] = enumParam(
            QStringLiteral("Quality check type."),
            {QStringLiteral("audio_levels"), QStringLiteral("black_frames"),
             QStringLiteral("silence_detection"), QStringLiteral("resolution_check"),
             QStringLiteral("all")});
        addTool(QStringLiteral("probe_quality"),
                QStringLiteral("Run quality assurance checks on the timeline: audio clipping, black frames, "
                               "silent segments, resolution mismatches."),
                makeParams(props, {}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // PLAYBACK & NAVIGATION
    // ════════════════════════════════════════════════════════════════════════

    // 21. seek_to — move playhead to a specific frame
    {
        QJsonObject props;
        props[QStringLiteral("frame")] = intParam(QStringLiteral(
            "Frame to seek to."));
        addTool(QStringLiteral("seek_to"),
                QStringLiteral("Move the timeline playhead to a specific frame position."),
                makeParams(props, {QStringLiteral("frame")}));
    }

    // 22. set_zone — set in/out zone for render or preview
    {
        QJsonObject props;
        props[QStringLiteral("in_frame")] = intParam(QStringLiteral("Zone in-point frame."));
        props[QStringLiteral("out_frame")] = intParam(QStringLiteral("Zone out-point frame."));
        addTool(QStringLiteral("set_zone"),
                QStringLiteral("Set the timeline in/out zone for preview rendering or export."),
                makeParams(props, {QStringLiteral("in_frame"), QStringLiteral("out_frame")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // TRANSCRIPT & SPEECH (ported from Velo find_transcript / speech editing)
    // ════════════════════════════════════════════════════════════════════════

    // 23. find_transcript — search speech transcript to locate content
    {
        QJsonObject props;
        props[QStringLiteral("query")] = stringParam(QStringLiteral(
            "Search query to find in the speech transcript (word, phrase, or regex)."));
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral(
            "Clip ID to search. -1 = search all clips with transcripts."));
        addTool(QStringLiteral("find_transcript"),
                QStringLiteral("Search speech transcripts to find where specific words or phrases are spoken. "
                               "Returns frame ranges for each match."),
                makeParams(props, {QStringLiteral("query")}));
    }

    // 24. generate_transcript — transcribe audio to text
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral(
            "Clip ID to transcribe. -1 = transcribe entire project audio."));
        props[QStringLiteral("language")] = stringParam(QStringLiteral(
            "Language code (e.g., 'en', 'es', 'fr'). Empty = auto-detect."));
        addTool(QStringLiteral("generate_transcript"),
                QStringLiteral("Generate a speech transcript for a clip's audio using speech-to-text. "
                               "Enables transcript-based editing (cut by words)."),
                makeParams(props, {}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // EXPORT & RENDER
    // ════════════════════════════════════════════════════════════════════════

    // 25. render_project — export final video
    {
        QJsonObject props;
        props[QStringLiteral("preset")] = enumParam(
            QStringLiteral("Render preset."),
            {QStringLiteral("h264_1080p"), QStringLiteral("h264_4k"), QStringLiteral("prores_422"),
             QStringLiteral("webm_vp9"), QStringLiteral("gif"), QStringLiteral("custom")});
        props[QStringLiteral("output_path")] = stringParam(QStringLiteral(
            "Output file path. Empty = auto-generate in project directory."));
        props[QStringLiteral("zone_only")] = boolParam(QStringLiteral(
            "If true, render only the in/out zone. False = render full timeline."));
        addTool(QStringLiteral("render_project"),
                QStringLiteral("Render/export the timeline to a video file with the specified codec preset."),
                makeParams(props, {}));
    }

    // 26. undo_last — undo the last action
    {
        QJsonObject props;
        props[QStringLiteral("count")] = intParam(QStringLiteral(
            "Number of undo steps (default: 1)."));
        addTool(QStringLiteral("undo_last"),
                QStringLiteral("Undo the last N editing actions."),
                makeParams(props, {}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // STOCK MEDIA & AI VOICEOVER GENERATION
    // ════════════════════════════════════════════════════════════════════════

    // 27. search_stock_media — search Pexels, Pixabay, Giphy, Freesound
    {
        QJsonObject props;
        props[QStringLiteral("query")] = stringParam(QStringLiteral("Search query (e.g. 'cinematic drone mountains', 'whoosh', 'neon abstract')."));
        props[QStringLiteral("kind")] = enumParam(
            QStringLiteral("Asset category to search."),
            {QStringLiteral("video"), QStringLiteral("image"), QStringLiteral("sfx"), QStringLiteral("gif")});
        addTool(QStringLiteral("search_stock_media"),
                QStringLiteral("Search multi-provider stock media library (Pexels, Pixabay, Giphy, Freesound)."),
                makeParams(props, {QStringLiteral("query")}));
    }

    // 28. generate_voiceover — ElevenLabs text-to-speech synthesis
    {
        QJsonObject props;
        props[QStringLiteral("text")] = stringParam(QStringLiteral("Script / spoken dialogue to synthesize."));
        props[QStringLiteral("voice_name")] = stringParam(QStringLiteral("Speaker voice name (e.g. 'Rachel', 'Adam', 'Antoni', 'Bella', 'Arnold')."));
        props[QStringLiteral("speed")] = numberParam(QStringLiteral("Voice playback speed multiplier (default: 1.0)."));
        props[QStringLiteral("stability")] = numberParam(QStringLiteral("Voice stability 0.0 to 1.0 (default: 0.5)."));
        props[QStringLiteral("track_id")] = intParam(QStringLiteral("Audio track ID to place the synthesized clip onto (-1 = active track)."));
        props[QStringLiteral("playhead_frame")] = intParam(QStringLiteral("Timeline frame to place clip at (-1 = current playhead)."));
        addTool(QStringLiteral("generate_voiceover"),
                QStringLiteral("Synthesize professional human voiceover via ElevenLabs neural TTS and insert to audio track."),
                makeParams(props, {QStringLiteral("text")}));
    }

    // 29. insert_media_url — download external asset and insert to timeline
    {
        QJsonObject props;
        props[QStringLiteral("url")] = stringParam(QStringLiteral("Public HTTP(S) URL of media file (mp4, jpg, mp3, gif)."));
        props[QStringLiteral("name")] = stringParam(QStringLiteral("Descriptive name for the asset in Project Bin."));
        props[QStringLiteral("kind")] = enumParam(
            QStringLiteral("Media kind."),
            {QStringLiteral("video"), QStringLiteral("image"), QStringLiteral("audio"), QStringLiteral("gif")});
        props[QStringLiteral("track_id")] = intParam(QStringLiteral("Track ID to insert onto (-1 = active track)."));
        props[QStringLiteral("playhead_frame")] = intParam(QStringLiteral("Timeline frame to insert at (-1 = playhead)."));
        addTool(QStringLiteral("insert_media_url"),
                QStringLiteral("Download remote stock/AI media asset, add to Project Bin, and insert onto timeline track."),
                makeParams(props, {QStringLiteral("url")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // AI VISUAL ANALYSIS & SCENE CUT DETECTION (PySceneDetect)
    // ════════════════════════════════════════════════════════════════════════

    // 30. detect_scenes — visual scene/shot boundary detection via PySceneDetect
    {
        QJsonObject props;
        props[QStringLiteral("clip_id")] = intParam(QStringLiteral(
            "Clip ID to analyze. Use -1 for currently selected clip or active timeline clip."));
        props[QStringLiteral("threshold")] = numberParam(QStringLiteral(
            "Detection threshold sensitivity (default: 27.0). Lower = more sensitive to subtle cuts."));
        props[QStringLiteral("detector")] = enumParam(
            QStringLiteral("Scene detection algorithm."),
            {QStringLiteral("content"), QStringLiteral("adaptive"), QStringLiteral("threshold")});
        props[QStringLiteral("apply_cuts")] = boolParam(QStringLiteral(
            "If true, automatically split/cut the clip on the timeline at each detected scene boundary (default: true)."));
        props[QStringLiteral("add_markers")] = boolParam(QStringLiteral(
            "If true, add scene guide markers with timestamps at each shot transition (default: false)."));
        addTool(QStringLiteral("detect_scenes"),
                QStringLiteral("Visually analyze a video clip using PySceneDetect to detect scene/shot changes, and optionally split the clip or place markers."),
                makeParams(props, {}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // PROCEDURAL GLSL / SHADERTOY GPU CODE-TO-VIDEO
    // ════════════════════════════════════════════════════════════════════════

    // 31. generate_glsl_shader — GPU-accelerated ShaderToy / SDF procedural generator
    {
        QJsonObject props;
        props[QStringLiteral("glsl_code")] = stringParam(QStringLiteral(
            "GLSL shader code or math logic function (ShaderToy mainImage or Raymarch map(vec3 p) & computeMaterial)."));
        props[QStringLiteral("shader_format")] = enumParam(
            QStringLiteral("Shader template format."),
            {QStringLiteral("shadertoy"), QStringLiteral("raymarch_sdf"), QStringLiteral("raw_fragment")});
        props[QStringLiteral("name")] = stringParam(QStringLiteral(
            "Descriptive name for the procedural shader effect or background."));
        props[QStringLiteral("duration_frames")] = intParam(QStringLiteral(
            "Duration in timeline frames (default: 300 = 10s at 30fps)."));
        props[QStringLiteral("track_id")] = intParam(QStringLiteral(
            "Timeline track ID to insert onto (-1 = active track)."));
        props[QStringLiteral("playhead_frame")] = intParam(QStringLiteral(
            "Timeline frame position to insert at (-1 = current playhead)."));
        props[QStringLiteral("audio_reactive")] = boolParam(QStringLiteral(
            "If true, binds the shader's iAudioLevels uniform to the timeline audio master track FFT spectrum."));
        addTool(QStringLiteral("generate_glsl_shader"),
                QStringLiteral("Compile, validate on GPU offscreen, and render high-performance procedural GLSL/ShaderToy animations at 60+ FPS with automatic driver error self-healing."),
                makeParams(props, {QStringLiteral("glsl_code")}));
    }

    // ════════════════════════════════════════════════════════════════════════
    // PERSISTENT MEMORY & CONTEXT TOOLS (inspired by agent.cpp)
    // ════════════════════════════════════════════════════════════════════════

    // 31. write_memory — save user style, editing rules, project constraints across sessions
    {
        QJsonObject props;
        props[QStringLiteral("key")] = stringParam(QStringLiteral(
            "Unique identifier for the memory (e.g., 'user_preferred_font', 'ducking_db', 'character_name')."));
        props[QStringLiteral("value")] = stringParam(QStringLiteral(
            "Information or rule to remember across conversations and sessions."));
        addTool(QStringLiteral("write_memory"),
                QStringLiteral("Persist a user editing preference, style rule, project guideline, or fact to long-term memory."),
                makeParams(props, {QStringLiteral("key"), QStringLiteral("value")}));
    }

    // 32. read_memory — retrieve saved memory
    {
        QJsonObject props;
        props[QStringLiteral("key")] = stringParam(QStringLiteral("The memory key to retrieve."));
        addTool(QStringLiteral("read_memory"),
                QStringLiteral("Retrieve previously saved memory by its key."),
                makeParams(props, {QStringLiteral("key")}));
    }

    // 33. list_memory_keys — list all known memory keys
    {
        QJsonObject props;
        addTool(QStringLiteral("list_memory_keys"),
                QStringLiteral("List all known keys stored in long-term persistent agent memory."),
                makeParams(props, {}));
    }
}

// ── API ─────────────────────────────────────────────────────────────────────

QJsonArray AIToolRegistry::toolSchemas() const
{
    QJsonArray tools;
    for (const auto &td : m_tools) {
        QJsonObject func;
        func[QStringLiteral("name")] = td.name;
        func[QStringLiteral("description")] = td.description;
        func[QStringLiteral("parameters")] = td.parameters;

        QJsonObject tool;
        tool[QStringLiteral("type")] = QStringLiteral("function");
        tool[QStringLiteral("function")] = func;
        tools.append(tool);
    }
    return tools;
}

QString AIToolRegistry::toolDescriptionsForPrompt() const
{
    QString result;
    for (const auto &td : m_tools) {
        result += QStringLiteral("- **%1** [%2]: %3\n").arg(td.name, td.targetEngine, td.description);

        // List parameters
        QJsonObject props = td.parameters[QStringLiteral("properties")].toObject();
        for (auto it = props.begin(); it != props.end(); ++it) {
            QJsonObject paramObj = it.value().toObject();
            QString type = paramObj[QStringLiteral("type")].toString();
            QString desc = paramObj[QStringLiteral("description")].toString();
            result += QStringLiteral("  - `%1` (%2): %3\n").arg(it.key(), type, desc);
        }
    }
    return result;
}

QStringList AIToolRegistry::toolNames() const
{
    QStringList names;
    for (const auto &td : m_tools) {
        names.append(td.name);
    }
    return names;
}

QJsonObject AIToolRegistry::toolSchema(const QString &name) const
{
    for (const auto &td : m_tools) {
        if (td.name == name) {
            QJsonObject func;
            func[QStringLiteral("name")] = td.name;
            func[QStringLiteral("description")] = td.description;
            func[QStringLiteral("parameters")] = td.parameters;
            return func;
        }
    }
    return {};
}
