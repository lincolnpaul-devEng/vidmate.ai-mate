"""VidMate AI-Mate Agent implementation using PydanticAI with self-correction."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any, Callable, Coroutine, Dict, Optional

from pydantic_ai import Agent, BinaryContent, RunContext
from pydantic_ai.exceptions import ModelRetry

from .config import AgentConfig
from .providers import create_agent_model
from .schemas import (
    AddEffectArgs,
    AddMixArgs,
    AddSubtitleArgs,
    AddTransitionArgs,
    AudioDuckingArgs,
    CreateSplitEditArgs,
    CutAtPlayheadArgs,
    DeleteClipsArgs,
    DetectBeatsArgs,
    DetectScenesArgs,
    GenerateLocalSFXArgs,
    GetEffectParametersArgs,
    GetNatronNodeParametersArgs,
    InsertClipArgs,
    InsertNatronToolArgs,
    InsertTitleArgs,
    MoveClipArgs,
    NatronVFXArgs,
    RemoveBackgroundArgs,
    RemoveEffectArgs,
    SeekToArgs,
    SetClipSpeedArgs,
    SetEditorialStyleArgs,
    SetEffectParameterArgs,
    SetNatronNodeParameterArgs,
    SetVolumeArgs,
    TrackObjectArgs,
    TrimClipArgs,
    VerifyEditVisuallyArgs,
)


@dataclass
class KdenliveDeps:
    """Execution dependencies passed into the agent on every run turn."""
    # Async callback to send JSON action commands to Kdenlive's AICommandRouter
    action_executor: Callable[[str, Dict[str, Any]], Coroutine[Any, Any, Dict[str, Any]]]
    editor_state: Dict[str, Any] = field(default_factory=dict)
    session_id: str = "default_session"

    async def call_kdenlive(self, action_name: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Send action to Kdenlive and return outcome dictionary."""
        return await self.action_executor(action_name, params)


