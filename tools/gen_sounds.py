"""Generate placeholder action sounds as 22050 Hz mono 16-bit WAVs.

Usage (run from the repo root):
    python3 tools/gen_sounds.py          writes assets/sounds/<id>.wav
    python3 tools/gen_sounds.py --sheet  also writes assets/sounds_sheet.wav (every sound in sequence)

Python stdlib only; output is deterministic.
"""
import math
import os
import random
import sys
import wave
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOUNDS = os.path.join(ROOT, "assets", "sounds")
SHEET = os.path.join(ROOT, "assets", "sounds_sheet.wav")
RATE = 22050
PEAK = 10 ** (-3 / 20)
FADE_SECONDS = 0.05
SHEET_GAP_SECONDS = 0.3
TAU = 2 * math.pi

ELEMENTS = ["kinetic", "fire", "frost", "lightning", "radiance", "venom", "mind", "void"]
ELEMENT_PITCH = {
    "kinetic": 150, "fire": 300, "frost": 880, "lightning": 600,
    "radiance": 1047, "venom": 400, "mind": 330, "void": 70,
}

ABILITY_SOUNDS = [
    ("kinetic", "block"), ("kinetic", "cast"), ("kinetic", "spawn"), ("kinetic", "strike"),
    ("fire", "cast"), ("frost", "strike"), ("lightning", "cast"), ("lightning", "strike"),
    ("radiance", "cast"), ("radiance", "strike"), ("radiance", "support"),
    ("venom", "strike"), ("mind", "cast"), ("void", "strike"),
]


def count(seconds):
    return int(seconds * RATE)


def lowpass(signal, cutoff):
    k = 1 - math.exp(-TAU * cutoff / RATE)
    out, y = [], 0.0
    for x in signal:
        y += k * (x - y)
        out.append(y)
    return out


def highpass(signal, cutoff):
    return [x - y for x, y in zip(signal, lowpass(signal, cutoff))]


def noise(rng, n):
    return [rng.uniform(-1, 1) for _ in range(n)]


def contour(n, start, end):
    return [start + (end - start) * i / max(n - 1, 1) for i in range(n)]


def oscillator(freqs, shape="sine"):
    out, phase = [], 0.0
    for f in freqs:
        phase += f / RATE
        p = phase % 1.0
        if shape == "sine":
            out.append(math.sin(TAU * p))
        elif shape == "square":
            out.append(1.0 if p < 0.5 else -1.0)
        else:
            out.append(2 * p - 1)
    return out


def partials(freqs, ratios, decay):
    out = [0.0] * len(freqs)
    for r, level in ratios:
        wave_r = oscillator([f * r for f in freqs])
        for i, w in enumerate(wave_r):
            out[i] += level * w * math.exp(-decay * r * i / RATE)
    return out


def mix(*parts):
    n = max(len(p) for p in parts)
    return [sum(p[i] for p in parts if i < len(p)) for i in range(n)]


def scale(signal, k):
    return [x * k for x in signal]


def envelope(n, attack, decay_tau):
    a = max(count(attack), 1)
    return [min(i / a, 1.0) * math.exp(-i / RATE / decay_tau) for i in range(n)]


def swell(n):
    return [math.sin(math.pi * (i / n) ** 1.6) for i in range(n)]


def apply(signal, env):
    return [x * e for x, e in zip(signal, env)]


def voice(element, freqs, rng):
    n = len(freqs)
    if element == "kinetic":
        return mix(oscillator(freqs), scale(lowpass(noise(rng, n), 900), 0.8))
    if element == "fire":
        crackle = [1.0 if rng.random() < 0.004 else 0.0 for _ in range(n)]
        gate = lowpass(crackle, 400)
        peak = max(gate) or 1.0
        return apply(lowpass(noise(rng, n), 3000), [3 * g / peak for g in gate])
    if element == "frost":
        return partials(freqs, [(1, 1.0), (2.76, 0.5), (5.4, 0.25)], 9)
    if element == "lightning":
        return mix(oscillator(freqs, "square"), scale(noise(rng, n), 0.5))
    if element == "radiance":
        return partials(freqs, [(1, 1.0), (2, 0.6), (3, 0.35), (4.2, 0.2)], 6)
    if element == "venom":
        hiss = highpass(noise(rng, n), 1500)
        return apply(hiss, [0.6 + 0.4 * math.sin(TAU * 28 * i / RATE) for i in range(n)])
    if element == "mind":
        wobbled = [f * (1 + 0.06 * math.sin(TAU * 7 * i / RATE)) for i, f in enumerate(freqs)]
        return mix(oscillator(wobbled), scale(oscillator([f * 2 for f in wobbled]), 0.3))
    sub = oscillator([f * 0.5 for f in freqs])
    return mix(oscillator(freqs), sub, scale(lowpass(noise(rng, n), 300), 1.5))


def strike(element, rng, pitch=1.0, seconds=0.22):
    n = count(seconds)
    base = ELEMENT_PITCH[element] * pitch
    return apply(voice(element, contour(n, base * 1.3, base * 0.8), rng), envelope(n, 0.001, 0.06))


def cast(element, rng):
    n = count(0.5)
    base = ELEMENT_PITCH[element]
    return apply(voice(element, contour(n, base * 0.7, base * 1.25), rng), swell(n))


def block(element, rng):
    n = count(0.25)
    base = ELEMENT_PITCH[element] * 0.6
    body = apply(voice(element, contour(n, base, base * 0.9), rng), envelope(n, 0.001, 0.07))
    return lowpass(body, 1200)


