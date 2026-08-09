#!/usr/bin/env python3
# ------------------------------------------------------------------
# validate_lsp.py - parse -DLSP_DEBUG output and report LSP
# quantization quality (cl. 4.2.2.3 split VQ).
#
# Usage:
#   make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DLSP_DEBUG"
#   for f in held_out/*.raw; do ./openacelp "$f" > /dev/null 2>> lsp_debug.txt; done
#   python3 scripts/validate_lsp.py lsp_debug.txt
#
# Each debug line (one per frame, prefix "LSP") contains:
#   10 unquantized LSPs (cosine domain) + 10 quantized LSPs +
#   3 codebook indices + 1 fallback flag.
#
# Reported metrics:
#   - per-group quantization error (RMS, cosine domain and in Hz)
#   - codebook usage (distinct indices / size per group)
#   - spectral distortion (mean / max, dB) — the standard LSP quality
#     metric; < 1 dB mean is considered very good, < 2 dB acceptable.
#   - fallback count (ordering check failures -> previous frame used)
#
# Prints "RESULT: OK" and exits 0 when everything looks sane, else
# "RESULT: FAIL" and a non-zero exit code.
#
# Requires: numpy.
# ------------------------------------------------------------------

import sys

import numpy as np

FS = 8000.0  # sample rate
DIM = 10
GROUPS = [(0, 3, 256), (3, 6, 512), (6, 10, 512)]  # (start, end, size)
NFREQ = 256  # spectral-distortion frequency grid


# ------------------------------------------------------------------
# LSP -> A(z) conversion, a faithful port of src/lsp.c (LSP_Poly +
# LSP_LP) so the spectral distortion matches the encoder exactly.
# ------------------------------------------------------------------
def lsp_poly(lsp, f):
    """Fill f[0..5] from the 5 LSPs at lsp[0], lsp[2], ..., lsp[8]
    (caller passes the sub-array to select even or odd roots)."""
    k = 0
    f[0] = 1.0
    f[1] = -2.0 * lsp[k]
    k += 2
    for i in range(2, 6):
        f[i] = -2.0 * lsp[k] * f[i - 1] + 2.0 * f[i - 2]
        for j in range(i - 1, 0, -1):
            if j > 1:
                f[j] = f[j] - 2.0 * lsp[k] * f[j - 1] + f[j - 2]
            else:
                f[j] = f[j] - 2.0 * lsp[k] * f[j - 1]
        k += 2


def lsp_to_a(lsp):
    """10 LSPs (cosine domain) -> 11 A(z) coefficients a[0..10], a[0]=1."""
    f1 = [0.0] * 6
    f2 = [0.0] * 6
    lsp_poly(list(lsp), f1)  # even roots: lsp[0,2,4,6,8]
    lsp_poly(list(lsp)[1:], f2)  # odd roots:  lsp[1,3,5,7,9]
    for i in range(5, 0, -1):
        f1[i] += f1[i - 1]
        f2[i] -= f2[i - 1]
    a = [0.0] * 11
    a[0] = 1.0
    for i in range(1, 6):
        j = 11 - i
        a[i] = 0.5 * (f1[i] + f2[i])
        a[j] = 0.5 * (f1[i] - f2[i])
    return a


def _eval_poly(coeffs, zinv):
    """Horner evaluation of sum coeffs[k] * zinv^k (zinv = e^{-jw})."""
    val = coeffs[-1]
    for c in coeffs[-2::-1]:
        val = val * zinv + c
    return val


def spectral_distortion(lsp, qlsp):
    """Spectral distortion between the unquantized and quantized
    synthesis filters 1/A(z): SD = sqrt(mean_k (10 log10 |Aq|^2/|A|^2)^2)
    over a frequency grid [0, pi). Returns dB (per-frame scalar)."""
    a = np.asarray(lsp_to_a(lsp), dtype=np.float64)
    aq = np.asarray(lsp_to_a(qlsp), dtype=np.float64)
    w = np.linspace(0.0, np.pi, NFREQ, endpoint=False)
    zinv = np.exp(-1j * w)
    A = _eval_poly(a, zinv)
    Aq = _eval_poly(aq, zinv)
    ratio = (np.abs(Aq) ** 2 + 1e-12) / (np.abs(A) ** 2 + 1e-12)
    d = 10.0 * np.log10(ratio)
    return float(np.sqrt(np.mean(d * d)))


# ------------------------------------------------------------------
def main():
    if len(sys.argv) < 2:
        print("usage: %s lsp_debug.txt [more.txt ...]" % sys.argv[0], file=sys.stderr)
        sys.exit(2)

    U, Q, IND, FB = [], [], [], []
    n = 0
    for path in sys.argv[1:]:
        with open(path) as fh:
            for line in fh:
                if not line.startswith("LSP"):
                    continue
                toks = line.split()
                if len(toks) < 25:
                    continue
                vals = [float(t) for t in toks[1:21]]
                U.append(vals[:10])
                Q.append(vals[10:20])
                IND.append([int(t) for t in toks[21:24]])
                FB.append(int(toks[24]))
                n += 1

    if n == 0:
        print("RESULT: FAIL — no LSP debug lines found", file=sys.stderr)
        sys.exit(1)

    U = np.asarray(U, dtype=np.float64)
    Q = np.asarray(Q, dtype=np.float64)
    IND = np.asarray(IND, dtype=np.int64)
    FB = np.asarray(FB, dtype=np.int64)

    nan_u = int(np.isnan(U).sum()) + int(np.isnan(Q).sum())

    print("LSP validation: %d frames" % n)
    print("NaN count: %d" % nan_u)
    print(
        "fallbacks (ordering failure -> prev frame): %d (%.2f%%)"
        % (int(FB.sum()), 100.0 * FB.mean())
    )

    ok = True
    print("\nper-group quantization error:")
    for gi, (s, e, size) in enumerate(GROUPS):
        err = U[:, s:e] - Q[:, s:e]
        rms = float(np.sqrt(np.mean(err * err)))
        # frequency-domain error (Hz): w = acos(q), f = FS*w/(2*pi)
        f_u = FS / (2 * np.pi) * np.arccos(np.clip(U[:, s:e], -1.0, 1.0))
        f_q = FS / (2 * np.pi) * np.arccos(np.clip(Q[:, s:e], -1.0, 1.0))
        rms_hz = float(np.sqrt(np.mean((f_u - f_q) ** 2)))
        usage = int(len(np.unique(IND[:, gi])))
        print(
            "  cb%d: cos RMS %.4f   Hz RMS %6.2f   usage %d/%d"
            % (gi + 1, rms, rms_hz, usage, size)
        )
        if usage < 0.7 * size:
            ok = False

    # spectral distortion
    sd = np.array([spectral_distortion(u, q) for u, q in zip(U, Q)])
    print("\nspectral distortion:")
    print(
        "  mean %.3f dB   max %.3f dB   >1dB: %d (%.2f%%)"
        % (sd.mean(), sd.max(), int((sd > 1.0).sum()), 100.0 * (sd > 1.0).mean())
    )
    if sd.mean() > 2.0:
        ok = False

    if nan_u > 0:
        ok = False

    print()
    if ok:
        print("RESULT: OK")
        sys.exit(0)
    else:
        print("RESULT: FAIL — see metrics above")
        sys.exit(1)


if __name__ == "__main__":
    main()