SYSTEM_PROMPT = """You are VidMate AI-Mate, an elite master video editor and compositing director embedded directly inside Kdenlive with in-process Natron compositing.
You do not edit like a blind script robot; you edit with the intentionality, taste, rhythm, and visual design standards of a master post-production director.

# ABSOLUTE RULE — AUTONOMOUS AGENT EXECUTION (NOT A CONVERSATIONAL CHATBOT):
- You are NOT a passive conversational chatbot. You are an autonomous software editing agent operating a live Kdenlive workstation.
- When the user gives an editing request (e.g. cut, split, add title, apply effect, color grade, adjust volume, sync beats), you must IMMEDIATELY invoke the appropriate tools to perform the work.
- NEVER reply with conversational filler (e.g. "Sure! I can help you with that", "I would be happy to...", "First, you could consider...").
- NEVER describe what you intend to do in text without actually invoking the corresponding tools.
- Every editing request MUST trigger real tool actions that modify or inspect the timeline.
- After all tool actions execute and are visually verified on the timeline, return a concise, professional markdown summary of what was completed.

# MASTER EDITORIAL WORKFLOW (THE 8-PHASE DIRECTIVE):
Every video project is unique in theme, pace, color, and audience. Always adapt dynamically to the user's footage and genre rather than applying a rigid, one-size-fits-all formula. Progress through these editorial stages:
1. **INGESTION & BIN ARCHITECTURE**: Inspect timeline clips and project bins. Ensure assets are logically structured (Footage, B-Roll, SFX, Music, Graphics).
2. **ROUGH ASSEMBLY & PACING CUT**:
   - Clean dead air and silences.
   - Establish narrative flow and rhythm. Use J-cuts (audio leads visual) and L-cuts (audio lingers) to create seamless cinematic continuity instead of blunt straight cuts.
3. **HOOK & INTRO ANIMATION**: Craft an engaging opening (first 3-5s) with a visual hook, title card, or dynamic zoom ramp.
4. **BODY & B-ROLL PACING**: Enhance story progression with relevant B-roll, punch zooms, or motion graphics to sustain viewer retention.
5. **DYNAMIC VISUAL DESIGN & COLOR HARMONY**:
   - **Flexible 60/30/10 Color Distribution**: Allocate roughly 60% dominant canvas/background, 30% structural shapes/cards, and 10% high-contrast focal accent. Choose color palettes dynamically that fit the footage (warm cinematic, clean monochrome, neon cyberpunk, pastel documentary, or corporate neutral). Never lock into a single palette!
   - **Rule of Thirds Placement**: Anchor titles and focal subjects along the 1/3 or 2/3 compositional axes, within Title Safe margins (80% safe zone).
   - **Contrast & Hierarchy**: Establish visual contrast using size (3:1 headline ratio), color contrast, shape differences, and texture.
6. **MUSIC & RHYTHMIC SYNCHRONIZATION**: Detect downbeats and BPM (`detect_beats`) to cut video shots on rhythm and align transitions with musical drops.
7. **SOUND DESIGN & TRANSIENT SYNTHESIS**: Place acoustic layers (whooshes, risers, impacts via `generate_local_sfx` or SFX tracks) so the sound's peak transient lands on the visual transition. Ensure background music is dynamically ducked (`audio_ducking`) during speech.
8. **VISUAL QA & CRITIC REFLECTION**: Visually inspect the Program Monitor (`verify_edit_visually`) on modified frames to ensure colors pop, text is legible, and no black frames or artifacts exist.

# CORE OPERATING DIRECTIVES (PLAN -> ACT -> OBSERVE -> CRITIC / VERIFY):
1. **EXACT TIMELINE & ACTIVE CLIPS AWARENESS**:
   - Inspect `<editor_state>` before taking any action:
     * Check `active_clip_id` and `active_clips_under_playhead` to see what clip is currently under the playhead cursor.
     * Check `selected=[...]` for user-selected clips.
     * Check `<timeline_clips count=...>` for the complete list of all clips: `[clip <id>] track=<tid> ("<track_name>") "<filename>" @<start> +<duration>f (in=<in> out=<out>)`.
   - When user asks to cut, trim, or apply effects to "the video" or "the clip", target `active_clip_id` or the selected clip ID.
   - NEVER blindly seek to frame 0 or verify frame 0 unless a clip actually covers frame 0!
     * Check the clip's `@<start>` position in `<timeline_clips>`. If the footage begins at frame 5, frame 0 is an empty black gap.
     * Always seek to a valid frame inside the clip (e.g. `start + 15` or the clip's midpoint `start + duration // 2`).
   - If user asks to "detect scenes" or "split scenes":
     * Call `detect_scenes(clip_id=...)` with the target clip ID.
     * If PySceneDetect returns "detected 1 scene(s), applied 0 cuts", this means the footage is a SINGLE CONTINUOUS TAKE without visual cuts!
     * DO NOT repeatedly call `detect_scenes` in a loop.
     * Immediately report to the user that the footage is a single uncut continuous take, OR if the user asked to split/segment it, proceed to split the clip into rhythmic intervals (e.g. every 3-4 seconds: frame 120, 240, 360) using `cut_at_playhead`.
     * Inspect `<timeline_clips count=...>` to confirm that cuts exist before concluding.

2. **MANDATORY TWO-WAY EFFECT & VFX INSPECTION**:
   - Before tweaking effect parameters (such as `triplevel`, `diffspace`, `radius`, `gain`), call `get_effect_parameters` first to inspect the live slider names, ranges, and current values, then invoke `set_effect_parameter`.
   - When manipulating Natron compositing nodes (e.g. `Grade1`, `Blur1`, `Transform1`, `Tracker1`), call `get_natron_node_parameters` first to inspect active node IDs and tunable knobs, then use `set_natron_node_parameter` to adjust knobs.

3. **MATHEMATICAL VISUAL LAYOUT & TITLE CARDS**:
   - When creating titles or lower thirds via `insert_title`, always leverage the Mathematical Visual Layout Engine:
     * Choose an appropriate `placement` (`lower_third`, `upper_third`, `center`, `rule_of_thirds_tl`, etc.).
     * Respect the 60/30/10 Color Rule: pass `color_dominant` (60% backdrop), `color_secondary` (30% border/subtext), and `color_accent` (10% title highlight) tailored dynamically to the footage aesthetic.
     * The engine automatically enforces the 80% Title Safe boundary and Rule of Thirds geometric snapping.

4. **DYNAMIC STYLE & CREATIVE FREEDOM**:
   - You can call `set_editorial_style` to define or adjust the project's pacing, color palette, and font direction for the session based on user intent.
   - Adapt freely to any genre: Talking Head, Gaming, Documentary, Music Video, Cinematic Short, SaaS Product Demo, or Social Ad.

5. **CRITIC & VERIFY**:
   - When calling `verify_edit_visually`, check the observation. If it reports `WARNING (Black frame detected)` or unexpected results, DO NOT declare success! Investigate: did you seek to an empty frame? Was the clip ID wrong? Correct your parameters immediately.
   - If any tool returns an error, read the error, adjust parameters, and fix it.

6. **FINAL PROFESSIONAL SUMMARY**:
   - When all edits are completed, provide a concise, structured markdown summary:
     * **Editorial Style & Pacing**: State the creative direction and pacing rhythm used.
     * **Clips Cut & Assembled**: List clip IDs, timecodes, and split frames.
     * **Transitions / Effects / SFX Added**: Specify tracks, transition types, and durations.
     * **Visual QA**: Confirm visual verification passed without black frames or errors.
"""


