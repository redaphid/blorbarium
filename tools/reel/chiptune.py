"""A little swamp chiptune for the reel, made here so nobody else owns it.

    python3 tools/reel/chiptune.py OUT.wav SECONDS

Square-wave lead and bass and a noise hat over i-VI-III-VII in A minor, the
lead a seeded walk on the pentatonic, so the same seconds give the same file.
Stdlib only: the reel's venv has nothing else to lean on.
"""
import array
import math
import random
import sys
import wave

RATE = 22050
BEAT = 0.25                                     # an eighth at 120 bpm
CHORDS = [57, 53, 48, 55]                       # A F C G roots (MIDI), two seconds each
SCALE = [57, 60, 62, 64, 67, 69, 72, 74, 76]    # A minor pentatonic, two octaves


def hz(note): return 440 * 2 ** ((note - 69) / 12)


def square(buf, start, length, freq, vol, duty=0.5):
    period = RATE / freq
    for i in range(length):
        if start + i >= len(buf): return
        env = math.exp(-4.0 * i / length)
        buf[start + i] += vol * env * (1 if (i % period) < period * duty else -1)


def noise(buf, start, length, vol, rng):
    for i in range(length):
        if start + i >= len(buf): return
        buf[start + i] += vol * math.exp(-12.0 * i / length) * (rng.random() * 2 - 1)


def main():
    out, seconds = sys.argv[1], float(sys.argv[2])
    n = int(seconds * RATE)
    buf = [0.0] * n
    rng = random.Random(0xB10B)
    step = int(BEAT * RATE)
    pos = 4
    for k in range(n // step + 1):
        chord = CHORDS[(k // 8) % len(CHORDS)]
        if k % 2 == 0: square(buf, k * step, step * 2, hz(chord - 24), 0.16)
        if k % 2 == 1: noise(buf, k * step, step // 2, 0.05, rng)
        if rng.random() < 0.8:
            pos = max(0, min(len(SCALE) - 1, pos + rng.choice([-2, -1, -1, 0, 1, 1, 2])))
            if k % 8 == 0: pos = min(range(len(SCALE)), key=lambda j: abs(SCALE[j] - chord - 12))
            square(buf, k * step, step, hz(SCALE[pos]), 0.10, duty=0.25)
    fade_in, fade_out = int(0.5 * RATE), int(2.5 * RATE)
    pcm = array.array("h")
    for i, v in enumerate(buf):
        g = min(1.0, i / fade_in, (n - i) / fade_out)
        pcm.append(int(max(-1.0, min(1.0, v * g)) * 32000))
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


if __name__ == "__main__":
    main()
