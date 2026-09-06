"""Conservative same-location colour comparison; not registration or causal attribution.

Model streams must be sRGB proxy encoded, before/after linear, as verified in logs.
RGB channel fractions are exposure-scale invariant, not a perceptual hue metric.
"""
import argparse
import importlib.util
import json
import pathlib

import numpy as np

spec = importlib.util.spec_from_file_location(
    "frequency", pathlib.Path(__file__).with_name("analyze-capture-frequency.py"))
frequency = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frequency)


def linearize(rgb):
    v = np.clip(rgb, 0, 1)
    return np.where(v < 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def fractions(rgb):
    return rgb / np.maximum(rgb.sum(axis=-1, keepdims=True), 1e-12)


def block_mean(rgb, block=8):
    h, w, c = rgb.shape
    if h % block or w % block:
        raise ValueError("Dimensions must be divisible by block size")
    return rgb.reshape(h // block, block, w // block, block, c).mean(axis=(1, 3))


def load(directory):
    # Fail closed: this analysis currently supports only the verified 4K four-stream contract.
    manifest = (directory / "manifest.txt").read_text()
    result = {}
    for stream in ("before", "after", "model_input", "model_output"):
        if f"{stream} width 3840 height 2160 format 26 rowPitch 15360" not in manifest:
            raise ValueError("Unsupported capture contract")
        paths = sorted(directory.glob(f"{stream}_*.raw"))
        if len(paths) != 8:
            raise ValueError("Expected eight frames per stream")
        frames = []
        for path in paths:
            rgb = frequency.read_rgb(path, 3840, 2160, 15360)
            if stream.startswith("model_"):
                rgb = linearize(rgb)
            frames.append(block_mean(rgb))
        result[stream] = np.mean(frames, axis=0)
    return result


def distance(a, b):
    return np.linalg.norm(a - b, axis=-1)


def compare(a, b, tolerance):
    fa = {k: fractions(v) for k, v in a.items()}
    fb = {k: fractions(v) for k, v in b.items()}
    ya, yb = frequency.luminance(a["before"]), frequency.luminance(b["before"])
    mask = ((ya > np.quantile(ya, 0.25)) & (yb > np.quantile(yb, 0.25))
            & (np.abs(np.log(np.maximum(ya, 1e-12) / np.maximum(yb, 1e-12))) < 0.05)
            & (distance(fa["before"], fb["before"]) < tolerance)
            & (distance(fa["model_input"], fb["model_input"]) < tolerance))
    result = {"input_fraction_tolerance": tolerance, "eligible_fraction": float(mask.mean()),
              "eligible_blocks": int(mask.sum())}
    def rms(value):
        return float(np.sqrt(np.mean(np.sum(value[mask] ** 2, axis=-1)))) if mask.any() else None
    result["cross_ratio_fraction_rms"] = {k: rms(fa[k] - fb[k]) for k in fa}
    result["difference_of_colour_edits_rms"] = {
        "model": rms((fa["model_output"] - fa["model_input"])
                     - (fb["model_output"] - fb["model_input"])),
        "final": rms((fa["after"] - fa["before"]) - (fb["after"] - fb["before"]))}
    result["within_capture_model_to_final_fraction_rms"] = {
        "first": rms(fa["after"] - fa["model_output"]),
        "second": rms(fb["after"] - fb["model_output"])}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=pathlib.Path)
    parser.add_argument("second", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path, required=True)
    args = parser.parse_args()
    a, b = load(args.first), load(args.second)
    result = {"first": str(args.first), "second": str(args.second),
              "scope": "Eight-frame average, 8x8 blocks, no registration; not a face ROI or perceptual score",
              "domain": "sRGB-decoded model streams; linear before/after; normalized RGB fractions",
              "comparisons": [compare(a, b, t) for t in (0.005, 0.01, 0.02)]}
    args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
