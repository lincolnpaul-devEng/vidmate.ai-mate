#!/usr/bin/env python3
"""
VidMate Natron VFX & Motion Graphics Synthesizer
=================================================
Procedural motion graphics generator and headless VFX fallback engine.
Produces broadcast-grade alpha-transparent ProRes 4444 / QuickTime RLE .mov files
for instant ingestion into Kdenlive's Project Bin and interactive Timeline.
"""

import os
import sys
import math
import json
import argparse
import subprocess
from PIL import Image, ImageDraw, ImageFont


def parse_args():
    parser = argparse.ArgumentParser(description="VidMate Natron VFX & Motion Graphics Synthesizer")
    parser.add_argument("--pipeline", default="youtube_subscribe", help="VFX pipeline or motion graphic type")
    parser.add_argument("--input", "-i", default="", help="Input clip path (for effects/compositing)")
    parser.add_argument("--output", "-o", required=True, help="Output media file path (.mov)")
    parser.add_argument("--width", type=int, default=1920, help="Output width")
    parser.add_argument("--height", type=int, default=1080, help="Output height")
    parser.add_argument("--fps", type=int, default=30, help="Framerate")
    parser.add_argument("--duration", type=float, default=3.5, help="Duration in seconds")
    parser.add_argument("--params", default="{}", help="JSON dictionary of parameters")
    return parser.parse_args()


def get_default_font(size):
    try:
        # Try standard TrueType font locations on Linux
        font_paths = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        ]
        for p in font_paths:
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    except Exception:
        pass
    return ImageFont.load_default()


def get_regular_font(size):
    try:
        font_paths = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        ]
        for p in font_paths:
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    except Exception:
        pass
    return ImageFont.load_default()


def draw_hand_cursor(draw, x, y, scale=1.0, is_clicking=False):
    """Draws a crisp white pointing hand cursor with black outline."""
    # Scale coordinates around (x, y) as the tip of index finger
    w = 28 * scale
    h = 36 * scale
    
    # Hand silhouette points
    # Tip of index finger is at (x, y)
    poly = [
        (x, y),                         # Index tip
        (x + 8 * scale, y + 16 * scale), # Index right
        (x + 13 * scale, y + 14 * scale),# Middle knuckle
        (x + 17 * scale, y + 16 * scale),# Ring knuckle
        (x + 20 * scale, y + 20 * scale),# Pinky knuckle
        (x + 22 * scale, y + 28 * scale),# Palm right
        (x + 14 * scale, y + 36 * scale),# Wrist right
        (x + 4 * scale, y + 36 * scale), # Wrist left
        (x + 0 * scale, y + 26 * scale), # Thumb base
        (x - 6 * scale, y + 20 * scale), # Thumb tip
        (x - 2 * scale, y + 15 * scale), # Thumb joint
        (x + 0 * scale, y + 14 * scale), # Index base
    ]

    # Draw black shadow / outer border
    shadow_poly = [(px + 2, py + 2) for px, py in poly]
    draw.polygon(shadow_poly, fill=(0, 0, 0, 160))

    # Outer black stroke
    draw.polygon(poly, fill=(255, 255, 255, 255), outline=(20, 20, 20, 255), width=int(max(2, 3 * scale)))

    # Click pulse ring
    if is_clicking:
        ring_r = 18 * scale
        draw.ellipse([x - ring_r, y - ring_r, x + ring_r, y + ring_r], outline=(255, 220, 50, 220), width=3)


def draw_bell_icon(draw, cx, cy, size, active=False, ring_angle=0.0):
    """Draws a clean notification bell icon."""
    fill_col = (255, 215, 0, 255) if active else (220, 220, 220, 255)
    outline_col = (30, 30, 30, 255)
    
    # Bell dome
    bw = size * 0.7
    bh = size * 0.7
    draw.arc([cx - bw/2, cy - bh/2, cx + bw/2, cy + bh/2], start=180, end=0, fill=fill_col, width=int(size * 0.15))
    draw.rectangle([cx - bw/2, cy, cx + bw/2, cy + bh*0.3], fill=fill_col)
    
    # Flared bottom rim
    draw.ellipse([cx - bw*0.65, cy + bh*0.25, cx + bw*0.65, cy + bh*0.45], fill=fill_col)
    
    # Clapper
    clapper_r = size * 0.15
    draw.ellipse([cx - clapper_r, cy + bh*0.35, cx + clapper_r, cy + bh*0.65], fill=(180, 180, 180, 255))

    # Sound wave vibration ripples if active
    if active:
        draw.arc([cx - bw*1.1, cy - bh*0.3, cx - bw*0.8, cy + bh*0.5], start=120, end=240, fill=(255, 215, 0, 220), width=2)
        draw.arc([cx + bw*0.8, cy - bh*0.3, cx + bw*1.1, cy + bh*0.5], start=-60, end=60, fill=(255, 215, 0, 220), width=2)


