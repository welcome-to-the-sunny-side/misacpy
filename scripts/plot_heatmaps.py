#!/usr/bin/env python3
"""Render dis x n heatmaps from bench_grid CSV output.

Produces three figures:
  - speedup            (naive_min / cyccpy_min), diverging log color centered at 1.0
  - cyccpy throughput  (GB/s), sequential color
  - naive  throughput  (GB/s), sequential color  -- shares cyccpy's color scale

Usage:
  python3 scripts/plot_heatmaps.py [grid.csv] [--outdir tmp] [--machine "i7-14650HX"]
"""

import argparse
import csv
import math
import os

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import Normalize


def human_bytes(n):
    n = int(n)
    if n >= 1 << 20:
        return f"{n >> 20} MB"
    if n >= 1 << 10:
        return f"{n >> 10} KB"
    return f"{n} B"


def load(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            rows.append({k: float(v) for k, v in r.items()})
    dis_vals = sorted({int(r["dis"]) for r in rows})
    n_vals = sorted({int(r["n"]) for r in rows})
    di = {d: j for j, d in enumerate(dis_vals)}
    ni = {n: i for i, n in enumerate(n_vals)}

    def grid(key):
        a = np.full((len(n_vals), len(dis_vals)), np.nan)
        for r in rows:
            a[ni[int(r["n"])], di[int(r["dis"])]] = r[key]
        return a

    return dis_vals, n_vals, grid


def text_color(rgba):
    lum = 0.299 * rgba[0] + 0.587 * rgba[1] + 0.114 * rgba[2]
    return "black" if lum > 0.55 else "white"


def boundary(vals, threshold):
    """Index position (between cells) where vals crosses >= threshold, or None."""
    for k, v in enumerate(vals):
        if v >= threshold:
            return k - 0.5
    return None


def draw(ax, data, dis_vals, n_vals, cmap, norm, annot_fmt, cbar_label, title):
    im = ax.imshow(data, origin="lower", aspect="auto", cmap=cmap, norm=norm)
    cmap_obj = matplotlib.colormaps[cmap].copy()
    cmap_obj.set_bad("0.85")

    ax.set_xticks(range(len(dis_vals)))
    ax.set_xticklabels(dis_vals, rotation=45, ha="right", fontsize=8)
    ax.set_yticks(range(len(n_vals)))
    ax.set_yticklabels([human_bytes(n) for n in n_vals], fontsize=8)
    ax.set_xlabel("dis (bytes)")
    ax.set_ylabel("n (bytes)")
    ax.set_title(title, fontsize=11, pad=10)

    for i in range(len(n_vals)):
        for j in range(len(dis_vals)):
            v = data[i, j]
            if np.isnan(v):
                continue
            rgba = cmap_obj(norm(v))
            ax.text(j, i, annot_fmt(v), ha="center", va="center",
                    fontsize=6.5, color=text_color(rgba))

    # Microarch regime guides.
    xv = boundary(dis_vals, 32)          # AVX-256 vectorization threshold
    if xv is not None:
        ax.axvline(xv, color="k", lw=1.0, ls="--", alpha=0.5)
    slf_lo = boundary(dis_vals, 33)
    slf_hi = boundary(dis_vals, 64)
    if slf_lo is not None and slf_hi is not None:
        ax.axvspan(slf_lo, slf_hi, color="k", alpha=0.06)
    for thresh, lbl in [(1 << 22, "L2"), (1 << 25, "L3 / NT")]:
        yb = boundary(n_vals, thresh)
        if yb is not None:
            ax.axhline(yb, color="k", lw=1.0, ls=":", alpha=0.5)
            ax.text(len(dis_vals) - 0.4, yb, " " + lbl, fontsize=7,
                    va="center", ha="left", alpha=0.6)

    cb = ax.figure.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
    cb.set_label(cbar_label, fontsize=9)
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="?", default="tmp/grid.csv")
    ap.add_argument("--outdir", default="tmp")
    args = ap.parse_args()

    dis_vals, n_vals, grid = load(args.csv)
    speedup = grid("speedup")
    cyc = grid("cyc_gbps")
    naive = grid("naive_gbps")

    # 1) Speedup: diverging color on log2(speedup), centered at 1.0 (=0 in log space).
    L = np.log2(speedup)
    lim = np.nanmax(np.abs(L))
    fig, ax = plt.subplots(figsize=(10, 6))
    draw(ax, L, dis_vals, n_vals,
         cmap="RdBu", norm=Normalize(-lim, lim),
         annot_fmt=lambda v: f"{2 ** v:.2f}",
         cbar_label="speedup  (naive / cyccpy), log color",
         title="cyccpy speedup vs naive")
    fig.tight_layout()
    for ext in ("png", "svg"):
        fig.savefig(os.path.join(args.outdir, f"heat_speedup.{ext}"), dpi=150, bbox_inches="tight")

    # 2) + 3) Throughput: shared sequential scale across both functions.
    vmax = np.nanmax([np.nanmax(cyc), np.nanmax(naive)])
    tnorm = Normalize(0, vmax)
    for data, name, title in [(cyc, "cyccpy", "cyccpy throughput"),
                              (naive, "naive", "naive throughput")]:
        fig, ax = plt.subplots(figsize=(10, 6))
        draw(ax, data, dis_vals, n_vals,
             cmap="viridis", norm=tnorm,
             annot_fmt=lambda v: f"{v:.0f}",
             cbar_label="throughput (GB/s)",
             title=title)
        fig.tight_layout()
        for ext in ("png", "svg"):
            fig.savefig(os.path.join(args.outdir, f"heat_{name}_gbps.{ext}"), dpi=150, bbox_inches="tight")

    print(f"wrote heat_speedup, heat_cyccpy_gbps, heat_naive_gbps (png+svg) to {args.outdir}/")
    print(f"grid: {len(dis_vals)} dis x {len(n_vals)} n = {len(dis_vals) * len(n_vals)} cells")


if __name__ == "__main__":
    main()
