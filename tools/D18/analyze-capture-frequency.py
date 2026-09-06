#!/usr/bin/env python3
"""Measure spatial and temporal luminance bands in matched D18 raw captures."""
from __future__ import annotations

import argparse
import json
import pathlib
import re

import numpy as np
from scipy.ndimage import gaussian_filter


def decode_ufloat(bits: np.ndarray, mantissa_bits: int) -> np.ndarray:
    exponent = bits >> mantissa_bits
    mantissa = bits & ((1 << mantissa_bits) - 1)
    normal = np.ldexp(1.0 + mantissa.astype(np.float32) / (1 << mantissa_bits),
                      exponent.astype(np.int32) - 15)
    subnormal = np.ldexp(mantissa.astype(np.float32), 1 - 15 - mantissa_bits)
    result = np.where(exponent == 0, subnormal, normal)
    return np.where(exponent == 31, np.nan, result).astype(np.float32)


def read_rgb(path: pathlib.Path, width: int, height: int, row_pitch: int) -> np.ndarray:
    packed_rows = np.fromfile(path, dtype="<u4").reshape(height, row_pitch // 4)
    packed = packed_rows[:, :width]
    red = decode_ufloat(packed & 0x7FF, 6)
    green = decode_ufloat((packed >> 11) & 0x7FF, 6)
    blue = decode_ufloat((packed >> 22) & 0x3FF, 5)
    rgb = np.stack((red, green, blue), axis=-1)
    return np.nan_to_num(rgb, nan=0.0, posinf=0.0, neginf=0.0)


def luminance(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def colour_stats(rgb: np.ndarray) -> dict[str, float]:
    luma = luminance(rgb)
    chroma = rgb - luma[..., None]
    return {
        "chroma_rms": float(np.sqrt(np.mean(np.sum(chroma * chroma, axis=-1)))),
        "red_mean": float(np.mean(rgb[..., 0])),
        "green_mean": float(np.mean(rgb[..., 1])),
        "blue_mean": float(np.mean(rgb[..., 2])),
    }


def tonal_colour_transfer(source_rgb: np.ndarray, target_rgb: np.ndarray) -> dict[str, float]:
    """Measure chroma transfer separately in shadow, midtone and highlight populations."""
    source_luma = luminance(source_rgb)
    target_luma = luminance(target_rgb)
    source_chroma = source_rgb - source_luma[..., None]
    target_chroma = target_rgb - target_luma[..., None]
    edit_rgb = target_rgb - source_rgb
    edit_luma = target_luma - source_luma
    edit_chroma = edit_rgb - edit_luma[..., None]
    low, high = np.quantile(source_luma, (0.25, 0.75))
    masks = {
        "shadow": source_luma <= low,
        "midtone": (source_luma > low) & (source_luma < high),
        "highlight": source_luma >= high,
    }
    result: dict[str, float] = {"shadow_upper_luma": float(low),
                                "highlight_lower_luma": float(high)}
    for name, mask in masks.items():
        source_rms = float(np.sqrt(np.mean(np.sum(source_chroma[mask] ** 2, axis=-1))))
        target_rms = float(np.sqrt(np.mean(np.sum(target_chroma[mask] ** 2, axis=-1))))
        result[f"{name}_source_chroma_rms"] = source_rms
        result[f"{name}_target_chroma_rms"] = target_rms
        result[f"{name}_target_over_source_chroma"] = target_rms / source_rms if source_rms else 0.0
        result[f"{name}_edit_chroma_rms"] = float(
            np.sqrt(np.mean(np.sum(edit_chroma[mask] ** 2, axis=-1))))
    return result


def bands(image: np.ndarray) -> dict[str, float]:
    fine = gaussian_filter(image, 1.0, mode="nearest")
    coarse = gaussian_filter(image, 4.0, mode="nearest")
    high = image - fine
    middle = fine - coarse
    return {
        "mean": float(np.mean(image)),
        "contrast_rms": float(np.std(image)),
        "high_rms": float(np.sqrt(np.mean(high * high))),
        "middle_rms": float(np.sqrt(np.mean(middle * middle))),
        "low_rms": float(np.std(coarse)),
    }


def downsample_2x_area(image: np.ndarray) -> np.ndarray:
    """Reduce an even-sized image by 2 with an aligned 2x2 area filter.

    Applying the identical linear filter to model input and output gives a
    conservative measurement in the 1920x1080 representable domain.  It does
    not undo or separately identify any reconstruction already performed
    inside the Runtime-visible 4K boundary.
    """
    height, width = image.shape[:2]
    if height % 2 or width % 2:
        raise ValueError(f"2x area downsample requires even dimensions, got {width}x{height}")
    return 0.25 * (image[0::2, 0::2] + image[0::2, 1::2] +
                   image[1::2, 0::2] + image[1::2, 1::2])


def reconstruct_2x_bilinear(image: np.ndarray) -> np.ndarray:
    """Phase-correct 2x bilinear reconstruction from a pixel-centred network lattice.

    The Runtime-visible 4K model surfaces can contain a 2x2 footprint for every
    1080p network sample.  Reduce each footprint to one lattice value first, then
    reconstruct between lattice centres.  Sampling the expanded surface directly
    as an ordinary 4K texture instead blends only across the narrow block seams.
    """
    height, width = image.shape[:2]
    if height < 1 or width < 1:
        raise ValueError("2x bilinear reconstruction requires a non-empty image")

    out_width, out_height = width * 2, height * 2
    x = (np.arange(out_width, dtype=np.float32) + 0.5) * 0.5 - 0.5
    x0_unclamped = np.floor(x).astype(np.intp)
    fx = x - x0_unclamped
    x0 = np.clip(x0_unclamped, 0, width - 1)
    x1 = np.clip(x0_unclamped + 1, 0, width - 1)
    horizontal = image[:, x0] * (1.0 - fx) + image[:, x1] * fx

    y = (np.arange(out_height, dtype=np.float32) + 0.5) * 0.5 - 0.5
    y0_unclamped = np.floor(y).astype(np.intp)
    fy = y - y0_unclamped
    y0 = np.clip(y0_unclamped, 0, height - 1)
    y1 = np.clip(y0_unclamped + 1, 0, height - 1)
    return horizontal[y0] * (1.0 - fy[:, None]) + horizontal[y1] * fy[:, None]


def parse_manifest(directory: pathlib.Path) -> tuple[int, int, int, int]:
    text = (directory / "manifest.txt").read_text(encoding="utf-8")
    frames = int(re.search(r"frames (\d+)", text).group(1))
    match = re.search(r"before width (\d+) height (\d+) format (\d+) rowPitch (\d+)", text)
    width, height, fmt, row_pitch = map(int, match.groups())
    if fmt != 26:
        raise ValueError(f"expected DXGI_FORMAT_R11G11B10_FLOAT (26), got {fmt}")
    return frames, width, height, row_pitch


def parse_stream(text: str, name: str) -> tuple[int, int, int]:
    match = re.search(rf"{re.escape(name)} width (\d+) height (\d+) format (\d+) rowPitch (\d+)", text)
    if not match:
        raise ValueError(f"manifest has no {name} stream")
    width, height, fmt, row_pitch = map(int, match.groups())
    if fmt != 26:
        raise ValueError(f"expected {name} DXGI_FORMAT_R11G11B10_FLOAT (26), got {fmt}")
    return width, height, row_pitch


def average(rows: list[dict[str, float]]) -> dict[str, float]:
    return {key: float(np.mean([row[key] for row in rows])) for key in rows[0]}


def analyze(directory: pathlib.Path) -> dict:
    frame_count, width, height, row_pitch = parse_manifest(directory)
    before_rows, after_rows, edit_rows = [], [], []
    before_colour, after_colour, edit_colour = [], [], []
    tonal_colour = []
    temporal_before, temporal_after = [], []
    previous_before = previous_after = None
    for index in range(frame_count):
        before_rgb = read_rgb(directory / f"before_{index:02d}.raw", width, height, row_pitch)
        after_rgb = read_rgb(directory / f"after_{index:02d}.raw", width, height, row_pitch)
        before = luminance(before_rgb)
        after = luminance(after_rgb)
        before_rows.append(bands(before))
        after_rows.append(bands(after))
        edit_rows.append(bands(after - before))
        before_colour.append(colour_stats(before_rgb))
        after_colour.append(colour_stats(after_rgb))
        edit_colour.append(colour_stats(after_rgb - before_rgb))
        tonal_colour.append(tonal_colour_transfer(before_rgb, after_rgb))
        if previous_before is not None:
            temporal_before.append(bands(before - previous_before))
            temporal_after.append(bands(after - previous_after))
        previous_before, previous_after = before, after
    before_avg, after_avg = average(before_rows), average(after_rows)
    mean_ratio = after_avg["mean"] / before_avg["mean"] if before_avg["mean"] else None
    result = {
        "directory": str(directory), "frames": frame_count,
        "width": width, "height": height,
        "before": before_avg, "after": after_avg, "edit": average(edit_rows),
        "before_colour": average(before_colour),
        "after_colour": average(after_colour),
        "edit_colour": average(edit_colour),
        "tonal_colour": average(tonal_colour),
        "after_over_before": {
            key: after_avg[key] / before_avg[key] if before_avg[key] else None
            for key in ("contrast_rms", "high_rms", "middle_rms", "low_rms")
        },
        "domain_check": {
            "after_over_before_mean": mean_ratio,
            "compatible": mean_ratio is not None and 0.25 <= mean_ratio <= 4.0,
        },
    }
    if temporal_before:
        tb, ta = average(temporal_before), average(temporal_after)
        result["temporal_before"] = tb
        result["temporal_after"] = ta
        result["temporal_after_over_before"] = {
            key: ta[key] / tb[key] if tb[key] else None
            for key in ("contrast_rms", "high_rms", "middle_rms", "low_rms")
        }
    manifest = (directory / "manifest.txt").read_text(encoding="utf-8")
    if (directory / "model_input_00.raw").exists() and "model_input width" in manifest:
        model_width, model_height, model_pitch = parse_stream(manifest, "model_input")
        output_width, output_height, output_pitch = parse_stream(manifest, "model_output")
        if (model_width, model_height) != (output_width, output_height):
            raise ValueError("model input/output dimensions differ")
        model_inputs, model_outputs, model_edits = [], [], []
        model_inputs_2x, model_outputs_2x, model_edits_2x = [], [], []
        model_inputs_reconstructed, model_outputs_reconstructed = [], []
        model_input_colour, model_output_colour, model_edit_colour = [], [], []
        model_tonal_colour = []
        for index in range(frame_count):
            model_input = read_rgb(directory / f"model_input_{index:02d}.raw",
                                   model_width, model_height, model_pitch)
            model_output = read_rgb(directory / f"model_output_{index:02d}.raw",
                                    output_width, output_height, output_pitch)
            input_luma = luminance(model_input)
            output_luma = luminance(model_output)
            model_inputs.append(bands(input_luma))
            model_outputs.append(bands(output_luma))
            model_edits.append(bands(output_luma - input_luma))
            if model_width % 2 == 0 and model_height % 2 == 0:
                input_luma_2x = downsample_2x_area(input_luma)
                output_luma_2x = downsample_2x_area(output_luma)
                model_inputs_2x.append(bands(input_luma_2x))
                model_outputs_2x.append(bands(output_luma_2x))
                model_edits_2x.append(bands(output_luma_2x - input_luma_2x))
                reconstructed_input = reconstruct_2x_bilinear(input_luma_2x)
                reconstructed_output = reconstruct_2x_bilinear(output_luma_2x)
                model_inputs_reconstructed.append(bands(reconstructed_input))
                model_outputs_reconstructed.append(bands(reconstructed_output))
            model_input_colour.append(colour_stats(model_input))
            model_output_colour.append(colour_stats(model_output))
            model_edit_colour.append(colour_stats(model_output - model_input))
            model_tonal_colour.append(tonal_colour_transfer(model_input, model_output))
        mi, mo = average(model_inputs), average(model_outputs)
        result["model"] = {
            "width": model_width,
            "height": model_height,
            "input": mi,
            "output": mo,
            "edit": average(model_edits),
            "output_over_input": {
                key: mo[key] / mi[key] if mi[key] else None
                for key in ("mean", "contrast_rms", "high_rms", "middle_rms", "low_rms")
            },
            "input_colour": average(model_input_colour),
            "output_colour": average(model_output_colour),
            "edit_colour": average(model_edit_colour),
            "tonal_colour": average(model_tonal_colour),
        }
        if model_inputs_2x:
            mi2, mo2 = average(model_inputs_2x), average(model_outputs_2x)
            result["model"]["downsampled_2x"] = {
                "width": model_width // 2,
                "height": model_height // 2,
                "filter": "aligned 2x2 area, identical on input and output",
                "scope": "Runtime-visible boundary after any internal reconstruction",
                "input": mi2,
                "output": mo2,
                "edit": average(model_edits_2x),
                "output_over_input": {
                    key: mo2[key] / mi2[key] if mi2[key] else None
                    for key in ("mean", "contrast_rms", "high_rms", "middle_rms", "low_rms")
                },
            }
            mir, mor = average(model_inputs_reconstructed), average(model_outputs_reconstructed)
            result["model"]["phase_aligned_bilinear_2x"] = {
                "width": model_width,
                "height": model_height,
                "source": f"{model_width // 2}x{model_height // 2} aligned 2x2 area lattice",
                "filter": "pixel-centred bilinear reconstruction",
                "scope": "offline reconstruction candidate, not a claim about hidden Runtime tensors",
                "input": mir,
                "output": mor,
                "output_over_input": {
                    key: mor[key] / mir[key] if mir[key] else None
                    for key in ("mean", "contrast_rms", "high_rms", "middle_rms", "low_rms")
                },
            }
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("captures", nargs="+", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path)
    args = parser.parse_args()
    result = [analyze(path) for path in args.captures]
    encoded = json.dumps(result, indent=2) + "\n"
    if args.json:
        args.json.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
