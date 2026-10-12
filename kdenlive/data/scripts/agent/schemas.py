"""Strongly-typed Pydantic schemas for Kdenlive and Natron editing actions."""

from __future__ import annotations

from typing import Any, Dict, List, Literal, Optional
from pydantic import BaseModel, Field


# ─── Timeline Editing Models ───────────────────────────────────────────────────

class CutAtPlayheadArgs(BaseModel):
    """Cut clips at a specific position or playhead."""
    position: int = Field(default=-1, description="Frame position to cut at (-1 for current playhead).")
    clip_id: int = Field(default=-1, description="Optional clip ID to target specifically (-1 for all tracks).")


class DeleteClipsArgs(BaseModel):
    """Delete clips from the timeline."""
    clip_ids: List[int] = Field(default_factory=list, description="List of clip IDs to remove.")
    ripple: bool = Field(default=False, description="Ripple delete to close gaps left by removed clips.")


class TrimClipArgs(BaseModel):
    """Trim in/out points of a clip."""
    clip_id: int = Field(description="ID of the clip to trim.")
    new_in: Optional[int] = Field(default=None, description="New in-point frame relative to source media.")
    new_out: Optional[int] = Field(default=None, description="New out-point frame relative to source media.")


class MoveClipArgs(BaseModel):
    """Move a clip to a different track or start frame."""
    clip_id: int = Field(description="ID of the clip to move.")
    target_track: int = Field(description="Target track index.")
    target_position: int = Field(description="Target start frame position on the timeline.")


class SetClipSpeedArgs(BaseModel):
    """Change the playback speed of a clip."""
    clip_id: int = Field(description="ID of the clip.")
    speed: float = Field(description="Speed multiplier (e.g., 0.5 for half speed, 2.0 for 2x).")
    pitch_compensate: bool = Field(default=True, description="Maintain audio pitch when changing speed.")


class InsertClipArgs(BaseModel):
    """Insert media into the timeline from the bin or an external path."""
    bin_clip_id: str = Field(description="Bin clip ID (or media path) to insert.")
    track_id: int = Field(description="Track index to place the clip on.")
    position: int = Field(description="Start frame position on the timeline.")


# ─── Effects & Grading Models ─────────────────────────────────────────────────

class AddEffectArgs(BaseModel):
    """Apply an MLT/Frei0r/Avfilter effect to a clip."""
    clip_id: int = Field(description="ID of the clip to apply effect to.")
    effect_id: str = Field(
        description="ID of the effect (e.g. 'transform', 'frei0r.glow', 'lift_gamma_gain', 'avfilter.perspective')."
    )
    parameters: Dict[str, Any] = Field(default_factory=dict, description="Initial parameter values.")


class GetEffectParametersArgs(BaseModel):
    """Inspect parameter schema, slider ranges (min/max), defaults, and current values for an effect or composition."""
    clip_id: int = Field(default=-1, description="Clip ID to inspect (-1 if inspecting track or composition).")
    track_id: int = Field(default=-2, description="Track ID to inspect (-2 if inspecting clip, -1 for Master).")
    composition_id: int = Field(default=-1, description="Composition / Transition ID to inspect.")
    effect_id: str = Field(default="", description="Effect identifier (e.g. 'frei0r.cartoon', 'qtblend', 'lift_gamma_gain').")


class SetEffectParameterArgs(BaseModel):
    """Tweak, slider-adjust, or keyframe an effect or composition parameter."""
    clip_id: int = Field(default=-1, description="Clip ID (-1 if modifying track or composition).")
    track_id: int = Field(default=-2, description="Track ID (-2 if modifying clip, -1 for Master).")
    composition_id: int = Field(default=-1, description="Composition / Transition ID (if modifying composition).")
    effect_id: str = Field(default="", description="Effect identifier (e.g. 'frei0r.cartoon', 'qtblend', 'lift_gamma_gain').")
    param_name: str = Field(default="", description="Specific parameter/slider name to modify (e.g. 'triplevel', 'diffspace', 'rect').")
    value: Any = Field(default=None, description="New value or keyframe string.")
    parameters: Dict[str, Any] = Field(default_factory=dict, description="Dictionary of multiple parameters to set simultaneously (e.g. {'triplevel': 1.0, 'diffspace': 0.02}).")


class RemoveEffectArgs(BaseModel):
    """Remove an effect from a clip."""
    clip_id: int = Field(description="ID of the clip.")
    effect_index: int = Field(default=0, description="Index of the effect to remove.")


class RemoveBackgroundArgs(BaseModel):
    """Isolate human foreground using AI Robust Video Matting (RVM)."""
    clip_id: int = Field(description="ID of the clip to apply matting to.")
    precision: str = Field(default="fp16", description="Inference precision ('fp16' or 'fp32').")


