#!/usr/bin/env python3
"""Compare PNG screenshots by mean luminance and per-channel mean."""
import sys
import shutil
from dataclasses import dataclass
from pathlib import Path
from PIL import Image, ImageStat


@dataclass(frozen=True)
class ImageMetrics:
    path: Path
    size: tuple[int, int]
    luminance: float
    r: float
    g: float
    b: float


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


def median(values: list[float]) -> float:
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2 == 1:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) * 0.5


def image_metrics(path: Path, target_size: tuple[int, int] | None = None) -> ImageMetrics:
    try:
        img = Image.open(path).convert("RGB")
    except Exception as e:
        raise RuntimeError(f"failed to open image ({e})") from e

    if target_size is not None and img.size != target_size:
        img = img.resize(target_size, Image.LANCZOS)

    img_c = crop_central_60(img)
    lum = luminance_mean(img_c)
    r, g, b = channel_means(img_c)
    return ImageMetrics(path=path, size=img.size, luminance=lum, r=r, g=g, b=b)


def require_files(paths: list[Path]) -> bool:
    for p in paths:
        if not p.is_file():
            print(f"ERROR: {p} not found", file=sys.stderr)
            return False
    return True


def usage() -> None:
    print("usage: diff-screenshots.py BASELINE [BASELINE ...] TEST", file=sys.stderr)
    print("       diff-screenshots.py --select-median DEST BASELINE [BASELINE ...]", file=sys.stderr)


def select_median_baseline(dest_path: Path, base_paths: list[Path]) -> int:
    if not require_files(base_paths):
        return 2

    try:
        first = image_metrics(base_paths[0])
        bases = [first]
        bases.extend(image_metrics(p, first.size) for p in base_paths[1:])
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    for m in bases:
        if m.luminance < 5.0:
            print(f"ERROR: baseline appears blank/black: {m.path}", file=sys.stderr)
            return 2

    selected = sorted(bases, key=lambda m: (m.luminance, str(m.path)))[len(bases) // 2]
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(selected.path, dest_path)
    print(
        f"Selected median baseline: {dest_path} from {selected.path} "
        f"baseline={selected.luminance:.1f} samples={len(bases)}"
    )
    return 0


def compare_images(base_paths: list[Path], test_path: Path) -> int:
    if not require_files([*base_paths, test_path]):
        return 2

    try:
        first = image_metrics(base_paths[0])
        bases = [first]
        bases.extend(image_metrics(p, first.size) for p in base_paths[1:])
        test = image_metrics(test_path, first.size)
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    base_l = median([m.luminance for m in bases])
    test_l = test.luminance
    for m in bases:
        if m.luminance < 5.0:
            print(f"ERROR: baseline appears blank/black: {m.path}", file=sys.stderr)
            return 2
    if test_l < 5.0:
        print("ERROR: test appears blank/black", file=sys.stderr)
        return 2
    br = median([m.r for m in bases])
    bg = median([m.g for m in bases])
    bb = median([m.b for m in bases])
    tr, tg, tb = test.r, test.g, test.b
    dL = pct_delta(test_l, base_l)
    dR = pct_delta(tr, br)
    dG = pct_delta(tg, bg)
    dB = pct_delta(tb, bb)
    within = abs(dL) <= 5.0 and abs(dR) <= 10.0 and abs(dG) <= 10.0 and abs(dB) <= 10.0
    label = "PASS" if within else "FAIL (luminance regression)"
    sample_note = f" samples={len(bases)}" if len(bases) > 1 else ""
    print(
        f"VERDICT: {label}  baseline={base_l:.1f} test={test_l:.1f}{sample_note} "
        f"dL={dL:+.1f}% dR={dR:+.1f}% dG={dG:+.1f}% dB={dB:+.1f}%"
    )
    return 0 if within else 1


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "--select-median":
        if len(args) < 3:
            usage()
            return 2
        return select_median_baseline(Path(args[1]), [Path(p) for p in args[2:]])
    if len(args) < 2:
        usage()
        return 2
    return compare_images([Path(p) for p in args[:-1]], Path(args[-1]))


if __name__ == "__main__":
    sys.exit(main())
