#!/usr/bin/env python3
"""Generate game/assets/sounds/*.wav -- the starter sound set.

Pure stdlib (wave + math). 44.1 kHz 16-bit PCM, mono except rain_loop
(stereo). File stems are the names the game plays ("mine", "hum_loop", ...);
keep them in sync with the audio() calls in game/src. Rerun after editing:
python tools/make_sounds.py

The output is a *starter* -- every file can be replaced by a hand-made or
recorded WAV of the same name; this script just guarantees a complete,
coherent set to start from. Loops (hum_loop, rain_loop) must stay seamless:
hum uses integer-cycle sines, rain crossfades its tail onto its head.
"""

import math
import random
import wave
from pathlib import Path

RATE = 44100
PEAK = 0.7  # normalize every file to this, leaving mix headroom

rng = random.Random(1234)  # deterministic output


# --- tiny synthesis kit ------------------------------------------------------

def silence(seconds):
    return [0.0] * int(RATE * seconds)


def add_sine(buf, freq, amp=1.0, decay=None, delay=0.0, attack=0.002,
             freq_end=None):
    """Mix a sine into buf: optional exp decay (tau seconds), start delay,
    short attack ramp (no start click), linear freq sweep to freq_end."""
    start = int(delay * RATE)
    n = len(buf) - start
    phase = 0.0
    for i in range(n):
        t = i / RATE
        f = freq if freq_end is None else freq + (freq_end - freq) * (i / n)
        phase += 2.0 * math.pi * f / RATE
        a = amp
        if decay:
            a *= math.exp(-t / decay)
        if t < attack:
            a *= t / attack
        buf[start + i] += a * math.sin(phase)


def add_noise(buf, amp=1.0, decay=None, lowpass=None):
    """Mix white noise, optionally exp-decayed / one-pole low-passed."""
    k = None
    if lowpass:
        k = 1.0 - math.exp(-2.0 * math.pi * lowpass / RATE)
    state = 0.0
    for i in range(len(buf)):
        s = rng.uniform(-1.0, 1.0)
        if k is not None:
            state += k * (s - state)
            s = state
        a = amp * (math.exp(-(i / RATE) / decay) if decay else 1.0)
        buf[i] += a * s


def loopify(buf, overlap_seconds):
    """Make a seamless loop: equal-power crossfade the tail onto the head,
    then drop the tail. Returns the shortened buffer."""
    ov = int(overlap_seconds * RATE)
    keep = len(buf) - ov
    out = buf[:keep]
    for i in range(ov):
        w = i / ov  # 0 -> 1 across the overlap
        out[i] = math.cos(w * math.pi / 2) * buf[keep + i] + \
            math.sin(w * math.pi / 2) * out[i]
    # started the file inside the crossfaded region: head now matches tail
    return out


def fade_out(buf, seconds=0.01):
    """Linear fade at the very end so one-shots never end on a click."""
    n = min(len(buf), int(seconds * RATE))
    for i in range(n):
        buf[len(buf) - n + i] *= 1.0 - (i + 1) / n


def write_wav(name, *channels):
    """Peak-normalize to PEAK and write mono/stereo 16-bit PCM."""
    peak = max(max(abs(s) for s in ch) for ch in channels)
    scale = (PEAK / peak) if peak > 0 else 0.0
    frames = bytearray()
    for i in range(len(channels[0])):
        for ch in channels:
            v = int(max(-1.0, min(1.0, ch[i] * scale)) * 32767)
            frames += v.to_bytes(2, "little", signed=True)
    out = OUT_DIR / f"{name}.wav"
    with wave.open(str(out), "wb") as w:
        w.setnchannels(len(channels))
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    print(f"wrote {out} ({len(channels[0]) / RATE:.3f}s, "
          f"{'stereo' if len(channels) > 1 else 'mono'})")


# --- the sounds --------------------------------------------------------------

def mine():
    """Rock crunch: noise burst + low thump sweeping down."""
    b = silence(0.12)
    add_noise(b, amp=0.9, decay=0.035, lowpass=3200)
    add_sine(b, 180, amp=0.8, decay=0.05, freq_end=90)
    fade_out(b)
    write_wav("mine", b)


