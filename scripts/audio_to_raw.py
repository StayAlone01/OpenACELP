#!/usr/bin/env python3
# ------------------------------------------------------------------
# audio_to_raw.py - Convert a corpus of audio files to the OpenACELP
# training format: 8 kHz, 16-bit signed, mono, little-endian RAW.
#
# Usage:
#   python3 audio_to_raw.py INPUT_DIR OUTPUT_DIR [--force] [--recursive]
#
# Conversion backends, in order of preference:
#   1. ffmpeg            (handles flac/wav/mp3/ogg/opus/m4a/...)
#   2. soundfile         (FLAC/OGG/MP3/WAV via bundled libsndfile,
#                         python3 -m pip install --user soundfile)
#   3. stdlib wave + numpy FFT resampler (WAV only)
#
# --recursive scans sub-directories too (e.g. LibriSpeech's
# train-clean-100/<book>/<chapter>/ layout) and writes all RAW files
# flat into OUTPUT_DIR (LibriSpeech file names are unique, so this is
# safe; for other corpora run per-directory if names can collide).
#
# Output: one "<name>.raw" per input file (the encoder's -DGAIN_TRAINING
# mode reads these to collect gain features).
# ------------------------------------------------------------------

import glob
import os
import shutil
import subprocess
import sys
import wave

SR = 8000  # target sample rate
FORMAT_OK = "  OK "
FORMAT_SKIP = "skip"

AUDIO_EXTS = (".wav", ".flac", ".mp3", ".ogg", ".opus", ".m4a", ".aiff")


def collect_files(src_dir, recursive):
    """Find the audio files to convert."""
    files = []
    if recursive:
        for root, _dirs, names in os.walk(src_dir):
            for n in names:
                if n.lower().endswith(AUDIO_EXTS):
                    files.append(os.path.join(root, n))
    else:
        for e in AUDIO_EXTS:
            files.extend(glob.glob(os.path.join(src_dir, "*" + e)))
            files.extend(glob.glob(os.path.join(src_dir, "*" + e.upper())))
    return sorted(set(files))


def find_ffmpeg():
    return shutil.which("ffmpeg")


def have_soundfile():
    try:
        import soundfile  # noqa: F401

        return True
    except ImportError:
        return False


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
            raise ValueError("only 16-bit WAV supported without ffmpeg/soundfile")
        data = w.readframes(n)
    if nch == 1:
        ch = data
    else:
        # take the first channel
        ch = b"".join(data[i * 2 * nch : i * 2 * nch + 2] for i in range(n))
    import numpy as np

    x = np.frombuffer(ch, dtype="<i2").astype(np.float64) / 32768.0
    return x, sr


def read_soundfile(path):
    """Return (samples_float, sample_rate) via soundfile (FLAC/OGG/MP3/WAV)."""
    import soundfile as sf

    x, sr = sf.read(path, dtype="float32", always_2d=True)
    return x[:, 0], sr


def resample(x, fs_in, fs_out):
    """Band-limited FFT resampling (good enough for codebook training),
    processed in blocks so long files stay memory-light."""
    import numpy as np

    if fs_in == fs_out:
        return x
    block = 1 << 20  # ~65 s at 16 kHz per block
    out = []
    for s in range(0, len(x), block):
        xb = x[s : s + block]
        n = len(xb)
        m = int(round(n * float(fs_out) / float(fs_in)))
        if m == 0:
            continue
        X = np.fft.rfft(xb)
        keep = min(m // 2 + 1, len(X))
        Y = np.zeros(m // 2 + 1, dtype=complex)
        Y[:keep] = X[:keep]
        out.append(np.fft.irfft(Y, n=m) * (float(m) / float(n)))
    if not out:
        return x[:0]
    return np.concatenate(out)


def write_pcm(y, dst):
    import numpy as np

    y = np.clip(y, -1.0, 1.0)
    pcm = (y * 32767.0).astype("<i2")
    with open(dst, "wb") as f:
        f.write(pcm.tobytes())


def convert_soundfile(src, dst):
    """FLAC/OGG/MP3/WAV via soundfile + numpy resampler."""
    x, sr = read_soundfile(src)
    write_pcm(resample(x, sr, SR), dst)


def convert_numpy(src, dst):
    """WAV-only fallback without ffmpeg or soundfile."""
    x, sr = read_wav(src)
    write_pcm(resample(x, sr, SR), dst)


def main():
    if len(sys.argv) < 3:
        print(
            "usage: %s INPUT_DIR OUTPUT_DIR [--force] [--recursive]" % sys.argv[0],
            file=sys.stderr,
        )
        sys.exit(1)

    src_dir = sys.argv[1]
    dst_dir = sys.argv[2]
    force = "--force" in sys.argv[3:]
    recursive = "--recursive" in sys.argv[3:]

    if not os.path.isdir(src_dir):
        print("error: input dir not found: %s" % src_dir, file=sys.stderr)
        sys.exit(1)
    os.makedirs(dst_dir, exist_ok=True)

    ffmpeg = find_ffmpeg()
    sf_ok = have_soundfile()
    if ffmpeg:
        print("backend: ffmpeg (%s)" % ffmpeg)
    elif sf_ok:
        print("backend: soundfile (FLAC/OGG/MP3/WAV supported, no ffmpeg)")
    else:
        print(
            "backend: stdlib wave only - install soundfile for FLAC/OGG/MP3"
            " (python3 -m pip install --user soundfile)"
        )

    files = collect_files(src_dir, recursive)

    if not files:
        print("no audio files found in %s" % src_dir, file=sys.stderr)
        sys.exit(1)
    print(
        "found %d audio files (%s scan)"
        % (len(files), "recursive" if recursive else "top-level")
    )

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
            elif sf_ok:
                convert_soundfile(src, dst)
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