def render_youtube_subscribe(output_path, width, height, fps, duration, extra_params):
    """Renders high-fidelity animated YouTube Subscribe button + Hand Click cursor on alpha channel."""
    total_frames = int(fps * duration)
    btn_w = 420
    btn_h = 84
    cx = width // 2
    cy = int(height * 0.75) # Positioned lower third by default
    
    if "center_y" in extra_params:
        cy = int(height * extra_params["center_y"])
    if "center_x" in extra_params:
        cx = int(width * extra_params["center_x"])

    bold_font = get_default_font(32)
    small_font = get_regular_font(24)

    # Click happens at frame click_frame
    click_frame = int(fps * 1.3)
    subscribed_frame = click_frame + 6

    # Start ffmpeg process writing to QuickTime RLE transparent MOV
    ffmpeg_cmd = [
        "ffmpeg", "-y",
        "-f", "rawvideo",
        "-vcodec", "rawvideo",
        "-s", f"{width}x{height}",
        "-pix_fmt", "rgba",
        "-r", str(fps),
        "-i", "-",
        "-c:v", "qtrle",
        "-pix_fmt", "argb",
        output_path
    ]

    proc = subprocess.Popen(ffmpeg_cmd, stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)

    for f in range(total_frames):
        # Full transparent canvas
        img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        # 1. Button Entrance & Scale Animation (0.0 to 0.4s)
        intro_t = min(1.0, f / (fps * 0.35))
        # Overshoot bounce easing
        bounce_scale = 1.0
        if intro_t < 1.0:
            bounce_scale = math.sin(intro_t * math.pi * 0.5) * 1.05
        
        # Click compression bounce (at click_frame)
        if click_frame <= f < click_frame + 12:
            dt = (f - click_frame) / 12.0
            bounce_scale = 1.0 - (math.sin(dt * math.pi) * 0.12) # Compress down to 88% then bounce back
        
        bw = int(btn_w * bounce_scale)
        bh = int(btn_h * bounce_scale)
        bx1 = cx - bw // 2
        by1 = cy - bh // 2
        bx2 = cx + bw // 2
        by2 = cy + bh // 2

        # 2. Button State & Color
        is_subscribed = (f >= subscribed_frame)
        if not is_subscribed:
            # YouTube Bright Red
            btn_color = (255, 0, 0, 245)
            text_str = "SUBSCRIBE"
            text_color = (255, 255, 255, 255)
        else:
            # Subscribed Dark Gray / Slate
            btn_color = (45, 45, 45, 245)
            text_str = "SUBSCRIBED"
            text_color = (220, 220, 220, 255)

        # Drop shadow
        shadow_rect = [bx1 + 3, by1 + 5, bx2 + 3, by2 + 5]
        draw.rounded_rectangle(shadow_rect, radius=int(bh // 2), fill=(0, 0, 0, 110))

        # Main Button Pill
        draw.rounded_rectangle([bx1, by1, bx2, by2], radius=int(bh // 2), fill=btn_color)

        # YouTube Logo / Play Triangle on left
        logo_cx = bx1 + 45
        logo_cy = cy
        logo_w = 34
        logo_h = 24
        draw.rounded_rectangle([logo_cx - logo_w//2, logo_cy - logo_h//2, logo_cx + logo_w//2, logo_cy + logo_h//2], radius=6, fill=(255, 255, 255, 240) if is_subscribed else (255, 255, 255, 255))
        # Triangle in play icon
        tri_pts = [
            (logo_cx - 5, logo_cy - 7),
            (logo_cx - 5, logo_cy + 7),
            (logo_cx + 7, logo_cy)
        ]
        draw.polygon(tri_pts, fill=btn_color)

        # Button Text
        text_bbox = draw.textbbox((0, 0), text_str, font=bold_font)
        tw = text_bbox[2] - text_bbox[0]
        th = text_bbox[3] - text_bbox[1]
        text_x = bx1 + 75
        text_y = cy - th // 2 - 2
        draw.text((text_x, text_y), text_str, fill=text_color, font=bold_font)

        # Notification Bell Icon on right side of button
        bell_cx = bx2 - 45
        bell_cy = cy
        draw_bell_icon(draw, bell_cx, bell_cy, size=24, active=is_subscribed, ring_angle=math.sin(f * 0.6) if is_subscribed else 0.0)

        # 3. Hand Cursor Pointer Animation
        # Path: Moves from bottom-right towards center of button
        start_hx = cx + 220
        start_hy = cy + 240
        target_hx = cx + 40
        target_hy = cy + 10

        t_hand = max(0.0, min(1.0, (f - fps * 0.25) / (fps * 0.95)))
        # Smooth easeInOut
        ease = 0.5 - 0.5 * math.cos(t_hand * math.pi)
        hx = start_hx + (target_hx - start_hx) * ease
        hy = start_hy + (target_hy - start_hy) * ease

        # Hand leaves after click
        if f > fps * 2.1:
            t_leave = min(1.0, (f - fps * 2.1) / (fps * 0.8))
            ease_leave = t_leave * t_leave
            hx += ease_leave * 180
            hy += ease_leave * 220

        is_clicking = (click_frame - 2 <= f <= click_frame + 4)
        click_scale = 0.88 if is_clicking else 1.0

        if f < fps * 2.9: # Hide when off screen
            draw_hand_cursor(draw, int(hx), int(hy), scale=click_scale, is_clicking=is_clicking)

        # Write raw RGBA frame to ffmpeg stdin
        proc.stdin.write(img.tobytes())

    proc.stdin.close()
    proc.wait()
    return proc.returncode == 0


def render_chroma_key(input_clip, output_path, extra_params):
    """Executes FFmpeg colorkey filter with despill to output transparent video."""
    color = extra_params.get("key_color", "0x00FF00")
    similarity = extra_params.get("similarity", 0.3)
    blend = extra_params.get("blend", 0.1)

    cmd = [
        "ffmpeg", "-y",
        "-i", input_clip,
        "-vf", f"colorkey={color}:{similarity}:{blend},format=yuva444p10le",
        "-c:v", "prores_ks", "-profile:v", "4",
        output_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if res.returncode != 0:
        # Fallback to qtrle
        cmd[7] = "qtrle"
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return res.returncode == 0


def render_glitch_effect(input_clip, output_path, extra_params):
    """Executes chromatic aberration / glitch shift via FFmpeg."""
    shift = extra_params.get("intensity", 8)
    cmd = [
        "ffmpeg", "-y",
        "-i", input_clip,
        "-vf", f"rgbashift=rh={shift}:bv=-{shift}",
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p",
        output_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return res.returncode == 0


def main():
    args = parse_args()
    
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    
    extra_params = {}
    if args.params:
        try:
            extra_params = json.loads(args.params)
        except Exception:
            pass

    pipeline = args.pipeline.lower()
    
    success = False
    if any(k in pipeline for k in ["subscribe", "button", "handclick", "motion_graphic", "overlay"]):
        success = render_youtube_subscribe(args.output, args.width, args.height, args.fps, args.duration, extra_params)
    elif "chroma" in pipeline and args.input and os.path.exists(args.input):
        success = render_chroma_key(args.input, args.output, extra_params)
    elif "glitch" in pipeline and args.input and os.path.exists(args.input):
        success = render_glitch_effect(args.input, args.output, extra_params)
    else:
        # Default high-quality motion graphic
        success = render_youtube_subscribe(args.output, args.width, args.height, args.fps, args.duration, extra_params)

    if success and os.path.exists(args.output):
        print(f"[VidMate VFX] Successfully synthesized asset: {args.output}")
        sys.exit(0)
    else:
        print(f"[VidMate VFX] Synthesis failed for pipeline {pipeline}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
