"""VidMate AI-Mate Agent implementation using PydanticAI with self-correction."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any, Callable, Coroutine, Dict, Optional

from pydantic_ai import Agent, RunContext
from pydantic_ai.exceptions import ModelRetry

from .config import AgentConfig
from .providers import create_agent_model
from .schemas import (
    AddEffectArgs,
    AddMixArgs,
    AddSubtitleArgs,
    AddTransitionArgs,
    AudioDuckingArgs,
    CutAtPlayheadArgs,
    DeleteClipsArgs,
    DetectBeatsArgs,
    DetectScenesArgs,
    GenerateLocalSFXArgs,
    InsertClipArgs,
    InsertNatronToolArgs,
    InsertTitleArgs,
    MoveClipArgs,
    NatronVFXArgs,
    RemoveBackgroundArgs,
    RemoveEffectArgs,
    SeekToArgs,
    SetClipSpeedArgs,
    SetEffectParameterArgs,
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

# CORE OPERATING DIRECTIVES (PLAN -> ACT -> OBSERVE -> CRITIC / VERIFY):
1. **EXACT TIMELINE AWARENESS**:
   - Inspect `<timeline_clips>` in `<editor_state>` before taking any action.
   - Note the exact active clip IDs, track IDs, start frames, and duration (+frames).
   - NEVER call `cut_at_playhead` at frame 0 or outside clip boundaries. Cut strictly within [clip.start, clip.start + clip.duration].
   - If user asks to "cut the video and detect scenes", DO NOT guess frame cuts manually. Call `detect_scenes` on the clip!
   - When cutting or applying effects, specify the exact target `clip_id` or seek the playhead (`seek_to`) to the target frame first.

2. **REASONING & INTENT**:
   - For every step, briefly explain your creative reasoning (what you plan to do, which clip you are targeting, and why).

3. **CRITIC & VERIFY**:
   - When calling `verify_edit_visually`, check the observation. If it reports `WARNING (Black frame detected)` or unexpected results, DO NOT declare success! Investigate: did you seek to an empty frame? Was the clip ID wrong? Correct your parameters immediately.
   - If any tool returns an error (e.g. `No clip selected`, `out of bounds`), read the error, adjust parameters, and fix it.

4. **FINAL PROFESSIONAL SUMMARY**:
   - When all edits are completed, provide a concise, structured markdown summary:
     * **Clips Cut**: List clip IDs, timecodes, and split frames.
     * **Transitions / Effects Added**: Specify tracks, transition types, and durations.
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
    async def set_effect_parameter(ctx: RunContext[KdenliveDeps], args: SetEffectParameterArgs) -> str:
        """Adjust a parameter on an applied effect."""
        res = await ctx.deps.call_kdenlive("set_effect_parameter", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"set_effect_parameter failed: {res.get('error')}.")
        return f"Set '{args.param_name}' to {args.value} on clip {args.clip_id}."

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
        """Create and place a title clip overlay."""
        res = await ctx.deps.call_kdenlive("insert_title", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"insert_title failed: {res.get('error')}.")
        return f"Created title '{args.text}' on track {args.track} at frame {args.position}."

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
    async def verify_edit_visually(ctx: RunContext[KdenliveDeps], args: VerifyEditVisuallyArgs) -> str:
        """Inspect the project monitor frame to verify the edit result visually."""
        res = await ctx.deps.call_kdenlive("verify_edit_visually", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"Visual verification failed: {res.get('error')}.")

        obs = res.get("observation", "")
        status = res.get("status", "")
        # Enforce Critic: If black frame or flaw detected, self-correct!
        if "WARNING" in status or "Black frame detected" in obs or "empty/black" in obs:
            raise ModelRetry(
                f"Visual verification warning at frame {args.frame}: {obs}. "
                "The frame is black or empty. Ensure playhead is within an active clip timeframe on the timeline."
            )

        return (
            f"Visual inspection at frame {args.frame}: {obs or 'Visual matches intent'}. "
            f"Quality confidence: {res.get('confidence', 'high')}."
        )

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
    async def generate_local_sfx(ctx: RunContext[KdenliveDeps], args: GenerateLocalSFXArgs) -> str:
        """Synthesize procedural sound effect (whoosh, riser, impact)."""
        res = await ctx.deps.call_kdenlive("generate_local_sfx", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"generate_local_sfx failed: {res.get('error')}.")
        return f"Generated SFX '{args.sound_type}': {res.get('path', 'sfx.wav')}."

    return agent
