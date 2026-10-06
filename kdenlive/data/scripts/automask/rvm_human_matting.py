#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#
# One-Click Human Video & Image Background Removal using RobustVideoMatting (RVM)
# Model source: RVM-Inference (https://github.com/xlite-dev/RVM-Inference)

import os
import sys
import argparse
import glob
import numpy as np
from PIL import Image

try:
    import onnxruntime as ort
    HAVE_ORT = True
except ImportError:
    HAVE_ORT = False

try:
    import torch
    HAVE_TORCH = True
except ImportError:
    HAVE_TORCH = False


def find_default_rvm_model():
    possible_paths = [
        os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx")),
        os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx")),
        "/home/lincoln/vidmate.ai-mate/RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx",
        "/home/lincoln/vidmate.ai-mate/RVM-Inference/examples/hub/onnx/rvm_mobilenetv3_fp32.onnx",
    ]
    for p in possible_paths:
        if os.path.exists(p):
            return p
    return None


class RVMOnnxRunner:
    def __init__(self, model_path, device="cpu", threads=4):
        self.model_path = model_path
        sess_options = ort.SessionOptions()
        sess_options.intra_op_num_threads = threads
        sess_options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        sess_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL

        providers = ['CPUExecutionProvider']
        if device == "cuda" and 'CUDAExecutionProvider' in ort.get_available_providers():
            providers = ['CUDAExecutionProvider', 'CPUExecutionProvider']

        self.session = ort.InferenceSession(model_path, sess_options, providers=providers)
        self.r1i = np.zeros([1, 1, 1, 1], dtype=np.float32)
        self.r2i = np.zeros([1, 1, 1, 1], dtype=np.float32)
        self.r3i = np.zeros([1, 1, 1, 1], dtype=np.float32)
        self.r4i = np.zeros([1, 1, 1, 1], dtype=np.float32)

    def reset(self):
        self.r1i = np.zeros([1, 1, 1, 1], dtype=np.float32)
        self.r2i = np.zeros([1, 1, 1, 1], dtype=np.float32)
        self.r3i = np.zeros([1, 1, 1, 1], dtype=np.float32)
        self.r4i = np.zeros([1, 1, 1, 1], dtype=np.float32)

    def process_frame(self, img_pil, downsample_ratio=None, warmup=False):
        # Image to tensor (1, 3, H, W) normalized [0, 1]
        src_np = np.array(img_pil.convert('RGB'), dtype=np.float32) / 255.0
        h, w = src_np.shape[0], src_np.shape[1]
        src_tensor = np.transpose(src_np, (2, 0, 1))[np.newaxis, ...]

        if downsample_ratio is None or downsample_ratio <= 0:
            # Official RVM recommendation: downsample_ratio = min(512 / max(H, W), 1.0)
            dsr = min(1.0, 512.0 / max(h, w))
        else:
            dsr = float(downsample_ratio)

        # If recurrent states are uninitialized or warmup requested, do an initial pass
        if warmup or self.r1i.shape == (1, 1, 1, 1):
            inputs_warmup = {
                'src': src_tensor,
                'r1i': self.r1i,
                'r2i': self.r2i,
                'r3i': self.r3i,
                'r4i': self.r4i,
                'downsample_ratio': np.array([dsr], dtype=np.float32)
            }
            _, _, self.r1i, self.r2i, self.r3i, self.r4i = self.session.run(None, inputs_warmup)

        inputs = {
            'src': src_tensor,
            'r1i': self.r1i,
            'r2i': self.r2i,
            'r3i': self.r3i,
            'r4i': self.r4i,
            'downsample_ratio': np.array([dsr], dtype=np.float32)
        }

        fgr, pha, self.r1i, self.r2i, self.r3i, self.r4i = self.session.run(None, inputs)

        # Alpha matte (H, W) in [0, 255]
        pha_img = (np.squeeze(pha) * 255.0).clip(0, 255).astype(np.uint8)
        pha_pil = Image.fromarray(pha_img, mode='L')

        # Foreground cutout (RGBA)
        fgr_img = (np.squeeze(fgr).transpose((1, 2, 0)) * 255.0).clip(0, 255).astype(np.uint8)
        rgba_np = np.dstack((fgr_img, pha_img))
        rgba_pil = Image.fromarray(rgba_np, mode='RGBA')

        return pha_pil, rgba_pil