def support(element, rng):
    n = count(0.45)
    base = ELEMENT_PITCH[element]
    return apply(voice(element, contour(n, base, base * 1.02), rng), envelope(n, 0.03, 0.15))


def spawn(element, rng):
    n = count(0.3)
    base = ELEMENT_PITCH[element] * 0.8
    popped = voice(element, contour(n, base * 0.5, base * 1.6), rng)
    bubbling = [0.55 + 0.45 * math.sin(TAU * 24 * i / RATE) for i in range(n)]
    return apply(apply(popped, bubbling), envelope(n, 0.002, 0.1))


def hit(element, rng):
    return strike(element, rng, pitch=1.6, seconds=0.16)


def effect(element, rng):
    n = count(0.35)
    base = ELEMENT_PITCH[element] * 1.3
    pulses = [0.5 + 0.5 * math.sin(TAU * 12 * i / RATE) ** 2 for i in range(n)]
    return apply(apply(voice(element, contour(n, base, base * 1.1), rng), pulses), envelope(n, 0.005, 0.18))


def step_feet(rng):
    n = count(0.08)
    return apply(lowpass(noise(rng, n), 700), envelope(n, 0.002, 0.02))


def step_wings(rng):
    n = count(0.18)
    return apply(highpass(lowpass(noise(rng, n), 2500), 300), swell(n))


def step_slither(rng):
    n = count(0.22)
    rustle = [0.5 + 0.5 * math.sin(TAU * 22 * i / RATE) for i in range(n)]
    return apply(apply(highpass(noise(rng, n), 2000), rustle), swell(n))


def step_levitate(rng):
    n = count(0.22)
    hum = oscillator([180 * (1 + 0.04 * math.sin(TAU * 9 * i / RATE)) for i in range(n)])
    return apply(hum, swell(n))


def pickup_health(rng):
    n = count(0.15)
    low = apply(partials(contour(n, 784, 784), [(1, 1.0), (2, 0.3)], 12), envelope(n, 0.005, 0.08))
    high = apply(partials(contour(n, 1175, 1175), [(1, 1.0), (2, 0.3)], 12), envelope(n, 0.005, 0.1))
    return mix(low, [0.0] * count(0.12) + high)


def pickup_rage(rng):
    n = count(0.35)
    growl = oscillator(contour(n, 90, 260), "saw")
    return apply(lowpass(growl, 1500), swell(n))


def trap_fire_vent(rng):
    n = count(0.4)
    roar = apply(lowpass(noise(rng, n), 1800), swell(n))
    return mix(roar, strike("fire", rng, 1.0, 0.4))


def trap_frost_snare(rng):
    n = count(0.3)
    snap = apply(highpass(noise(rng, n), 3000), envelope(n, 0.001, 0.015))
    return mix(snap, scale(strike("frost", rng, 1.0, 0.3), 0.7))


def cue_death(rng):
    n = count(0.55)
    fall = oscillator(contour(n, 330, 55))
    return apply(mix(fall, scale(lowpass(noise(rng, n), 500), 0.6)), envelope(n, 0.005, 0.2))


def cue_part_destroyed(rng):
    n = count(0.3)
    crunch = apply(lowpass(noise(rng, n), 2200), envelope(n, 0.001, 0.05))
    thud = apply(oscillator(contour(n, 120, 50)), envelope(n, 0.001, 0.09))
    return mix(crunch, thud)


def cue_spawn_appear(rng):
    n = count(0.35)
    return apply(oscillator(contour(n, 220, 880)), envelope(n, 0.02, 0.12))


def build_sounds():
    sounds = {}
    for e in ELEMENTS:
        sounds[f"{e}_hit"] = lambda rng, e=e: hit(e, rng)
        sounds[f"{e}_effect"] = lambda rng, e=e: effect(e, rng)
    orders = {"strike": strike, "cast": cast, "block": block, "support": support, "spawn": spawn}
    for e, order in ABILITY_SOUNDS:
        sounds[f"{e}_{order}"] = lambda rng, e=e, make=orders[order]: make(e, rng)
    sounds.update({
        "step_feet": step_feet, "step_wings": step_wings,
        "step_slither": step_slither, "step_levitate": step_levitate,
        "pickup_health": pickup_health, "pickup_rage": pickup_rage,
        "trap_fire_vent": trap_fire_vent, "trap_frost_snare": trap_frost_snare,
        "cue_death": cue_death, "cue_part_destroyed": cue_part_destroyed,
        "cue_spawn_appear": cue_spawn_appear,
    })
    return sounds


def finish(signal):
    mean = sum(signal) / len(signal)
    centered = [x - mean for x in signal]
    fade = min(count(FADE_SECONDS), len(centered) // 3)
    for i in range(fade):
        centered[-1 - i] *= (i / fade) ** 2
    peak = max(abs(x) for x in centered)
    return [int(round(x / peak * PEAK * 32767)) for x in centered]


def write_wav(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(s.to_bytes(2, "little", signed=True) for s in samples))


def render(sound_id, make):
    return finish(make(random.Random(zlib.crc32(sound_id.encode()))))


def main():
    os.makedirs(SOUNDS, exist_ok=True)
    rendered = {sid: render(sid, make) for sid, make in sorted(build_sounds().items())}
    for sid, samples in rendered.items():
        write_wav(os.path.join(SOUNDS, f"{sid}.wav"), samples)
    if "--sheet" in sys.argv:
        gap = [0] * count(SHEET_GAP_SECONDS)
        write_wav(SHEET, [s for samples in rendered.values() for s in samples + gap])


if __name__ == "__main__":
    main()