# ─── Audio & Subtitles Models ─────────────────────────────────────────────────

class SetVolumeArgs(BaseModel):
    """Adjust audio volume of a clip."""
    clip_id: int = Field(description="ID of the clip.")
    gain_db: float = Field(description="Volume gain in dB (e.g., -6.0 or +3.0).")


class AudioDuckingArgs(BaseModel):
    """Automatically duck background music when voice/dialogue is present."""
    voice_track: int = Field(default=1, description="Track containing dialogue.")
    music_track: int = Field(default=2, description="Track containing background music.")
    attenuation_db: float = Field(default=-14.0, description="Attenuation amount in dB during speech.")


class AddSubtitleArgs(BaseModel):
    """Insert a subtitle block on the timeline."""
    start_frame: int = Field(description="Start frame position.")
    end_frame: int = Field(description="End frame position.")
    text: str = Field(description="Subtitle text to display.")


class InsertTitleArgs(BaseModel):
    """Insert a mathematically aligned title card or lower-third overlay enforcing 60/30/10 Color Rule and Rule of Thirds snapping."""
    text: str = Field(description="Primary title text content.")
    subtext: Optional[str] = Field(default=None, description="Optional secondary subtitle or speaker attribution.")
    placement: Literal[
        "lower_third", "lower_third_left", "lower_third_center", "lower_third_right",
        "upper_third", "center",
        "rule_of_thirds_tl", "rule_of_thirds_tr", "rule_of_thirds_bl", "rule_of_thirds_br"
    ] = Field(default="lower_third", description="Geometric snapping rule (Rule of Thirds / Golden Ratio).")
    color_dominant: Optional[str] = Field(default=None, description="60% Dominant color for card backdrop (e.g. '#141416e0').")
    color_secondary: Optional[str] = Field(default=None, description="30% Secondary color for card border and subtext (e.g. '#dcdcdc').")
    color_accent: Optional[str] = Field(default=None, description="10% Accent color for punchy title text (e.g. '#ffbb00', '#3daee9', '#ff4b4b').")
    font_size: int = Field(default=56, description="Base font size in points.")
    font_family: str = Field(default="DejaVu Sans", description="Font family name.")
    duration_frames: int = Field(default=90, description="Duration in frames.")
    position: int = Field(default=-1, description="Start frame on timeline (-1 for playhead cursor).")
    track_id: int = Field(default=-1, description="Video track ID (-1 for top video track).")
    safe_margin_percent: int = Field(default=80, description="Title Safe boundary percentage (default 80%).")


# ─── Natron VFX & Compositing Models ──────────────────────────────────────────

class NatronVFXArgs(BaseModel):
    """Generate or render an advanced VFX composite using Natron."""
    prompt: str = Field(description="VFX description or instructions (motion graphic, 3D text, particle flare).")
    duration_frames: int = Field(default=120, description="Duration of the composite in frames.")
    template_type: str = Field(default="motion_graphic", description="Template category.")
    custom_params: Dict[str, Any] = Field(default_factory=dict, description="Custom node graph properties.")


class InsertNatronToolArgs(BaseModel):
    """Insert a Natron compositing node (e.g. Blur, Transform, Merge, Roto, Grade)."""
    tool_type: str = Field(description="Natron node type (e.g. 'Blur', 'Grade', 'Transform', 'Roto').")
    inputs: List[str] = Field(default_factory=list, description="Input node names to connect.")
    properties: Dict[str, Any] = Field(default_factory=dict, description="Node properties.")


class GetNatronNodeParametersArgs(BaseModel):
    """Inspect active nodes and knob parameter values in Natron's in-process compositor node graph."""
    node_id: Optional[str] = Field(default=None, description="Specific node ID to inspect (e.g. 'Grade1', 'Blur1', 'Tracker1'). Leave None to list all nodes.")


class SetNatronNodeParameterArgs(BaseModel):
    """Adjust a parameter knob on a node in Natron's in-process compositor node graph."""
    node_id: str = Field(description="Target node ID (e.g. 'Grade1', 'Blur1', 'Transform1', 'Tracker1').")
    param_name: str = Field(description="Parameter knob name (e.g. 'gamma', 'contrast', 'brightness', 'saturation', 'gain', 'size', 'scale', 'rotate').")
    value: Any = Field(description="New parameter knob value (number, string, or boolean).")


class TrackObjectArgs(BaseModel):
    """Track an object across frames using OpenCV tracker."""
    clip_id: int = Field(description="ID of the clip.")
    roi: List[int] = Field(description="Bounding box [x, y, width, height] in normalized coordinates.")


# ─── Navigation, State & Quality Inspection Models ────────────────────────────

