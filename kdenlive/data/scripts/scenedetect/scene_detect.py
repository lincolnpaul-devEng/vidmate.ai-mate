#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
"""
PySceneDetect Scene Analysis & Shot Boundary Detection Script
Outputs scene cut boundaries in JSON format for the VidMate AI orchestration layer.
"""

import sys
import os
import json
import argparse
import subprocess
import re

def detect_with_scenedetect(video_path, threshold=27.0, detector_type="content", min_scene_len=15, start_frame=0, end_frame=None):
    try:
        from scenedetect import open_video, SceneManager
        from scenedetect.detectors import ContentDetector, AdaptiveDetector, ThresholdDetector
        from scenedetect.frame_timecode import FrameTimecode
    except ImportError:
        return None

    video = open_video(video_path)
    scene_manager = SceneManager()

    if detector_type.lower() == "adaptive":
        scene_manager.add_detector(AdaptiveDetector(adaptive_threshold=threshold, min_scene_len=min_scene_len))
    elif detector_type.lower() == "threshold":
        scene_manager.add_detector(ThresholdDetector(threshold=threshold, min_scene_len=min_scene_len))
    else:
        scene_manager.add_detector(ContentDetector(threshold=threshold, min_scene_len=min_scene_len))

    # Detect scenes
    scene_manager.detect_scenes(video=video)
    scene_list = scene_manager.get_scene_list()

    scenes = []
    for i, (start_time, end_time) in enumerate(scene_list):
        s_frame = start_time.get_frames()
        e_frame = end_time.get_frames()
        
        if end_frame is not None and s_frame > end_frame:
            break
        if e_frame < start_frame:
            continue

        scenes.append({
            "scene_num": i + 1,
            "start_frame": s_frame,
            "end_frame": e_frame,
            "duration_frames": e_frame - s_frame,
            "start_time": start_time.get_timecode(),
            "end_time": end_time.get_timecode(),
            "start_seconds": start_time.get_seconds(),
            "end_seconds": end_time.get_seconds(),
            "duration_seconds": end_time.get_seconds() - start_time.get_seconds()
        })

    return {
        "engine": "scenedetect",
        "video_path": video_path,
        "detector": detector_type,
        "threshold": threshold,
        "scene_count": len(scenes),
        "scenes": scenes
    }

def detect_with_ffmpeg_fallback(video_path, threshold=0.3, fps=30.0):
    cmd = [
        "ffmpeg", "-hide_banner", "-i", video_path,
        "-filter:v", f"select='gt(scene,{threshold})',showinfo",
        "-f", "null", "-"
    ]
    try:
        proc = subprocess.run(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, text=True, check=True)
        output = proc.stderr
    except Exception as e:
        return {
            "engine": "ffmpeg_fallback_error",
            "error": str(e),
            "scene_count": 0,
            "scenes": []
        }

    # Parse pts_time from showinfo
    pattern = re.compile(r"pts_time:([0-9.]+)")
    times = [0.0]
    for line in output.splitlines():
        if "showinfo" in line:
            m = pattern.search(line)
            if m:
                t = float(m.group(1))
                if t not in times:
                    times.append(t)

    times.sort()
    scenes = []
    for i in range(len(times)):
        s_sec = times[i]
        e_sec = times[i+1] if i + 1 < len(times) else s_sec + 5.0
        s_frame = int(round(s_sec * fps))
        e_frame = int(round(e_sec * fps))

        scenes.append({
            "scene_num": i + 1,
            "start_frame": s_frame,
            "end_frame": e_frame,
            "duration_frames": max(1, e_frame - s_frame),
            "start_time": f"{int(s_sec//3600):02d}:{int((s_sec%3600)//60):02d}:{s_sec%60:06.3f}",
            "end_time": f"{int(e_sec//3600):02d}:{int((e_sec%3600)//60):02d}:{e_sec%60:06.3f}",
            "start_seconds": s_sec,
            "end_seconds": e_sec,
            "duration_seconds": e_sec - s_sec
        })

    return {
        "engine": "ffmpeg_fallback",
        "video_path": video_path,
        "detector": "content_ffmpeg",
        "threshold": threshold,
        "scene_count": len(scenes),
        "scenes": scenes
    }

def main():
    parser = argparse.ArgumentParser(description="PySceneDetect Video Analysis")
    parser.add_argument("-i", "--input", required=True, help="Input video file path")
    parser.add_argument("-t", "--threshold", type=float, default=27.0, help="Detection threshold")
    parser.add_argument("-d", "--detector", default="content", choices=["content", "adaptive", "threshold"], help="Detector type")
    parser.add_argument("-m", "--min-scene-len", type=int, default=15, help="Min scene length in frames")
    parser.add_argument("-s", "--start-frame", type=int, default=0, help="Start frame")
    parser.add_argument("-e", "--end-frame", type=int, default=None, help="End frame")
    parser.add_argument("-o", "--output", help="Output JSON path (optional)")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        result = {"error": f"Input file not found: {args.input}", "scenes": []}
    else:
        # Try PySceneDetect first
        result = detect_with_scenedetect(
            args.input,
            threshold=args.threshold,
            detector_type=args.detector,
            min_scene_len=args.min_scene_len,
            start_frame=args.start_frame,
            end_frame=args.end_frame
        )
        if result is None:
            # Fallback to ffmpeg
            ffmpeg_thresh = min(0.9, max(0.1, args.threshold / 100.0 if args.threshold > 1.0 else args.threshold))
            result = detect_with_ffmpeg_fallback(args.input, threshold=ffmpeg_thresh)

    json_str = json.dumps(result, indent=2)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(json_str)
    else:
        print(json_str)

if __name__ == "__main__":
    main()
