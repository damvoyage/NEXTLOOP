#!/usr/bin/env python3
"""Generates synthetic test tracks in every supported format.

Each track is a drum + bass + chord loop at a known BPM, starting after a known
silence offset, in a known key. Usage: make_test_audio.py <output dir>
"""
import json, os, subprocess, sys
import numpy as np

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def make_track(bpm, seconds, sr, offset, root_midi, minor=True, seed=0):
    rng = np.random.default_rng(seed)
    n = int(seconds * sr)
    x = np.zeros((n, 2), dtype=np.float64)
    beat = 60.0 / bpm
    t_kick = np.arange(int(0.25 * sr)) / sr

    kick = np.sin(2 * np.pi * (50 + 120 * np.exp(-t_kick * 30)) * t_kick) * np.exp(-t_kick * 9)
    hat_len = int(0.05 * sr)
    hat = rng.standard_normal(hat_len) * np.exp(-np.arange(hat_len) / sr * 70) * 0.25
    hat = np.diff(hat, prepend=0)  # brighten

    def add(sig, start, gain=1.0):
        s = int(round(start * sr))
        if s >= n:
            return
        e = min(n, s + len(sig))
        x[s:e, 0] += sig[: e - s] * gain
        x[s:e, 1] += sig[: e - s] * gain

    k = 0
    while offset + k * beat < seconds:
        tb = offset + k * beat
        add(kick, tb, 0.9)
        add(hat, tb + beat / 2, 0.6)
        k += 1

    # chords (root, minor/major third, fifth) + bass, changing every bar between i and VI
    third = 3 if minor else 4
    progression = [0, 0, 5, 0] if minor else [0, 0, 5, 7]  # i-i-iv-i / I-I-IV-V
    bar = beat * 4
    b = 0
    while offset + b * bar < seconds:
        root = root_midi + progression[b % 4]
        start = offset + b * bar
        length = int(bar * sr)
        tt = np.arange(length) / sr
        env = np.minimum(1, tt * 20) * np.exp(-tt * 0.6)
        chord = np.zeros(length)
        for iv in (0, third, 7, 12):
            f = 440.0 * 2 ** ((root + iv - 69) / 12)
            chord += np.sin(2 * np.pi * f * tt) * 0.06
        bass_f = 440.0 * 2 ** ((root - 24 - 69) / 12)
        bass = np.sin(2 * np.pi * bass_f * tt) * 0.18 * env
        add(chord * env + bass, start)
        b += 1

    x /= np.max(np.abs(x)) * 1.12
    return x.astype(np.float32)


def write_wav_float(path, data, sr):
    import wave, struct
    # write 32-bit float WAV manually (wave module only does PCM)
    n, ch = data.shape
    raw = data.tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF')
        f.write(struct.pack('<I', 36 + len(raw) + 12))
        f.write(b'WAVEfmt ')
        f.write(struct.pack('<IHHIIHH', 18, 3, ch, sr, sr * ch * 4, ch * 4, 32))
        f.write(struct.pack('<H', 0))
        f.write(b'fact')
        f.write(struct.pack('<II', 4, n))
        f.write(b'data')
        f.write(struct.pack('<I', len(raw)))
        f.write(raw)


def ffmpeg(src, dst, *args):
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', src, *args, dst], check=True)


manifest = []

def emit(name, bpm, offset, key, camelot, sr, seconds, formats, minor=True, root=57, seed=0):
    data = make_track(bpm, seconds, sr, offset, root, minor, seed)
    master = os.path.join(out, f'{name}_master_f32.wav')
    write_wav_float(master, data, sr)
    for fmt, ext, args in formats:
        dst = os.path.join(out, f'{name}_{fmt}.{ext}')
        ffmpeg(master, dst, *args)
        manifest.append(dict(file=os.path.basename(dst), bpm=bpm, offset=offset, key=key,
                             camelot=camelot, sr=sr, seconds=seconds, format=fmt))
    manifest.append(dict(file=os.path.basename(master), bpm=bpm, offset=offset, key=key,
                         camelot=camelot, sr=sr, seconds=seconds, format='wav_f32'))


all_formats = [
    ('wav16', 'wav', ['-c:a', 'pcm_s16le']),
    ('wav24', 'wav', ['-c:a', 'pcm_s24le']),
    ('aiff16', 'aiff', ['-c:a', 'pcm_s16be']),
    ('aiff24', 'aif', ['-c:a', 'pcm_s24be']),
    ('flac16', 'flac', ['-c:a', 'flac', '-sample_fmt', 's16']),
    ('flac24', 'flac', ['-c:a', 'flac', '-sample_fmt', 's32', '-bits_per_raw_sample', '24']),
    ('mp3_320', 'mp3', ['-c:a', 'libmp3lame', '-b:a', '320k']),
    ('mp3_v0', 'mp3', ['-c:a', 'libmp3lame', '-q:a', '0']),
    ('mp3_128', 'mp3', ['-c:a', 'libmp3lame', '-b:a', '128k']),
    ('mp3_96mono', 'mp3', ['-c:a', 'libmp3lame', '-b:a', '96k', '-ac', '1']),
]

# A minor, 126 BPM, 44.1k — every format
emit('house126', 126.0, 0.37, 'Am', '8A', 44100, 60, all_formats)
# 120 BPM in C major at 48k (sync partner)
emit('groove120', 120.0, 0.11, 'C', '8B', 48000, 60,
     [('wav16', 'wav', ['-c:a', 'pcm_s16le']), ('flac16', 'flac', ['-c:a', 'flac'])], minor=False, root=60, seed=1)
# 174 DnB at 96k, E minor
emit('dnb174', 174.0, 0.05, 'Em', '9A', 96000, 40,
     [('wav24', 'wav', ['-c:a', 'pcm_s24le']), ('mp3_320', 'mp3', ['-c:a', 'libmp3lame', '-b:a', '320k'])], root=52, seed=2)
# 93.5 hip hop tempo in D minor
emit('hiphop935', 93.5, 0.6, 'Dm', '7A', 44100, 50,
     [('wav16', 'wav', ['-c:a', 'pcm_s16le']), ('aiff16', 'aiff', ['-c:a', 'pcm_s16be'])], root=50, seed=3)

# pure 440 Hz tone for keylock pitch tests
sr = 48000
t = np.arange(sr * 20) / sr
tone = (0.5 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
write_wav_float(os.path.join(out, 'tone440.wav'), np.stack([tone, tone], 1), sr)

# click track for alignment tests: 1 ms broadband clicks exactly on every beat, 120 BPM from 0.25 s
sr = 48000
clicks = np.zeros(sr * 30, dtype=np.float32)
rng = np.random.default_rng(7)
k = 0
while 0.25 + k * 0.5 < 29.5:
    s0 = int(round((0.25 + k * 0.5) * sr))
    clicks[s0:s0 + 48] = (rng.standard_normal(48) * 0.6).astype(np.float32)
    k += 1
write_wav_float(os.path.join(out, 'clicks120.wav'), np.stack([clicks, clicks], 1), sr)

with open(os.path.join(out, 'manifest.json'), 'w') as f:
    json.dump(manifest, f, indent=1)
print(f'wrote {len(manifest)} test files to {out}')
