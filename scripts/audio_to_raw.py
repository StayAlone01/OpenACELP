#!/usr/bin/env python3
# ------------------------------------------------------------------
# audio_to_raw.py - Convert a corpus of audio files to the OpenACELP
# training format: 8 kHz, 16-bit signed, mono, little-endian RAW.
#
# Usage:
#   python3 audio_to_raw.py INPUT_DIR OUTPUT_DIR [--force]
#
# Uses ffmpeg when available (handles flac/wav/mp3/ogg/...). Without
# ffmpeg it falls back to a numpy band-limited resampler for WAV files
# (via the standard library `wave` module + numpy FFT resampling).
#
# Output: one "<name>.raw" per input file (the encoder's -DGAIN_TRAINING
# mode reads these to collect gain features).
# ------------------------------------------------------------------

import glob
import os
import shutil
import struct
import subprocess
import sys
import wave

SR = 8000  # target sample rate
FORMAT_OK = "  OK "
FORMAT_SKIP = "skip"


def find_ffmpeg():
    return shutil.which("ffmpeg")


def convert_ffmpeg(ffmpeg, src, dst):
    """8 kHz 16-bit mono RAW via ffmpeg."""
    subprocess.run(
        [
            ffmpeg,
            "-y",
            "-i",
            src,
            "-ac",
            "1",
            "-ar",
            str(SR),
            "-f",
            "s16le",
            "-acodec",
            "pcm_s16le",
            dst,
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def read_wav(path):
    """Return (samples_float, sample_rate) for a WAV file."""
    with wave.open(path, "rb") as w:
        nch, sw, sr, n = (
            w.getnchannels(),
            w.getsampwidth(),
            w.getframerate(),
            w.getnframes(),
        )
        if sw != 2:
            raise ValueError("only 16-bit WAV supported without ffmpeg")
        data = w.readframes(n)
    if nch == 1:
        ch = data
    else:
        # take the first channel
        ch = b"".join(data[i * 2 * nch : i * 2 * nch + 2] for i in range(n))
    import numpy as np

    x = np.frombuffer(ch, dtype="<i2").astype(np.float64) / 32768.0
    return x, sr


def resample(x, fs_in, fs_out):
    """Band-limited FFT resampling (good enough for codebook training)."""
    import numpy as np

    if fs_in == fs_out:
        return x
    n = len(x)
    m = int(round(n * float(fs_out) / float(fs_in)))
    if m == 0:
        return x[:0]
    X = np.fft.rfft(x)
    keep = min(m // 2 + 1, len(X))
    Y = np.zeros(m // 2 + 1, dtype=complex)
    Y[:keep] = X[:keep]
    y = np.fft.irfft(Y, n=m) * (float(m) / float(n))
    return y


def convert_numpy(src, dst):
    """WAV-only fallback without ffmpeg."""
    import numpy as np

    x, sr = read_wav(src)
    y = resample(x, sr, SR)
    # soft clip / scale back to int16
    y = np.clip(y, -1.0, 1.0)
    pcm = (y * 32767.0).astype("<i2")
    with open(dst, "wb") as f:
        f.write(pcm.tobytes())


def main():
    if len(sys.argv) < 3:
        print("usage: %s INPUT_DIR OUTPUT_DIR [--force]" % sys.argv[0], file=sys.stderr)
        sys.exit(1)

    src_dir = sys.argv[1]
    dst_dir = sys.argv[2]
    force = "--force" in sys.argv[3:]

    if not os.path.isdir(src_dir):
        print("error: input dir not found: %s" % src_dir, file=sys.stderr)
        sys.exit(1)
    os.makedirs(dst_dir, exist_ok=True)

    ffmpeg = find_ffmpeg()
    if ffmpeg:
        print("using ffmpeg: %s" % ffmpeg)
    else:
        print("ffmpeg not found - WAV files only (numpy resampler)")

    exts = ("*.wav", "*.flac", "*.mp3", "*.ogg", "*.opus", "*.m4a", "*.aiff")
    files = []
    for e in exts:
        files.extend(glob.glob(os.path.join(src_dir, e)))
    files = sorted(set(files))

    if not files:
        print("no audio files found in %s" % src_dir, file=sys.stderr)
        sys.exit(1)

    n_ok = n_skip = n_err = 0
    for src in files:
        name = os.path.splitext(os.path.basename(src))[0]
        dst = os.path.join(dst_dir, name + ".raw")
        if os.path.exists(dst) and not force:
            n_skip += 1
            print("%s %s (exists)" % (FORMAT_SKIP, name))
            continue
        try:
            if ffmpeg:
                convert_ffmpeg(ffmpeg, src, dst)
            else:
                convert_numpy(src, dst)
            n_ok += 1
            print("%s %s" % (FORMAT_OK, name))
        except Exception as e:
            n_err += 1
            print("FAIL %s (%s)" % (name, e))

    print("\n%d converted, %d skipped, %d failed" % (n_ok, n_skip, n_err))
    sys.exit(0 if n_err == 0 else 1)


if __name__ == "__main__":
    main()
