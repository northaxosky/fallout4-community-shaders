#!/usr/bin/env python3
"""Compare two PNG screenshots by mean luminance and per-channel mean."""
import sys
from pathlib import Path
from PIL import Image, ImageStat


def crop_central_60(img: Image.Image) -> Image.Image:
    w, h = img.size
    cw, ch = int(w * 0.6), int(h * 0.6)
    left, top = (w - cw) // 2, (h - ch) // 2
    return img.crop((left, top, left + cw, top + ch))


def channel_means(img_rgb: Image.Image) -> tuple[float, float, float]:
    r, g, b = ImageStat.Stat(img_rgb).mean[:3]
    return r, g, b


def luminance_mean(img_rgb: Image.Image) -> float:
    return ImageStat.Stat(img_rgb.convert("L")).mean[0]


def pct_delta(test: float, base: float) -> float:
    return (test - base) / base * 100.0 if base else 0.0


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: diff-screenshots.py BASELINE TEST", file=sys.stderr)
        return 2
    base_path, test_path = Path(sys.argv[1]), Path(sys.argv[2])
    for p in (base_path, test_path):
        if not p.is_file():
            print(f"ERROR: {p} not found", file=sys.stderr)
            return 2
    try:
        base = Image.open(base_path).convert("RGB")
        test = Image.open(test_path).convert("RGB")
    except Exception as e:
        print(f"ERROR: failed to open image ({e})", file=sys.stderr)
        return 2
    if test.size != base.size:
        test = test.resize(base.size, Image.LANCZOS)
    base_c = crop_central_60(base)
    test_c = crop_central_60(test)
    base_l = luminance_mean(base_c)
    test_l = luminance_mean(test_c)
    if base_l < 5.0:
        print("ERROR: baseline appears blank/black", file=sys.stderr)
        return 2
    if test_l < 5.0:
        print("ERROR: test appears blank/black", file=sys.stderr)
        return 2
    br, bg, bb = channel_means(base_c)
    tr, tg, tb = channel_means(test_c)
    dL = pct_delta(test_l, base_l)
    dR = pct_delta(tr, br)
    dG = pct_delta(tg, bg)
    dB = pct_delta(tb, bb)
    within = abs(dL) <= 5.0 and abs(dR) <= 10.0 and abs(dG) <= 10.0 and abs(dB) <= 10.0
    label = "PASS" if within else "FAIL (luminance regression)"
    print(
        f"VERDICT: {label}  baseline={base_l:.1f} test={test_l:.1f} "
        f"dL={dL:+.1f}% dR={dR:+.1f}% dG={dG:+.1f}% dB={dB:+.1f}%"
    )
    return 0 if within else 1


if __name__ == "__main__":
    sys.exit(main())
