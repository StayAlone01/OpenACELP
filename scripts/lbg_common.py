#!/usr/bin/env python3
# ------------------------------------------------------------------
# lbg_common.py - shared, vectorised Linde-Buzo-Gray (LBG) machinery
# used by the gain codebook generator (2-D, scripts/gain_codebook_generator.py)
# and the LSP codebook generator (3/3/4-D, scripts/lsp_codebook_generator.py).
# numpy only.
#
# The Lloyd iterations are processed in batches so no (chunk x K x dim)
# tensor is ever materialised: tens of millions of points fit in ~1 GB of
# RAM (measured: 5 M 2-D points -> 838 MB peak; ~48 M -> ~1 GB).
#
# Convergence uses the *relative distortion decrease* (1e-4), which is
# scale-invariant and always terminates, even with degenerate or empty
# cells (an absolute centroid-movement threshold never converges with
# float32 assignments). Empty cells keep their old center fixed so the
# iteration cannot stall in a perpetual jitter.
#
# Requires: numpy (python3 -m pip install --user numpy).
# ------------------------------------------------------------------

import sys

import numpy as np

READ_CHUNK = 64 << 20  # 64 MB of text per parse chunk
LLOYD_CHUNK = 1_000_000  # points per assignment batch (~256 MB peak)
MAX_ITER = 200  # Lloyd iterations per split stage


def _parse_tokens(buf, dim):
    """Parse a chunk of 'x y ...' text (no partial token) to a (N, dim)
    float32 array. Drops the trailing incomplete vector, if any."""
    try:
        a = np.fromstring(buf, sep=" ", dtype=np.float32)
    except (TypeError, ValueError):
        # np.fromstring is deprecated; fall back for newer numpy
        a = np.array(buf.split(), dtype=np.float32)
    m = a.size - (a.size % dim)
    if m == 0:
        return None
    return a[:m].reshape(-1, dim)


def read_features(paths, dim, clip_lo=None, clip_hi=None, max_points=None):
    """Read dim-dimensional feature vectors from one or more text files
    into a float32 (N, dim) array. Text is parsed in chunks (memory-safe)."""
    if isinstance(paths, str):
        paths = [paths]

    parts = []
    for path in paths:
        with open(path, "rb") as f:
            carry = b""
            while True:
                chunk = f.read(READ_CHUNK)
                if not chunk:
                    break
                data = carry + chunk
                nl = data.rfind(b"\n")
                if nl < 0:  # no complete line yet
                    carry = data
                    continue
                carry = data[nl + 1 :]
                a = _parse_tokens(data[: nl + 1], dim)
                if a is not None:
                    parts.append(a)
            if carry.strip():
                a = _parse_tokens(carry, dim)
                if a is not None:
                    parts.append(a)

    if not parts:
        return np.empty((0, dim), dtype=np.float32)

    X = np.concatenate(parts)

    # Drop non-finite rows (defensive; should not happen)
    X = X[np.isfinite(X).all(axis=1)]

    if clip_lo is not None:
        X = np.maximum(X, clip_lo)
    if clip_hi is not None:
        X = np.minimum(X, clip_hi)

    if max_points is not None and len(X) > max_points:
        idx = np.random.RandomState(0).choice(len(X), max_points, replace=False)
        X = X[idx]

    return X


def lloyd(X, centers):
    """One vectorised assignment + centroid-update pass, processed in
    batches. Returns (updated centers (K, dim), total squared distortion)."""
    K = len(centers)
    dim = X.shape[1]
    cf = centers.astype(np.float32)  # keep the distance matrix in float32
    cf2 = (cf * cf).sum(axis=1)  # (K,)

    sums = np.zeros((K, dim), dtype=np.float64)
    counts = np.zeros(K, dtype=np.int64)
    distortion = 0.0

    for i in range(0, len(X), LLOYD_CHUNK):
        xb = X[i : i + LLOYD_CHUNK]
        # squared distance: |x|^2 + |c|^2 - 2 x.c  ->  (batch, K) float32
        d = ((xb * xb).sum(axis=1))[:, None] + cf2[None, :] - 2.0 * (xb @ cf.T)
        idx = d.argmin(axis=1)
        distortion += float(d[np.arange(len(xb)), idx].sum())

        counts += np.bincount(idx, minlength=K)
        for j in range(dim):
            sums[:, j] += np.bincount(idx, weights=xb[:, j], minlength=K)

    new = np.empty_like(centers)
    for k in range(K):
        if counts[k] > 0:
            new[k] = sums[k] / counts[k]
        else:
            # Empty cell: re-seed from a data point (deterministic, stable
            # across iterations). This keeps the center inside the data hull.
            # The previous "keep the old center fixed" behaviour let
            # split-artifact centers (mean +- eps) drift out of range over
            # the split stages — harmless for the gain codebook, but a
            # 2-D/4-D LSP codebook must stay in the valid cosine range.
            new[k] = X[(k * 2654435761) % len(X)]
    return new, distortion


def lbg(X, n, eps=0.5):
    """Binary-splitting LBG (numpy). n must be a power of two. eps is the
    initial split offset (fraction of the data spread would also work;
    a small eps suits compact data like the cosine-domain LSPs)."""
    centers = X.mean(axis=0, keepdims=True)

    while len(centers) < n:
        # Split every center into two
        centers = np.concatenate([centers + eps, centers - eps], axis=0)
        # Refine until the distortion stops decreasing (scale-invariant, so
        # it always converges quickly even with degenerate/empty cells)
        prev_d = float("inf")
        for it in range(MAX_ITER):
            centers, d = lloyd(X, centers)
            if prev_d != float("inf") and (prev_d - d) <= 1e-4 * prev_d:
                break
            prev_d = d
            if it % 50 == 0:
                print(
                    "  stage %d, iter %d, distortion %.3e" % (len(centers), it, d),
                    file=sys.stderr,
                )

    return centers[:n]
