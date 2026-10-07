#!/usr/bin/env python3
"""
sfx_generator.py - High-Fidelity Local Cinematic SFX Synthesizer
Generates studio-grade cinematic sound effects (impacts, whooshes, risers, sub drops, glitches)
locally without third-party cloud dependencies, and outputs peak transient timing for timeline sync.
"""

import os
import sys
import json
import math
import argparse
import numpy as np
import soundfile as sf
from scipy import signal

def create_impact(duration=2.5, sample_rate=48000):
    """Deep sub-bass impact / cinematic boom with transient click and rumble."""
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
    
    # 1. Sub boom (pitch sweep from 95Hz down to 32Hz)
    f_start, f_end = 95.0, 32.0
    freq_curve = f_start * (f_end / f_start) ** (t / (duration * 0.75))
    phase = 2 * np.pi * np.cumsum(freq_curve) / sample_rate
    sub = np.sin(phase) * np.exp(-t / 0.55)
    
    # 2. Transient punch click (sharp high-to-mid transient within first 40ms)
    click_t = t[t < 0.06]
    click_freq = 450.0 * np.exp(-click_t / 0.012)
    click_phase = 2 * np.pi * np.cumsum(click_freq) / sample_rate
    click = np.zeros_like(t)
    click[:len(click_t)] = np.sin(click_phase) * np.exp(-click_t / 0.015) * 1.2
    
    # 3. Noise texture rumble (lowpass filtered white noise)
    noise = np.random.normal(0, 0.35, len(t))
    sos = signal.butter(4, 280, 'lp', fs=sample_rate, output='sos')
    rumble = signal.sosfilt(sos, noise) * np.exp(-t / 0.8)
    
    # Mix components
    mixed = sub * 0.7 + click * 0.5 + rumble * 0.35
    # Soft saturation curve: tanh
    audio = np.tanh(mixed * 2.2) * 0.95
    
    # Stereo widening
    left = audio
    # Slight delay & decorrelation on right channel for wide cinematic space
    delay_samples = int(0.008 * sample_rate)
    right = np.roll(audio, delay_samples)
    right[:delay_samples] = 0
    
    stereo = np.column_stack((left, right))
    peak_offset = float(click_t[np.argmax(np.abs(click[:len(click_t)]))]) if len(click_t) > 0 else 0.02
    return stereo, peak_offset

def create_whoosh(duration=1.8, sample_rate=48000):
    """Cinematic transition whoosh / swoosh with dynamic bandpass sweep and stereo pan."""
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
    peak_time = duration * 0.65  # peak hits at 65% of duration
    
    # Dynamic envelope: asymmetric gaussian rising to peak_time then decaying
    sigma_rise = peak_time / 2.5
    sigma_fall = (duration - peak_time) / 2.0
    env = np.where(t < peak_time,
                   np.exp(-0.5 * ((t - peak_time) / sigma_rise) ** 2),
                   np.exp(-0.5 * ((t - peak_time) / sigma_fall) ** 2))
    
    # Modulated noise body
    noise = np.random.normal(0, 0.5, len(t))
    
    # Cutoff frequency sweep from 180Hz -> 3200Hz -> 280Hz
    f_center = 280.0 + 2900.0 * env
    # Apply time-varying resonant filter via chunking
    chunk_size = 1024
    filtered = np.zeros_like(noise)
    for i in range(0, len(noise), chunk_size):
        chunk = noise[i:i+chunk_size]
        cf = np.clip(np.mean(f_center[i:i+chunk_size]), 120, sample_rate * 0.45)
        bw = cf * 0.6
        low = max(40, cf - bw / 2)
        high = min(sample_rate * 0.45, cf + bw / 2)
        sos = signal.butter(2, [low, high], 'bp', fs=sample_rate, output='sos')
        filtered[i:i+chunk_size] = signal.sosfilt(sos, chunk)
    
    # Sub air rumble backing
    sub_pitch = 80.0 + 70.0 * env
    sub_phase = 2 * np.pi * np.cumsum(sub_pitch) / sample_rate
    sub_body = np.sin(sub_phase) * (env ** 1.8) * 0.4
    
    raw = (filtered * 1.4 + sub_body) * env
    raw = np.tanh(raw * 1.8) * 0.95
    
    # Dynamic stereo panning from left (-0.85) to right (+0.85)
    pan = np.interp(t, [0, duration], [-0.85, 0.85])
    left_gain = np.cos((pan + 1) * np.pi / 4)
    right_gain = np.sin((pan + 1) * np.pi / 4)
    
    stereo = np.column_stack((raw * left_gain, raw * right_gain))
    return stereo, float(peak_time)

def create_riser(duration=3.5, sample_rate=48000):
    """Tension riser / pitch build-up crescendo leading into a cut."""
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
    progress = t / duration
    
    # Exponential pitch sweep: 55Hz (A1) rising to 784Hz (G5)
    f_start, f_end = 55.0, 784.0
    freq_curve = f_start * (f_end / f_start) ** (progress ** 1.3)
    phase = 2 * np.pi * np.cumsum(freq_curve) / sample_rate
    
    # Rich multi-oscillator saw + sine stack
    osc1 = signal.sawtooth(phase) * 0.5
    osc2 = np.sin(phase * 2.01) * 0.3
    osc3 = np.sin(phase * 0.5) * 0.4
    synth = osc1 + osc2 + osc3
    
    # Crescendo volume envelope
    vol_env = (progress ** 1.8)
    
    # Rhythmic pulse / stutter tremolo accelerating in frequency (from 4Hz to 24Hz)
    tremolo_freq = 4.0 + 20.0 * (progress ** 2)
    tremolo = 0.5 + 0.5 * np.cos(2 * np.pi * np.cumsum(tremolo_freq) / sample_rate)
    
    # Highpass filtered noise wash building up
    noise = np.random.normal(0, 0.4, len(t))
    sos = signal.butter(3, 800, 'hp', fs=sample_rate, output='sos')
    noise_wash = signal.sosfilt(sos, noise) * (progress ** 2.2) * 0.4
    
    combined = (synth * tremolo * 0.7 + noise_wash) * vol_env
    combined = np.tanh(combined * 1.6) * 0.95
    
    # Clean tail silence or transient hit right at the finish
    tail_len = int(0.04 * sample_rate)
    combined[-tail_len:] *= np.linspace(1, 0, tail_len)
    
    stereo = np.column_stack((combined, combined))
    peak_offset = float(duration - 0.05)
    return stereo, peak_offset