def place():
    """Soft thock: damped low sine + a tiny click transient."""
    b = silence(0.09)
    for i in range(int(0.003 * RATE)):  # 3 ms click transient
        b[i] += 0.6 * rng.uniform(-1.0, 1.0)
    add_sine(b, 220, amp=1.0, decay=0.04)
    fade_out(b)
    write_wav("place", b)


def hum_loop():
    """Machine hum, exactly 1 s: integer-cycle sines are seamless by
    construction; the quiet noise bed is loop-crossfaded."""
    b = silence(1.25)
    add_noise(b, amp=0.10, lowpass=400)
    b = loopify(b, 0.25)  # 1.000 s remains
    for freq, amp in ((60, 1.0), (120, 0.55), (180, 0.30), (240, 0.15)):
        add_sine(b, freq, amp=amp, attack=0.0)  # integer cycles in 1 s
    write_wav("hum_loop", b)


def rain_loop():
    """Rain bed, exactly 2 s stereo: two decorrelated low-passed noise
    streams, each tail-to-head crossfaded."""
    chans = []
    for _ in range(2):
        c = silence(2.5)
        add_noise(c, amp=1.0, lowpass=1200)
        chans.append(loopify(c, 0.5))  # 2.000 s remains
    write_wav("rain_loop", *chans)


def click():
    """UI tick."""
    b = silence(0.03)
    add_sine(b, 1500, amp=1.0, decay=0.008, attack=0.001)
    fade_out(b, 0.005)
    write_wav("click", b)


def blips(name, f0, f1):
    """Two quick blips (rising = open, falling = close)."""
    b = silence(0.08)
    add_sine(b, f0, amp=1.0, decay=0.02, attack=0.001)
    add_sine(b, f1, amp=1.0, decay=0.02, delay=0.04, attack=0.001)
    fade_out(b)
    write_wav(name, b)


def craft():
    """Success chime: two notes, the second brighter."""
    b = silence(0.15)
    add_sine(b, 660, amp=1.0, decay=0.04, attack=0.001)
    add_sine(b, 990, amp=0.9, decay=0.06, delay=0.06, attack=0.001)
    fade_out(b)
    write_wav("craft", b)


def deny():
    """Low buzz: flat then a quick fade -- 'can't do that'."""
    b = silence(0.10)
    for i in range(len(b)):
        t = i / RATE
        s = 1.0 if math.sin(2.0 * math.pi * 160 * t) >= 0 else -1.0  # square
        b[i] += 0.5 * s + 0.25 * math.sin(2.0 * math.pi * 160 * t)
    fade_out(b, 0.03)
    write_wav("deny", b)


# NOTE: new sounds are appended AFTER the originals (shared rng) so rerunning
# the script keeps every previously-committed WAV byte-identical.

def hurt():
    """Player damage: dull low thump sweeping down + a grunt of noise."""
    b = silence(0.16)
    add_noise(b, amp=0.5, decay=0.05, lowpass=900)
    add_sine(b, 130, amp=1.0, decay=0.08, freq_end=70)
    fade_out(b)
    write_wav("hurt", b)


def heal():
    """Drinking a draught: three soft rising notes -- restorative."""
    b = silence(0.25)
    add_sine(b, 520, amp=0.8, decay=0.06, attack=0.005)
    add_sine(b, 660, amp=0.8, decay=0.06, delay=0.08, attack=0.005)
    add_sine(b, 880, amp=0.9, decay=0.09, delay=0.16, attack=0.005)
    fade_out(b)
    write_wav("heal", b)


OUT_DIR = Path(__file__).resolve().parent.parent / "game" / "assets" / "sounds"


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    mine()
    place()
    hum_loop()
    rain_loop()
    click()
    blips("open", 600, 900)
    blips("close", 900, 600)
    craft()
    deny()
    hurt()
    heal()


if __name__ == "__main__":
    main()