class SeekToArgs(BaseModel):
    """Move project playhead cursor."""
    position: int = Field(description="Target frame position.")


class VerifyEditVisuallyArgs(BaseModel):
    """Request visual frame inspection from the project monitor to verify quality."""
    frame: int = Field(default=-1, description="Frame number to inspect (-1 for current playhead).")
    expected_element: str = Field(description="Description of what should be visually verified.")


class DetectBeatsArgs(BaseModel):
    """Detect musical beats and downbeats on an audio clip."""
    clip_id: int = Field(description="Audio clip ID to analyze.")


class DetectScenesArgs(BaseModel):
    """Visually analyze a video clip using PySceneDetect to detect scene/shot changes and automatically split the clip."""
    clip_id: int = Field(default=-1, description="Clip ID to analyze (-1 for selected or active timeline clip).")
    threshold: float = Field(default=27.0, description="Detection threshold sensitivity (lower = more sensitive to subtle cuts).")
    detector: str = Field(default="content", description="Scene detection algorithm: 'content', 'adaptive', or 'threshold'.")
    apply_cuts: bool = Field(default=True, description="If true, automatically split/cut the clip on the timeline at each detected scene boundary.")
    add_markers: bool = Field(default=False, description="If true, add scene guide markers with timestamps at each shot transition.")


class AddTransitionArgs(BaseModel):
    """Add a transition or composition between overlapping clips on a track."""
    track_id: int = Field(description="Track ID for the composition.")
    position: int = Field(description="Frame position to place the transition.")
    transition_id: str = Field(default="dissolve", description="Transition type: 'wipe', 'dissolve', 'slide', 'luma', 'composite'.")
    duration: int = Field(default=30, description="Duration in frames (e.g. 30 frames = 1 second at 30fps).")


class AddMixArgs(BaseModel):
    """Create a cross-dissolve mix between two adjacent clips at a cut point."""
    track_id: int = Field(description="Track ID.")
    position: int = Field(description="Frame position at the cut point between two adjacent clips.")
    transition_id: str = Field(default="luma", description="Mix transition type (e.g. 'luma').")


class CreateSplitEditArgs(BaseModel):
    """Create a cinematic split edit (J-cut or L-cut) where audio and video start or end at different times."""
    clip_id: int = Field(description="Clip ID on timeline.")
    type: str = Field(default="j_cut", description="Type of split edit: 'j_cut' (audio leads video) or 'l_cut' (audio lingers after video cut).")
    lead_frames: int = Field(default=12, description="Frame offset for the audio-video split (typically 8 to 24 frames).")


class GenerateLocalSFXArgs(BaseModel):
    """Synthesize a sound effect procedurally."""
    sound_type: str = Field(description="Type of SFX (e.g. 'whoosh', 'riser', 'glitch', 'impact', 'pop').")
    duration_seconds: float = Field(default=1.0, description="Duration of the sound in seconds.")


class SetEditorialStyleArgs(BaseModel):
    """Dynamically establish or adapt the editorial style, pacing, typography, and color palette for the editing session."""
    style_name: str = Field(
        default="custom",
        description="Name or archetype of the style: 'minimal_clean', 'cinematic_documentary', 'high_energy_viral', 'corporate_business', 'retro_analog', 'gaming_streamer', or 'custom'."
    )
    pacing_mood: str = Field(
        default="balanced",
        description="Pacing tempo: 'slow_deliberate' (4-8s cuts), 'balanced_narrative' (2.5-5s cuts), 'fast_paced' (1.2-2.5s cuts), 'hyper_montage' (<1s cuts)."
    )
    typography_intent: str = Field(
        default="modern_sans",
        description="Font and subtitle visual direction: 'modern_sans' (Inter/Helvetica), 'cinematic_serif' (Cinzel/Playfair), 'bold_display' (Montserrat Black/Impact), 'handwritten_script' (Artistic), or custom font family name."
    )
    color_palette: Dict[str, str] = Field(
        default_factory=lambda: {"dominant_60": "#0D0E12", "secondary_30": "#C89D5C", "accent_10": "#FF4500"},
        description="Dynamic 60/30/10 color distribution dictionary: 'dominant_60' (background/canvas), 'secondary_30' (cards/shapes/secondary), 'accent_10' (key text/highlights/punchline)."
    )
    transition_preference: str = Field(
        default="clean_cuts",
        description="Preferred transition language: 'clean_cuts' (invisible/J-cut/L-cut), 'cross_dissolve', 'whip_pan_zoom', 'glitch_digital', or 'luma_wipe'."
    )
    creative_intent_notes: str = Field(
        default="",
        description="Specific user desires, audience expectations, or thematic guidelines to respect."
    )