def create_sub_drop(duration=3.0, sample_rate=48000):
    """Massive sub drop / downshifter (130Hz sliding to 28Hz)."""
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
    f_start, f_end = 135.0, 28.0
    freq = f_start * (f_end / f_start) ** (t / duration)
    phase = 2 * np.pi * np.cumsum(freq) / sample_rate
    
    # Exponential decay with warm 2nd harmonic
    env = np.exp(-t / 1.1)
    body = (np.sin(phase) + 0.3 * np.sin(phase * 2)) * env
    body = np.tanh(body * 2.0) * 0.95
    
    stereo = np.column_stack((body, body))
    return stereo, 0.04

def create_glitch(duration=1.2, sample_rate=48000):
    """Digital stutter / cyber glitch effect with fast micro-clicks and FM chirps."""
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
    out = np.zeros_like(t)
    
    # Rapid micro-bursts
    num_bursts = int(duration * 12)
    for _ in range(num_bursts):
        pos = np.random.uniform(0.05, duration - 0.1)
        burst_dur = np.random.uniform(0.015, 0.06)
        b_idx = int(pos * sample_rate)
        b_len = int(burst_dur * sample_rate)
        if b_idx + b_len < len(t):
            bt = np.linspace(0, burst_dur, b_len)
            carrier = np.random.uniform(300, 3200)
            mod = np.random.uniform(40, 200)
            chirp = np.sin(2 * np.pi * carrier * bt + np.sin(2 * np.pi * mod * bt) * 3)
            out[b_idx:b_idx+b_len] += chirp * np.hanning(b_len) * np.random.uniform(0.5, 0.9)
    
    out = np.clip(out, -0.95, 0.95)
    stereo = np.column_stack((out, np.roll(out, 120)))
    peak_offset = float(np.argmax(np.abs(out)) / sample_rate)
    return stereo, peak_offset

def generate_sfx(prompt, duration=None, output_path=None):
    """Generate SFX based on prompt categorization."""
    p = prompt.lower()
    sample_rate = 48000
    
    category = "impact"
    default_duration = 2.5
    
    if any(w in p for w in ["whoosh", "swoosh", "transition", "flyby", "swish", "pass"]):
        category = "whoosh"
        default_duration = 1.6
    elif any(w in p for w in ["riser", "buildup", "crescendo", "tension", "build"]):
        category = "riser"
        default_duration = 3.5
    elif any(w in p for w in ["sub drop", "downshifter", "bass drop", "subdrop"]):
        category = "sub_drop"
        default_duration = 3.0
    elif any(w in p for w in ["glitch", "stutter", "digital", "cyber", "robot", "blip"]):
        category = "glitch"
        default_duration = 1.2
    elif any(w in p for w in ["impact", "hit", "boom", "slam", "punch", "drop", "thud"]):
        category = "impact"
        default_duration = 2.5
    
    target_duration = float(duration) if duration and float(duration) > 0 else default_duration
    
    if category == "whoosh":
        audio, peak_offset = create_whoosh(target_duration, sample_rate)
    elif category == "riser":
        audio, peak_offset = create_riser(target_duration, sample_rate)
    elif category == "sub_drop":
        audio, peak_offset = create_sub_drop(target_duration, sample_rate)
    elif category == "glitch":
        audio, peak_offset = create_glitch(target_duration, sample_rate)
    else:
        audio, peak_offset = create_impact(target_duration, sample_rate)
    
    # Peak normalization
    max_val = np.max(np.abs(audio))
    if max_val > 0.001:
        audio = (audio / max_val) * 0.95
        
    if not output_path:
        cache_dir = os.path.expanduser("~/.cache/kdenlive/sfx")
        os.makedirs(cache_dir, exist_ok=True)
        safe_name = "".join(c if c.isalnum() else "_" for c in p[:24]).strip("_")
        output_path = os.path.join(cache_dir, f"sfx_{category}_{safe_name}_{int(np.random.randint(1000, 9999))}.wav")
    else:
        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        
    sf.write(output_path, audio, sample_rate, subtype='PCM_24')
    
    result = {
        "status": "success",
        "category": category,
        "prompt": prompt,
        "file_path": output_path,
        "duration": round(float(target_duration), 3),
        "peak_offset_seconds": round(float(peak_offset), 3),
        "sample_rate": sample_rate,
        "channels": 2
    }
    return result

def main():
    parser = argparse.ArgumentParser(description="High-Fidelity Local Cinematic SFX Generator")
    parser.add_argument("--prompt", required=True, help="SFX description prompt")
    parser.add_argument("--duration", type=float, default=None, help="Desired duration in seconds")
    parser.add_argument("--output", default=None, help="Output WAV file path")
    
    args = parser.parse_args()
    try:
        res = generate_sfx(args.prompt, args.duration, args.output)
        print(json.dumps(res, indent=2))
        return 0
    except Exception as e:
        err = {"status": "error", "message": str(e)}
        print(json.dumps(err, indent=2), file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