def build_vidmate_agent(
    config: Optional[AgentConfig] = None,
    provider: Optional[str] = None,
    model_name: Optional[str] = None,
) -> Agent[KdenliveDeps, str]:
    """Construct a configured PydanticAI Agent with full tool suite and self-correction."""
    cfg = config or AgentConfig.load()
    model = create_agent_model(cfg, provider=provider, model_name=model_name)

    agent: Agent[KdenliveDeps, str] = Agent(
        model,
        deps_type=KdenliveDeps,
        system_prompt=SYSTEM_PROMPT,
        retries=3,  # Allow up to 3 automatic ModelRetry self-correction attempts
    )

    # ─── Timeline Editing Tools ────────────────────────────────────────────────

    @agent.tool
    async def cut_at_playhead(ctx: RunContext[KdenliveDeps], args: CutAtPlayheadArgs) -> str:
        """Cut clips on the timeline at a specific position or current playhead."""
        target_pos = args.position
        if target_pos != -1:
            await ctx.deps.call_kdenlive("seek_to", {"position": target_pos})
        res = await ctx.deps.call_kdenlive("cut_at_playhead", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(
                f"cut_at_playhead failed: {res.get('error', 'Unknown error')}. "
                "Check clip positions and frame bounds."
            )
        return f"Cut clips at frame {res.get('cut_position', args.position)}. Message: {res.get('message', 'Success')}"

    @agent.tool
    async def delete_clips(ctx: RunContext[KdenliveDeps], args: DeleteClipsArgs) -> str:
        """Delete specified clips from the timeline."""
        res = await ctx.deps.call_kdenlive("delete_clips", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"delete_clips failed: {res.get('error')}. Verify clip_ids exist.")
        return f"Successfully deleted clips {args.clip_ids} (ripple={args.ripple})."

    @agent.tool
    async def trim_clip(ctx: RunContext[KdenliveDeps], args: TrimClipArgs) -> str:
        """Trim the in or out point of a clip on the timeline."""
        if args.new_in != -1:
            await ctx.deps.call_kdenlive("seek_to", {"position": args.new_in})
        elif args.new_out != -1:
            await ctx.deps.call_kdenlive("seek_to", {"position": args.new_out})
        res = await ctx.deps.call_kdenlive("trim_clip", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"trim_clip failed: {res.get('error')}. Verify new_in/new_out boundaries.")
        return f"Trimmed clip {args.clip_id}: in={args.new_in}, out={args.new_out}."

    @agent.tool
    async def move_clip(ctx: RunContext[KdenliveDeps], args: MoveClipArgs) -> str:
        """Move a clip to a target track and start position."""
        res = await ctx.deps.call_kdenlive("move_clip", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"move_clip failed: {res.get('error')}. Check track availability and gaps.")
        return f"Moved clip {args.clip_id} to track {args.target_track} at frame {args.target_position}."

    @agent.tool
    async def set_clip_speed(ctx: RunContext[KdenliveDeps], args: SetClipSpeedArgs) -> str:
        """Change clip playback speed."""
        res = await ctx.deps.call_kdenlive("set_clip_speed", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"set_clip_speed failed: {res.get('error')}.")
        return f"Set speed of clip {args.clip_id} to {args.speed}x."

    @agent.tool
    async def insert_clip(ctx: RunContext[KdenliveDeps], args: InsertClipArgs) -> str:
        """Insert a media asset from the bin into a timeline track."""
        res = await ctx.deps.call_kdenlive("insert_clip", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"insert_clip failed: {res.get('error')}. Check bin_clip_id and track.")
        return f"Inserted clip {args.bin_clip_id} onto track {args.track_id} at frame {args.position}."

    # ─── Effects & Grading Tools ───────────────────────────────────────────────

    @agent.tool
    async def add_effect(ctx: RunContext[KdenliveDeps], args: AddEffectArgs) -> str:
        """Apply a video/audio effect to a clip."""
        res = await ctx.deps.call_kdenlive("add_effect", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"add_effect failed: {res.get('error')}. Verify effect_id '{args.effect_id}'.")
        return f"Applied effect '{args.effect_id}' to clip {args.clip_id}."

    @agent.tool
    async def get_effect_parameters(ctx: RunContext[KdenliveDeps], args: GetEffectParametersArgs) -> str:
        """Inspect the exact parameter schema, sliders, default values, min/max limits, and current values of an effect or composition."""
        res = await ctx.deps.call_kdenlive("get_effect_parameters", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"get_effect_parameters failed: {res.get('error')}. Verify clip_id or effect_id.")
        data = res.get("data", {})
        params = data.get("parameters", [])
        return (
            f"Effect/Composition '{args.effect_id or data.get('effect_id')}' parameters ({len(params)} found): "
            f"{params}."
        )

    @agent.tool
    async def set_effect_parameter(ctx: RunContext[KdenliveDeps], args: SetEffectParameterArgs) -> str:
        """Adjust or tweak slider controls, keyframes, toggles, or values on an applied effect or composition."""
        res = await ctx.deps.call_kdenlive("set_effect_parameter", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"set_effect_parameter failed: {res.get('error')}. Verify parameter name and limits.")
        return f"Successfully updated parameter(s) for '{args.effect_id}' on Target: {res.get('message', 'Updated successfully')}."

    @agent.tool
    async def remove_effect(ctx: RunContext[KdenliveDeps], args: RemoveEffectArgs) -> str:
        """Remove an effect from a clip."""
        res = await ctx.deps.call_kdenlive("remove_effect", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"remove_effect failed: {res.get('error')}.")
        return f"Removed effect index {args.effect_index} from clip {args.clip_id}."

    @agent.tool
    async def remove_background(ctx: RunContext[KdenliveDeps], args: RemoveBackgroundArgs) -> str:
        """Perform AI background removal using Robust Video Matting (RVM)."""
        res = await ctx.deps.call_kdenlive("remove_background", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"remove_background failed: {res.get('error')}.")
        return f"Background removed for clip {args.clip_id} using RVM ({args.precision})."

    # ─── Audio & Subtitles Tools ───────────────────────────────────────────────

    @agent.tool
    async def set_volume(ctx: RunContext[KdenliveDeps], args: SetVolumeArgs) -> str:
        """Set volume gain on a clip."""
        res = await ctx.deps.call_kdenlive("set_volume", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"set_volume failed: {res.get('error')}.")
        return f"Set volume of clip {args.clip_id} to {args.gain_db} dB."

    @agent.tool
    async def audio_ducking(ctx: RunContext[KdenliveDeps], args: AudioDuckingArgs) -> str:
        """Apply automatic audio ducking on background music during dialogue."""
        res = await ctx.deps.call_kdenlive("audio_ducking", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"audio_ducking failed: {res.get('error')}.")
        return f"Applied ducking: voice track {args.voice_track}, music track {args.music_track} ({args.attenuation_db} dB)."

    @agent.tool
    async def add_subtitle(ctx: RunContext[KdenliveDeps], args: AddSubtitleArgs) -> str:
        """Add a subtitle block to the subtitle track."""
        res = await ctx.deps.call_kdenlive("add_subtitle", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"add_subtitle failed: {res.get('error')}.")
        return f"Added subtitle from frame {args.start_frame} to {args.end_frame}: '{args.text}'."

    @agent.tool
    async def insert_title(ctx: RunContext[KdenliveDeps], args: InsertTitleArgs) -> str:
        """Create and place a mathematically aligned title card or lower-third overlay enforcing 60/30/10 Color Rule and Rule of Thirds snapping."""
        res = await ctx.deps.call_kdenlive("insert_title", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"insert_title failed: {res.get('error')}.")
        return (
            f"Created and inserted title '{args.text}' (Placement: {args.placement}, "
            f"Title-Safe verified) at frame {res.get('position', args.position)} on track {res.get('track_id', args.track_id)}."
        )

    # ─── Natron VFX & Compositing Tools ────────────────────────────────────────

    @agent.tool
    async def natron_vfx(ctx: RunContext[KdenliveDeps], args: NatronVFXArgs) -> str:
        """Generate procedural motion graphics or VFX composites using in-process Natron."""
        res = await ctx.deps.call_kdenlive("natron_vfx", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(
                f"natron_vfx composite generation failed: {res.get('error')}. "
                "Adjust prompt or template parameters and try again."
            )
        return (
            f"Synthesized Natron VFX: {res.get('output_path', 'clip_ready')} "
            f"({args.duration_frames} frames). Status: {res.get('message', 'Imported to bin')}"
        )

    @agent.tool
    async def insert_natron_tool(ctx: RunContext[KdenliveDeps], args: InsertNatronToolArgs) -> str:
        """Add a compositing node into the active Natron Node Graph."""
        res = await ctx.deps.call_kdenlive("insert_natron_tool", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"insert_natron_tool failed: {res.get('error')}.")
        return f"Inserted Natron node '{args.tool_type}' with connections {args.inputs}."

    @agent.tool
    async def get_natron_node_parameters(ctx: RunContext[KdenliveDeps], args: GetNatronNodeParametersArgs) -> str:
        """Inspect active nodes and parameter knob values in Natron's in-process compositor node graph."""
        res = await ctx.deps.call_kdenlive("get_natron_node_parameters", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"get_natron_node_parameters failed: {res.get('error')}.")
        return f"Natron node inspection: {res.get('message', 'Nodes retrieved')}"

    @agent.tool
    async def set_natron_node_parameter(ctx: RunContext[KdenliveDeps], args: SetNatronNodeParameterArgs) -> str:
        """Adjust a parameter knob on a node in Natron's in-process compositor node graph (e.g. Grade1.gamma, Blur1.size, Transform1.scale)."""
        res = await ctx.deps.call_kdenlive("set_natron_node_parameter", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"set_natron_node_parameter failed: {res.get('error')}. Inspect active nodes first using get_natron_node_parameters.")
        return f"Updated Natron node '{args.node_id}' knob '{args.param_name}' to {args.value}."

    @agent.tool
    async def track_object(ctx: RunContext[KdenliveDeps], args: TrackObjectArgs) -> str:
        """Track object or region across frames using OpenCV tracker."""
        res = await ctx.deps.call_kdenlive("track_object", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"track_object failed: {res.get('error')}.")
        return f"Tracking generated for clip {args.clip_id}. Keyframes: {res.get('keyframes_count', 0)}."

    # ─── Verification & Inspection Tools ───────────────────────────────────────

    @agent.tool
    async def seek_to(ctx: RunContext[KdenliveDeps], args: SeekToArgs) -> str:
        """Move playhead cursor on the timeline."""
        res = await ctx.deps.call_kdenlive("seek_to", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"seek_to failed: {res.get('error')}.")
        return f"Seeked playhead to frame {args.position}."

    @agent.tool
    async def verify_edit_visually(ctx: RunContext[KdenliveDeps], args: VerifyEditVisuallyArgs) -> list[Any] | str:
        """Inspect the project monitor frame to verify the edit result visually."""
        res = await ctx.deps.call_kdenlive("verify_edit_visually", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"Visual verification failed: {res.get('error')}.")

        obs = res.get("observation", res.get("message", ""))
        status = res.get("status", "")
        # Enforce Critic: If black frame or flaw detected, self-correct!
        if "WARNING" in status or "Black frame detected" in obs or "empty/black" in obs:
            raise ModelRetry(
                f"Visual verification warning at frame {args.frame}: {obs}. "
                "The frame is black or empty. Ensure playhead is within an active clip timeframe on the timeline."
            )

        summary_text = (
            f"Visual inspection at frame {args.frame}: {obs or 'Visual matches intent'}. "
            f"Quality confidence: {res.get('confidence', 'high')}. Inspect the rendered frame below."
        )

        image_path = res.get("image_path") or res.get("path")
        if image_path:
            import os
            if os.path.isfile(image_path):
                try:
                    with open(image_path, "rb") as f:
                        img_bytes = f.read()
                    mime = "image/jpeg" if image_path.lower().endswith((".jpg", ".jpeg")) else "image/png"
                    return [summary_text, BinaryContent(data=img_bytes, media_type=mime)]
                except Exception:
                    pass

        return summary_text

    @agent.tool
    async def detect_beats(ctx: RunContext[KdenliveDeps], args: DetectBeatsArgs) -> str:
        """Detect musical beats on an audio clip using neural beat detector."""
        res = await ctx.deps.call_kdenlive("detect_beats", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"detect_beats failed: {res.get('error')}.")
        return f"Detected {len(res.get('beats', []))} beats on clip {args.clip_id}."

    @agent.tool
    async def detect_scenes(ctx: RunContext[KdenliveDeps], args: DetectScenesArgs) -> str:
        """Visually analyze a video clip using PySceneDetect to detect scene/shot changes and automatically split the clip."""
        res = await ctx.deps.call_kdenlive("detect_scenes", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"detect_scenes failed: {res.get('error')}. Verify clip_id exists on timeline.")
        scenes = res.get("scenes", [])
        return f"Scene detection complete on clip {args.clip_id}. Found {len(scenes)} scenes/cuts: {scenes}."

    @agent.tool
    async def add_transition(ctx: RunContext[KdenliveDeps], args: AddTransitionArgs) -> str:
        """Add a transition or composition between overlapping clips on a track."""
        res = await ctx.deps.call_kdenlive("add_transition", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"add_transition failed: {res.get('error')}. Verify track_id and position overlap.")
        # Auto-seek to transition point for immediate visual feedback
        await ctx.deps.call_kdenlive("seek_to", {"position": args.position})
        return f"Added '{args.transition_id}' transition on Track {args.track_id} at frame {args.position} (duration={args.duration}f)."

    @agent.tool
    async def add_mix(ctx: RunContext[KdenliveDeps], args: AddMixArgs) -> str:
        """Create a cross-dissolve mix between two adjacent clips at a cut point."""
        res = await ctx.deps.call_kdenlive("add_mix", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"add_mix failed: {res.get('error')}. Verify cut point position between two clips.")
        await ctx.deps.call_kdenlive("seek_to", {"position": args.position})
        return f"Created mix '{args.transition_id}' on Track {args.track_id} at frame {args.position}."

    @agent.tool
    async def create_split_edit(ctx: RunContext[KdenliveDeps], args: CreateSplitEditArgs) -> str:
        """Create a cinematic split edit (J-cut or L-cut) where audio and video change at different times to smooth scene transitions."""
        res = await ctx.deps.call_kdenlive("create_split_edit", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"create_split_edit failed: {res.get('error')}. Verify clip_id exists on timeline.")
        return f"Created cinematic {args.type.upper()} on clip {args.clip_id} with {args.lead_frames} frames lead."

    @agent.tool
    async def generate_local_sfx(ctx: RunContext[KdenliveDeps], args: GenerateLocalSFXArgs) -> str:
        """Synthesize procedural sound effect (whoosh, riser, impact)."""
        res = await ctx.deps.call_kdenlive("generate_local_sfx", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"generate_local_sfx failed: {res.get('error')}.")
        return f"Generated SFX '{args.sound_type}': {res.get('path', 'sfx.wav')}."

    @agent.tool
    async def set_editorial_style(ctx: RunContext[KdenliveDeps], args: SetEditorialStyleArgs) -> str:
        """Establish or adapt the creative editorial style profile, color palette, and pacing preferences for this edit."""
        palette = args.color_palette
        summary = (
            f"Set editorial style to '{args.style_name}' with {args.pacing_mood} pacing. "
            f"Typography: {args.typography_intent}. Color Palette: 60%={palette.get('dominant_60')}, "
            f"30%={palette.get('secondary_30')}, 10%={palette.get('accent_10')}. "
            f"Preferred transitions: {args.transition_preference}."
        )
        return summary

    return agent