def main():
    parser = argparse.ArgumentParser("RobustVideoMatting Human Background Removal")
    parser.add_argument("-I", "--inputFolder", help="Folder containing source video frame images", required=True)
    parser.add_argument("-O", "--output", help="Output directory for generated alpha masks / cutouts", required=True)
    parser.add_argument("-F", "--preview_frame", help="Single frame number for instant preview (-1 for full sequence)", type=int, default=-1)
    parser.add_argument("-M", "--model", help="Path to RVM ONNX model file", default=None)
    parser.add_argument("-D", "--device", help="Compute device: cpu, cuda", default="cpu")
    parser.add_argument("--downsample_ratio", help="Downsample ratio for RVM (auto if omitted)", type=float, default=None)
    parser.add_argument("--cutout", help="Save full RGBA transparent cutout image along with alpha mask", action="store_true")
    args = parser.parse_args()

    model_path = args.model
    if not model_path or not os.path.exists(model_path):
        model_path = find_default_rvm_model()

    if not model_path or not os.path.exists(model_path):
        print(f"Error: RVM model file not found at {model_path}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)

    if not HAVE_ORT:
        print("Error: onnxruntime not installed in Python environment.", file=sys.stderr)
        sys.exit(1)

    runner = RVMOnnxRunner(model_path, device=args.device)

    # Collect source frames
    extensions = ('*.jpg', '*.jpeg', '*.png', '*.bmp')
    frame_files = []
    for ext in extensions:
        frame_files.extend(glob.glob(os.path.join(args.inputFolder, ext)))
    frame_files.sort()

    if not frame_files:
        print(f"Error: No image frames found in input folder {args.inputFolder}", file=sys.stderr)
        sys.exit(1)

    # Preview mode
    if args.preview_frame >= 0:
        target_file = None
        # Try matching exact frame number index or filename
        for f in frame_files:
            base = os.path.basename(f)
            num_str = ''.join(filter(str.isdigit, base))
            if num_str and int(num_str) == args.preview_frame:
                target_file = f
                break
        if not target_file and args.preview_frame < len(frame_files):
            target_file = frame_files[args.preview_frame]
        elif not target_file:
            target_file = frame_files[0]

        img = Image.open(target_file)
        pha_pil, rgba_pil = runner.process_frame(img, downsample_ratio=args.downsample_ratio)

        out_mask_path = os.path.join(args.output, f"mask_{args.preview_frame:06d}.png")
        out_preview_path = os.path.join(args.output, "preview.png")
        pha_pil.save(out_mask_path)
        pha_pil.save(out_preview_path)

        if args.cutout:
            out_cutout_path = os.path.join(args.output, f"cutout_{args.preview_frame:06d}.png")
            rgba_pil.save(out_cutout_path)

        print(f"RVM Human Matting: Preview frame {args.preview_frame} saved to {out_mask_path}")
        return

    # Full sequence batch processing
    total_frames = len(frame_files)
    print(f"RVM Human Matting: Processing {total_frames} frames...")

    for idx, f in enumerate(frame_files):
        img = Image.open(f)
        pha_pil, rgba_pil = runner.process_frame(img, downsample_ratio=args.downsample_ratio)

        base_name = os.path.splitext(os.path.basename(f))[0]
        out_path = os.path.join(args.output, f"{base_name}.png")
        pha_pil.save(out_path)

        if args.cutout:
            out_cutout_path = os.path.join(args.output, f"{base_name}_cutout.png")
            rgba_pil.save(out_cutout_path)

        if idx % 20 == 0 or idx == total_frames - 1:
            progress = int(((idx + 1) / total_frames) * 100)
            print(f"PROGRESS: {progress}% ({idx + 1}/{total_frames})")
            sys.stdout.flush()

    print("RVM Human Matting complete.")


if __name__ == "__main__":
    main()
