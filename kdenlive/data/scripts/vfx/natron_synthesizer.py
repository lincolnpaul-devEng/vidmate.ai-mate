#!/usr/bin/env python3
"""
VidMate Natron VFX & Motion Graphics Synthesizer Engine
======================================================
Procedural motion graphics generator and in-process VFX compositing engine.
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
    parser.add_argument("--pipeline", default="kinetic_motion_graphic", help="VFX pipeline or motion graphic type")
    parser.add_argument("--input", "-i", default="", help="Input clip path (for effects/compositing)")
    parser.add_argument("--output", "-o", required=True, help="Output media file path (.mov)")
    parser.add_argument("--width", type=int, default=1920, help="Output width")
    parser.add_argument("--height", type=int, default=1080, help="Output height")
    parser.add_argument("--fps", type=int, default=30, help="Framerate")
    parser.add_argument("--duration", type=float, default=3.5, help="Duration in seconds")
    parser.add_argument("--params", default="{}", help="JSON dictionary of parameters")
    return parser.parse_args()


def get_bold_font(size):
    font_paths = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    ]
    for p in font_paths:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()


def get_regular_font(size):
    font_paths = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    ]
    for p in font_paths:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()


def parse_color(c, default=(255, 255, 255, 255)):
    if not c:
        return default
    if isinstance(c, (list, tuple)):
        if len(c) == 3:
            return (int(c[0]), int(c[1]), int(c[2]), 255)
        elif len(c) >= 4:
            return (int(c[0]), int(c[1]), int(c[2]), int(c[3]))
    if isinstance(c, str):
        c = c.strip().lstrip("#")
        if len(c) == 6:
            r = int(c[0:2], 16)
            g = int(c[2:4], 16)
            b = int(c[4:6], 16)
            return (r, g, b, 255)
        elif len(c) == 8:
            r = int(c[0:2], 16)
            g = int(c[2:4], 16)
            b = int(c[4:6], 16)
            a = int(c[6:8], 16)
            return (r, g, b, a)
    return default


def render_radial_kinetic_motion_graphic(output_path, width, height, fps, duration, extra_params):
    """
    Renders high-energy After Effects style radial kinetic motion graphic with:
    - Bold punchy centered typography (e.g. MOTION GRAPHICS)
    - Dynamic spinning concentric dashed arcs and clock ticks
    - Radiating burst velocity lines
    - Swirling speed streak arcs
    - Floating particle rings and dots
    - Transparent alpha channel for instant timeline overlay
    """
    total_frames = max(1, int(fps * duration))
    cx = width // 2
    cy = height // 2

    # Allow custom center offset
    if "center_x" in extra_params:
        cx = int(width * float(extra_params["center_x"]))
    if "center_y" in extra_params:
        cy = int(height * float(extra_params["center_y"]))

    # Extract text parameters
    title_1 = str(extra_params.get("title_1", extra_params.get("text_1", extra_params.get("title", "MOTION")))).upper()
    title_2 = str(extra_params.get("title_2", extra_params.get("text_2", extra_params.get("subtitle", "GRAPHICS")))).upper()

    col_1 = parse_color(extra_params.get("color_1", extra_params.get("accent_color", "#F5BA20")), (245, 186, 32, 255))
    col_2 = parse_color(extra_params.get("color_2", "#FFFFFF"), (255, 255, 255, 255))

    has_bg = extra_params.get("has_background", False) or extra_params.get("bg", "") in ["dark", "solid"]
    bg_color = (14, 34, 40, 255) if has_bg else (0, 0, 0, 0)

    # Base font sizing
    base_font_size = int(height * 0.098)  # ~105px for 1080p
    bold_font = get_bold_font(base_font_size)

    # Start ffmpeg process writing QuickTime RLE transparent MOV
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
        t_sec = f / float(fps)
        progress = f / float(total_frames)

        img = Image.new("RGBA", (width, height), bg_color)
        draw = ImageDraw.Draw(img)

        # Entrance scale animation (0.0 to 0.45s) with overshoot bounce
        t_intro = min(1.0, f / (fps * 0.45))
        if t_intro < 1.0:
            scale = math.sin(t_intro * math.pi * 0.5) * (1.0 + 0.15 * math.cos(t_intro * math.pi * 1.5))
        else:
            scale = 1.0 + 0.02 * math.sin(t_sec * 1.8)  # Subtle dynamic breathing drift

        # 1. Centered Kinetic Typography
        cur_font_size = max(10, int(base_font_size * scale))
        cur_font = get_bold_font(cur_font_size)

        bbox1 = draw.textbbox((0, 0), title_1, font=cur_font)
        w1 = bbox1[2] - bbox1[0]
        h1 = bbox1[3] - bbox1[1]

        bbox2 = draw.textbbox((0, 0), title_2, font=cur_font)
        w2 = bbox2[2] - bbox2[0]
        h2 = bbox2[3] - bbox2[1]

        spacing = int(14 * scale)
        total_text_h = h1 + h2 + spacing
        text_y_start = cy - total_text_h // 2

        # Draw Title 1 (e.g. MOTION in gold/yellow)
        draw.text((cx - w1 // 2, text_y_start), title_1, fill=col_1, font=cur_font)
        # Draw Title 2 (e.g. GRAPHICS in crisp white)
        draw.text((cx - w2 // 2, text_y_start + h1 + spacing), title_2, fill=col_2, font=cur_font)

        # 2. Main Outer Thick Circular Arc (Rotating clockwise)
        r_main = int(280 * scale)
        main_angle_start = (progress * 360.0 * 1.2) - 40.0
        main_angle_end = main_angle_start + 240.0
        draw.arc(
            [cx - r_main, cy - r_main, cx + r_main, cy + r_main],
            start=int(main_angle_start),
            end=int(main_angle_end),
            fill=(255, 255, 255, 255),
            width=int(max(3, 8 * scale))
        )

        # 3. Outer Thin Concentric Arc (Wrapping top & left)
        r_outer_thin = int(330 * scale)
        outer_thin_start = (progress * 360.0 * 0.8) + 120.0
        outer_thin_end = outer_thin_start + 190.0
        draw.arc(
            [cx - r_outer_thin, cy - r_outer_thin, cx + r_outer_thin, cy + r_outer_thin],
            start=int(outer_thin_start),
            end=int(outer_thin_end),
            fill=(255, 255, 255, 220),
            width=int(max(1, 3 * scale))
        )

        # 4. Inner Accent Arc (Rotating counter-clockwise)
        r_inner = int(240 * scale)
        inner_start = (-progress * 360.0 * 1.5) + 210.0
        inner_end = inner_start + 110.0
        draw.arc(
            [cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner],
            start=int(inner_start),
            end=int(inner_end),
            fill=(255, 255, 255, 240),
            width=int(max(2, 5 * scale))
        )

        # 5. Radial Clock Dial Ticks
        r_tick_in = int(212 * scale)
        r_tick_out = int(234 * scale)
        num_ticks = 24
        tick_rot = (progress * 360.0 * 0.5) * (math.pi / 180.0)
        for i in range(num_ticks):
            # Dynamic gaps around circumference
            if i % 3 != 1:
                ang = i * (2.0 * math.pi / num_ticks) + tick_rot
                x1 = cx + r_tick_in * math.cos(ang)
                y1 = cy + r_tick_in * math.sin(ang)
                x2 = cx + r_tick_out * math.cos(ang)
                y2 = cy + r_tick_out * math.sin(ang)
                draw.line([(x1, y1), (x2, y2)], fill=(200, 225, 235, 210), width=int(max(1, 3 * scale)))

        # 6. Radiating Velocity Burst Lines (Shooting outwards)
        burst_base_angles = [0.2, 0.9, 1.8, 2.5, 3.4, 4.2, 5.1, 5.8]
        burst_pulse = (f % int(fps * 0.8)) / float(fps * 0.8)
        for idx, base_ang in enumerate(burst_base_angles):
            ang = base_ang + (progress * 0.4)
            r_burst_1 = int((350 + (burst_pulse * 40) + (15 * math.sin(idx + t_sec * 3))) * scale)
            r_burst_2 = r_burst_1 + int((50 + (30 * math.cos(idx * 2))) * scale)
            bx1 = cx + r_burst_1 * math.cos(ang)
            by1 = cy + r_burst_1 * math.sin(ang)
            bx2 = cx + r_burst_2 * math.cos(ang)
            by2 = cy + r_burst_2 * math.sin(ang)
            alpha_burst = int(max(60, min(240, 240 * (1.0 - burst_pulse * 0.6))))
            draw.line([(bx1, by1), (bx2, by2)], fill=(255, 255, 255, alpha_burst), width=int(max(1, 3 * scale)))

        # 7. Swirling Velocity Streak Arcs
        draw.arc(
            [cx - int(390 * scale), cy - int(360 * scale), cx + int(350 * scale), cy + int(380 * scale)],
            start=int(140 + progress * 80),
            end=int(190 + progress * 80),
            fill=(255, 255, 255, 200),
            width=int(max(1, 3 * scale))
        )
        draw.arc(
            [cx - int(320 * scale), cy - int(410 * scale), cx + int(420 * scale), cy + int(330 * scale)],
            start=int(20 - progress * 90),
            end=int(70 - progress * 90),
            fill=(255, 255, 255, 190),
            width=int(max(1, 3 * scale))
        )
        draw.arc(
            [cx - int(260 * scale), cy - int(290 * scale), cx + int(290 * scale), cy + int(260 * scale)],
            start=int(60 + progress * 110),
            end=int(110 + progress * 110),
            fill=(255, 255, 255, 170),
            width=int(max(1, 2 * scale))
        )

        # 8. Floating Particle Rings (Hollow circles drifting & oscillating)
        particle_configs = [
            (-380, -180, 14, 0.8),
            (-310, 220, 10, -0.6),
            (360, -240, 18, 1.1),
            (420, -120, 8, -0.9),
            (460, 180, 12, 0.7),
            (-440, -40, 7, -1.2),
            (280, 340, 11, 0.5),
            (-180, 320, 9, -0.7)
        ]
        for ox, oy, pr, speed in particle_configs:
            drift_x = math.cos(t_sec * speed) * 12
            drift_y = math.sin(t_sec * speed) * 12
            px = cx + int((ox + drift_x) * scale)
            py = cy + int((oy + drift_y) * scale)
            cur_r = int(pr * scale)
            draw.ellipse(
                [px - cur_r, py - cur_r, px + cur_r, py + cur_r],
                outline=(255, 255, 255, 220),
                width=int(max(1, 2 * scale))
            )

        # 9. Small Solid Micro Dots
        dot_offsets = [
            (-360, 80), (390, 60), (220, -340), (-260, -310), (320, 280), (140, 360), (-120, -360)
        ]
        for dx, dy in dot_offsets:
            dot_x = cx + int((dx + math.sin(t_sec + dx) * 8) * scale)
            dot_y = cy + int((dy + math.cos(t_sec + dy) * 8) * scale)
            draw.ellipse([dot_x - 3, dot_y - 3, dot_x + 3, dot_y + 3], fill=(255, 255, 255, 240))

        # Write frame to ffmpeg
        proc.stdin.write(img.tobytes())

    proc.stdin.close()
    proc.wait()
    return proc.returncode == 0


def render_lower_third(output_path, width, height, fps, duration, extra_params):
    """Renders sleek broadcast animated Lower Third on alpha channel."""
    total_frames = max(1, int(fps * duration))
    title = str(extra_params.get("title", extra_params.get("title_1", "FEATURE REVEAL")))
    subtitle = str(extra_params.get("subtitle", extra_params.get("title_2", "VidMate Compositor & VFX Engine")))
    accent_col = parse_color(extra_params.get("accent_color", "#3DAEE9"), (61, 174, 233, 255))

    bold_font = get_bold_font(int(height * 0.038))
    reg_font = get_regular_font(int(height * 0.024))

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

    y_pos = int(height * 0.80)
    for f in range(total_frames):
        img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        # Entrance wipe from left (0 to 0.4s)
        wipe_t = min(1.0, f / (fps * 0.4))
        slide_x = int(math.sin(wipe_t * math.pi * 0.5) * 580)

        # Accent bar
        draw.rectangle([100, y_pos - 8, 100 + slide_x, y_pos + 68], fill=(22, 24, 28, 230))
        draw.rectangle([100, y_pos - 8, 108, y_pos + 68], fill=accent_col)

        if wipe_t >= 0.7:
            fade_text = int(min(255, (wipe_t - 0.7) / 0.3 * 255))
            draw.text((124, y_pos - 2), title, fill=(255, 255, 255, fade_text), font=bold_font)
            draw.text((124, y_pos + 36), subtitle, fill=(190, 205, 220, fade_text), font=reg_font)

        proc.stdin.write(img.tobytes())

    proc.stdin.close()
    proc.wait()
    return proc.returncode == 0


def render_hud_circle(output_path, width, height, fps, duration, extra_params):
    """Renders high-tech tactical circular HUD element on alpha channel."""
    total_frames = max(1, int(fps * duration))
    cx, cy = width // 2, height // 2
    r = int(height * 0.28)

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
        img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        rot = (f / float(total_frames)) * 360.0

        # Concentric HUD circles
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(61, 174, 233, 180), width=2)
        draw.ellipse([cx - r + 24, cy - r + 24, cx + r - 24, cy + r - 24], outline=(61, 174, 233, 100), width=1)

        # Crosshairs
        draw.line([(cx - r - 20, cy), (cx - r + 10, cy)], fill=(61, 174, 233, 220), width=2)
        draw.line([(cx + r - 10, cy), (cx + r + 20, cy)], fill=(61, 174, 233, 220), width=2)
        draw.line([(cx, cy - r - 20), (cx, cy - r + 10)], fill=(61, 174, 233, 220), width=2)
        draw.line([(cx, cy + r - 10), (cx, cy + r + 20)], fill=(61, 174, 233, 220), width=2)

        # Rotating dashed telemetry segment
        draw.arc([cx - r - 12, cy - r - 12, cx + r + 12, cy + r + 12], start=int(rot), end=int(rot + 70), fill=(245, 186, 32, 230), width=4)
        draw.arc([cx - r - 12, cy - r - 12, cx + r + 12, cy + r + 12], start=int(rot + 180), end=int(rot + 250), fill=(245, 186, 32, 230), width=4)

        proc.stdin.write(img.tobytes())

    proc.stdin.close()
    proc.wait()
    return proc.returncode == 0


def render_youtube_subscribe(output_path, width, height, fps, duration, extra_params):
    """Renders animated YouTube Subscribe button + Hand Click cursor on alpha channel."""
    total_frames = int(fps * duration)
    btn_w = 420
    btn_h = 84
    cx = width // 2
    cy = int(height * 0.75)

    if "center_y" in extra_params:
        cy = int(height * float(extra_params["center_y"]))
    if "center_x" in extra_params:
        cx = int(width * float(extra_params["center_x"]))

    bold_font = get_bold_font(32)
    click_frame = int(fps * 1.3)
    subscribed_frame = click_frame + 6

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
        img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        intro_t = min(1.0, f / (fps * 0.35))
        bounce_scale = 1.0
        if intro_t < 1.0:
            bounce_scale = math.sin(intro_t * math.pi * 0.5) * 1.05

        if click_frame <= f < click_frame + 12:
            dt = (f - click_frame) / 12.0
            bounce_scale = 1.0 - (math.sin(dt * math.pi) * 0.12)

        bw = int(btn_w * bounce_scale)
        bh = int(btn_h * bounce_scale)
        bx1 = cx - bw // 2
        by1 = cy - bh // 2
        bx2 = cx + bw // 2
        by2 = cy + bh // 2

        is_subscribed = (f >= subscribed_frame)
        btn_color = (45, 45, 45, 245) if is_subscribed else (255, 0, 0, 245)
        text_str = "SUBSCRIBED" if is_subscribed else "SUBSCRIBE"
        text_color = (220, 220, 220, 255) if is_subscribed else (255, 255, 255, 255)

        # Drop shadow & pill
        draw.rounded_rectangle([bx1 + 3, by1 + 5, bx2 + 3, by2 + 5], radius=int(bh // 2), fill=(0, 0, 0, 110))
        draw.rounded_rectangle([bx1, by1, bx2, by2], radius=int(bh // 2), fill=btn_color)

        # Text
        text_bbox = draw.textbbox((0, 0), text_str, font=bold_font)
        tw = text_bbox[2] - text_bbox[0]
        th = text_bbox[3] - text_bbox[1]
        draw.text((cx - tw // 2, cy - th // 2 - 2), text_str, fill=text_color, font=bold_font)

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


def render_color_grade(input_clip, output_path, extra_params):
    """Executes primary color grade adjustments (gamma, contrast, saturation)."""
    gamma = extra_params.get("gamma", 1.0)
    contrast = extra_params.get("contrast", 1.0)
    brightness = extra_params.get("brightness", 0.0)
    saturation = extra_params.get("saturation", 1.0)

    cmd = [
        "ffmpeg", "-y",
        "-i", input_clip,
        "-vf", f"eq=gamma={gamma}:contrast={contrast}:brightness={brightness}:saturation={saturation}",
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

    # 1. YouTube Subscribe Button (only when specifically requested)
    if ("subscribe" in pipeline or "youtube" in pipeline) and "motion_graphic" not in pipeline:
        success = render_youtube_subscribe(args.output, args.width, args.height, args.fps, args.duration, extra_params)

    # 2. Lower Third
    elif "lower_third" in pipeline or "lowerthird" in pipeline:
        success = render_lower_third(args.output, args.width, args.height, args.fps, args.duration, extra_params)

    # 3. HUD / Circular Telemetry
    elif "hud" in pipeline or "tech_ring" in pipeline or "telemetry" in pipeline:
        success = render_hud_circle(args.output, args.width, args.height, args.fps, args.duration, extra_params)

    # 4. Clip VFX: Chroma Key
    elif "chroma" in pipeline and args.input and os.path.exists(args.input):
        success = render_chroma_key(args.input, args.output, extra_params)

    # 5. Clip VFX: Glitch
    elif "glitch" in pipeline and args.input and os.path.exists(args.input):
        success = render_glitch_effect(args.input, args.output, extra_params)

    # 6. Clip VFX: Color Grade
    elif ("grade" in pipeline or "color" in pipeline) and args.input and os.path.exists(args.input):
        success = render_color_grade(args.input, args.output, extra_params)

    # 7. High-Energy Kinetic Radial Motion Graphic (Default for all motion graphics & titles!)
    else:
        success = render_radial_kinetic_motion_graphic(args.output, args.width, args.height, args.fps, args.duration, extra_params)

    if success and os.path.exists(args.output) and os.path.getsize(args.output) > 0:
        print(f"[VidMate Natron VFX] Successfully synthesized asset: {args.output}")
        sys.exit(0)
    else:
        print(f"[VidMate Natron VFX] Synthesis failed for pipeline {pipeline}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
