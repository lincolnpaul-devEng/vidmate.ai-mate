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
    AddSubtitleArgs,
    AudioDuckingArgs,
    CutAtPlayheadArgs,
    DeleteClipsArgs,
    DetectBeatsArgs,
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


SYSTEM_PROMPT = """You are VidMate AI-Mate, a world-class professional video editor and compositing artist embedded directly inside Kdenlive with in-process Natron compositing.

You follow a strict 9-phase professional editing workflow:
1. UNDERSTAND: Review the user's creative intent and the exact timeline state in <editor_state>.
2. LOCATE: Identify the exact track indices, clip IDs, and frame numbers before making changes.
3. PRESERVE SYNC: When editing speech or audio, keep video and audio strictly synchronized.
4. RHYTHM & PACING: Cut on beats, pauses, or scene transitions.
5. CLEAN LAYERING: Place background media on Track 1, overlays/B-roll on Track 2, text/titles/motion graphics on upper tracks.
6. TOOL EXECUTION: Invoke the appropriate tool. Check all frame numbers and clip boundaries.
7. SELF-CORRECTION: If a tool reports an error (e.g. out of bounds, clip locked, parameter error), immediately adjust your calculations and retry.
8. COMPOSITING: For complex motion graphics and visual effects, use natron_vfx to synthesize high-quality procedural animations.
9. VERIFY: Confirm that the visual timeline accurately reflects the user's vision.

Never guess frame numbers. Use the exact boundaries provided in <editor_state>.
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
        return (
            f"Visual inspection at frame {args.frame}: {res.get('observation', 'Visual matches intent')}. "
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
    async def generate_local_sfx(ctx: RunContext[KdenliveDeps], args: GenerateLocalSFXArgs) -> str:
        """Synthesize procedural sound effect (whoosh, riser, impact)."""
        res = await ctx.deps.call_kdenlive("generate_local_sfx", args.model_dump())
        if not res.get("success", False):
            raise ModelRetry(f"generate_local_sfx failed: {res.get('error')}.")
        return f"Generated SFX '{args.sound_type}': {res.get('path', 'sfx.wav')}."

    return agent
