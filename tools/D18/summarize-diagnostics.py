#!/usr/bin/env python3
"""Parse the fixed D18 diagnostics ring and emit deterministic JSON/Markdown summaries."""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import struct

HEADER = struct.Struct("<8sIIIIQqqq16s16s64s")
RECORD = struct.Struct("<8Q8I5f32s96si")
FLAG_SUBMISSION_SUBMITTED = 1 << 4
FLAG_SUBMISSION_COMPLETE = 1 << 5


def _text(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def read_ring(path: pathlib.Path) -> tuple[dict, list[dict]]:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("truncated header")
    values = HEADER.unpack_from(data)
    magic, schema, header_bytes, record_bytes, capacity, session, next_seq, dropped, trigger_ms, api, backend, game = values
    if not magic.startswith(b"D18DIAG") or schema != 1:
        raise ValueError("unsupported diagnostics schema")
    if header_bytes != HEADER.size or record_bytes != RECORD.size:
        raise ValueError(f"layout mismatch: header={header_bytes}, record={record_bytes}")
    if len(data) < header_bytes + record_bytes * capacity:
        raise ValueError("truncated ring")
    records = []
    for slot in range(capacity):
        v = RECORD.unpack_from(data, header_bytes + slot * record_bytes)
        if v[-1] != 1 or v[0] == 0:
            continue
        records.append({
            "sequence": v[0], "monotonic_ms": v[1], "frame": v[2], "feature_generation": v[3],
            "queue": v[4], "command_list": v[5], "fence_target": v[6], "fence_completed": v[7],
            "width": v[8], "height": v[9], "network_width": v[10], "network_height": v[11],
            "guide_width": v[12], "guide_height": v[13], "result": v[14], "flags": v[15],
            "ratio": v[16], "exposure": v[17], "white_point": v[18], "mv_scale_x": v[19],
            "mv_scale_y": v[20], "type": _text(v[21]), "reason": _text(v[22]),
        })
    records.sort(key=lambda r: r["sequence"])
    return ({"schema": schema, "session": session, "next_sequence": next_seq, "dropped": dropped,
             "last_trigger_ms": trigger_ms, "api": _text(api), "backend": _text(backend), "game": _text(game)}, records)


def summarize(header: dict, records: list[dict]) -> dict:
    abnormal = {"allocation_failed", "device_lost", "nr_failure_state", "nr_skip", "compose_skip", "capture_rejected", "capture_write_failed", "mp_failure", "mp_admission"}
    first = next((r for r in records if (r["type"] in abnormal and r["reason"] != "it is switched off") or
                  (r["type"] == "nr_outcome" and r["result"] != 1 and not r["reason"].startswith("summary_overflow")) or
                  (r["type"] in {"feature_create", "evaluate", "mp_create", "mp_evaluate"} and r["result"] != 1)), None)
    skip_counts = collections.Counter(r["reason"] or "unknown" for r in records if r["type"] == "nr_skip")
    gaps = []
    pending_recordings = []
    for r in records:
        submitted = bool(r["flags"] & FLAG_SUBMISSION_SUBMITTED)
        complete = bool(r["flags"] & FLAG_SUBMISSION_COMPLETE)
        if r["fence_target"] and submitted and not complete and r["fence_completed"] < r["fence_target"]:
            gaps.append({"sequence": r["sequence"], "queue": r["queue"],
                         "target": r["fence_target"], "completed": r["fence_completed"]})
        elif r["fence_target"] and not submitted:
            pending_recordings.append({"sequence": r["sequence"], "queue": r["queue"],
                                       "target": r["fence_target"]})
    evidence_gaps = []
    for r in records:
        if r["type"] in {"frame_contract", "evaluate"} and not r["queue"]:
            evidence_gaps.append({"sequence": r["sequence"], "missing": "queue"})
    ui = [r for r in records if r["type"] == "ui_scroll"]
    ui_summary = {
        "samples": len(ui),
        "routes": sorted({r["reason"] for r in ui}),
        "wheel_samples": sum(r["white_point"] != 0 for r in ui),
        "held_samples": sum(bool(r["flags"] & 4) for r in ui),
        "scrollbar_active_samples": sum(bool(r["flags"] & 64) for r in ui),
        "button_mismatch_samples": sum(bool(r["flags"] & 2) != bool(r["flags"] & 4) for r in ui),
        "observed_scroll_range": [min((r["ratio"] for r in ui), default=0), max((r["ratio"] for r in ui), default=0)],
        "gameplay_verdict": "not_inferred",
    }
    output_rects = [{"sequence": r["sequence"], "frame": r["frame"], "action": r["reason"],
                     "effective_output": [r["width"], r["height"]],
                     "reported_output": [r["network_width"], r["network_height"]],
                     "sr_render": [r["guide_width"], r["guide_height"]]}
                    for r in records if r["type"] == "sr_output_rect"]
    outcomes = []
    reset_names = {0: "none", 1: "first_use", 2: "source_changed", 3: "recording_gap",
                   4: "nr_disabled", 5: "source_released", 6: "renderer_reset", 7: "additional_pass_reset"}
    for r in records:
        if r["type"] != "nr_outcome": continue
        parts = r["reason"].split(";")
        fields = dict(p.split("=", 1) for p in parts[1:] if "=" in p)
        def number(key: str, default: int) -> int:
            try: return int(fields.get(key, default))
            except (ValueError, TypeError): return default
        flags = r["flags"]
        outcomes.append({"sequence": r["sequence"], "last_attempt": r["frame"],
            "count": max(1, number("n", 1)), "source_id": number("s", 0), "reason": parts[0],
            "requested": (flags >> 12) & 15, "ready": (flags >> 16) & 15,
            "evaluated": (flags >> 20) & 15, "composed": (flags >> 24) & 15,
            "reset_mask": (flags >> 4) & 15, "shared": bool(flags & 2), "requested_shared": bool(flags & 4),
            "reset_reason": reset_names.get(number("h", 0), "unknown"),
            "output": [r["width"], r["height"]],
            "evidence": "cpu_recorded_outcome_not_gpu_completion_or_displayed_frame"})
    known = [o for o in outcomes if o["reason"] != "summary_overflow"]
    distribution = collections.Counter()
    fallback_reasons = collections.Counter()
    reset_reasons = collections.Counter()
    for o in known:
        distribution[f"{o['requested']}->{o['composed']}"] += o["count"]
        if o["composed"] < o["requested"]: fallback_reasons[o["reason"]] += o["count"]
        if o["reset_mask"]: reset_reasons[o["reset_reason"]] += o["count"]
    outcome_summary = {"observed_attempts": sum(o["count"] for o in known),
        "sr_fallback_attempts": sum(o["count"] for o in known if o["composed"] == 0),
        "reduced_pass_attempts": sum(o["count"] for o in known if 0 < o["composed"] < o["requested"]),
        "signature_overflow_attempts": sum(o["count"] for o in outcomes if o["reason"] == "summary_overflow"),
        "requested_to_composed": dict(distribution), "fallback_reasons": dict(fallback_reasons),
        "reset_reasons": dict(reset_reasons),
        "ordering": "Summary aggregates signatures within bounded time windows; no per-frame ordering inferred",
        "coverage": "No outcomes means unobserved, not zero failures; sources are NGX handle ids, zero is unidentified"}
    memory = []
    memory_overflow = 0
    for r in records:
        if r["type"] != "nr_memory": continue
        parts = r["reason"].split(";")
        fields = dict(p.split("=", 1) for p in parts[1:] if "=" in p)
        if parts[0] == "overflow":
            try: memory_overflow += int(fields.get("n", "0"))
            except ValueError: pass
            continue
        def byte_value(key):
            try: return int(fields[key], 16)
            except (ValueError, KeyError): return None
        valid = bool(r["flags"] & 1)
        memory.append({"sequence": r["sequence"], "frame": r["frame"], "phase": parts[0],
            "pass": (r["flags"] >> 8) & 15, "generation": r["feature_generation"],
            "adapter_luid": r["queue"], "device_identity": r["command_list"],
            "query_valid": valid, "query_hresult": r["result"],
            "admitted": bool(r["flags"] & 2) if parts[0] in {"before", "admission", "scratch", "highres"} else None,
            "budget_bytes": byte_value("b") if valid else None,
            "usage_bytes": byte_value("u") if valid else None,
            "proposed_bytes": byte_value("p"), "reserve_bytes": byte_value("r"),
            "output": [r["width"], r["height"]], "network": [r["network_width"], r["network_height"]],
            "ratio": r["ratio"]})
    memory_deltas = []
    before = {}
    for sample in memory:
        key = (sample["device_identity"], sample["adapter_luid"], sample["generation"], sample["pass"],
               *sample["output"], *sample["network"])
        if sample["phase"] == "before": before[key] = sample
        elif sample["phase"] in {"created", "gpu_ready"} and key in before:
            start = before[key]
            delta = (sample["usage_bytes"] - start["usage_bytes"]
                     if sample["usage_bytes"] is not None and start["usage_bytes"] is not None else None)
            memory_deltas.append({"pass": sample["pass"], "generation": sample["generation"],
                "phase": sample["phase"], "usage_delta_bytes": delta,
                "evidence": "process_usage_delta_not_model_allocation_or_gpu_peak"})
    return {"header": header, "record_count": len(records), "first_anomaly": first, "ui_input": ui_summary,
            "nr_memory": memory, "nr_memory_deltas": memory_deltas, "nr_memory_suppressed": memory_overflow,
            "nr_queue_history": [{"sequence": r["sequence"], "frame": r["frame"],
                "queue": r["queue"], "command_list": r["command_list"], "action": r["reason"],
                "evidence": "pinned_past_execute_used_to_select_queue_not_current_gpu_completion"}
                for r in records if r["type"] == "nr_queue_history"],
            "nr_outcomes": outcomes, "nr_outcome_summary": outcome_summary,
            "high_resolution_contracts": [{"frame": r["frame"], "event": r["type"],
                "output": [r["width"], r["height"]], "network": [r["network_width"], r["network_height"]],
                "guides": [r["guide_width"], r["guide_height"]], "result": r["result"],
                "evidence": "cpu_contract_not_gpu_completion"}
                for r in records if r["type"] in {"frame_contract", "feature_create", "evaluate", "composed"}
                    and r["flags"] & 256],
            "nr_failure_states": [{"frame": r["frame"], "target_format": r["flags"], "device_lost": bool(r["result"]),
                "reason": r["reason"], "output": [r["width"], r["height"]], "evidence": "latched_cpu_failure_not_new_gpu_execution"}
                for r in records if r["type"] == "nr_failure_state"],
            "multipass_contracts": [{"frame": r["frame"], "target_format": r["flags"], "effective": r["result"],
                "reason": r["reason"], "evidence": "backend_contract_not_gameplay_verdict"}
                for r in records if r["type"] == "mp_contract"],
            "multipass_events": [{"frame": r["frame"], "event": r["type"], "pass": (r["flags"]>>8)&15,
                "requested": (r["flags"]>>12)&15, "ready": (r["flags"]>>16)&15, "recorded": (r["flags"]>>20)&15,
                "reset": bool(r["flags"]&1), "result": r["result"], "reason": r["reason"],
                "ratio": r["ratio"], "network": [r["network_width"],r["network_height"]],
                "submitted": bool(r["flags"]&16), "completed": bool(r["flags"]&32),
                "evidence": "snapshot_not_gameplay_verdict"} for r in records if r["type"].startswith("mp_") and r["type"] != "mp_contract"],
            "sr_output_rects": output_rects,
            "native_sr_progress": [r for r in records if r["type"] == "native_sr_progress"],
            "capture_write_failures": [{"frame": r["frame"], "reason": r["reason"]}
                for r in records if r["type"] == "capture_write_failed"],
            "highlight_encoding_events": [{"frame": r["frame"], "event": r["type"], "reason": r["reason"],
                "mode": r["result"]} for r in records if r["type"] in ("highlight_encoding", "highlight_encoding_unavailable")],
            "dlssg_hooks": [{"sequence": r["sequence"], "action_stage": r["reason"],
                              "result": r["result"]} for r in records if r["type"] == "dlssg_hook"],
            "allocation_failures": [dict(role=r["reason"], width=r["width"], height=r["height"], format=r["flags"], result=r["result"], device_lost=r["type"] == "device_lost") for r in records if r["type"] in {"allocation_failed", "device_lost"}],
            "skip_reason_counts": dict(skip_counts), "incomplete_fences": gaps,
            "pending_recordings": pending_recordings, "evidence_gaps": evidence_gaps}


def markdown(summary: dict) -> str:
    h = summary["header"]
    lines = ["# D18 diagnostics summary", "", f"- Game: `{h['game']}`", f"- Backend: `{h['backend']}`",
             f"- Session: `{h['session']}`", f"- Records: {summary['record_count']}; overwritten: {h['dropped']}", ""]
    ui = summary["ui_input"]
    outcome = summary["nr_outcome_summary"]
    lines += ["## SR queue history (C4)", "",
              f"- Pinned prior Execute promotions observed: {len(summary['nr_queue_history'])}.",
              "- Queue selection evidence only; every new NR recording still needs its own Execute/Signal/fence.",
              "- Queue rejection subreasons are counted under NR output fallback reasons below; absent events mean unobserved.", ""]
    lines += ["", "## NR recorded output (C2)", "",
              f"- Observed attempts: {outcome['observed_attempts']}; complete SR fallback: {outcome['sr_fallback_attempts']}; reduced pass count: {outcome['reduced_pass_attempts']}",
              f"- Signature overflow: {outcome['signature_overflow_attempts']}; requested -> composed: {outcome['requested_to_composed']}",
              f"- Fallback reasons: {outcome['fallback_reasons']}",
              f"- History resets: {outcome['reset_reasons']}",
              "- CPU command recording only; not proof of GPU completion or displayed frames.",
              "- Summary groups one-second windows; absence of records is not proof of no failures."]
    if ui["samples"]:
        lines += ["## UI input observations", "", f"- Routes: {', '.join(ui['routes'])}.",
                  f"- Samples: {ui['samples']}; wheel: {ui['wheel_samples']}; held: {ui['held_samples']}; scrollbar active: {ui['scrollbar_active_samples']}.",
                  f"- Observed vertical scroll range: {ui['observed_scroll_range']}; button-state mismatches: {ui['button_mismatch_samples']}.",
                  "- Missing sampled input is not proof that no event arrived. Gameplay success is not inferred.", ""]
    lines += ["", "## NR memory observations (C3)", "",
              f"- Samples: {len(summary['nr_memory'])}; reported suppressed: {summary['nr_memory_suppressed']}.",
              "- Lifecycle samples only; no samples means unobserved. Query failure is unknown, not zero usage.",
              "- Deltas are whole-process observations, not isolated model allocations or GPU peaks."]
    for sample in summary["nr_memory"]:
        lines.append(f"- Pass {sample['pass']} generation {sample['generation']} {sample['phase']}: "
                     f"usage/budget {sample['usage_bytes']}/{sample['budget_bytes']} bytes; "
                     f"proposed/reserve {sample['proposed_bytes']}/{sample['reserve_bytes']}; admitted={sample['admitted']}.")
    for delta in summary["nr_memory_deltas"]:
        lines.append(f"- Pass {delta['pass']} generation {delta['generation']} {delta['phase']} "
                     f"process usage delta: {delta['usage_delta_bytes']} bytes.")
    if summary.get("nr_failure_states"):
        lines += ["", "## Retained NR failures (including late-enabled diagnostics)", ""]
        lines += [f"- Frame {r['frame']}: format {r['target_format']}, device lost={r['device_lost']}; {r['reason']}." for r in summary["nr_failure_states"]]
    if summary.get("multipass_contracts"):
        lines += ["", "## Multipass backend contracts", ""]
        lines += [f"- Frame {r['frame']}: target format {r['target_format']}, effective passes {r['effective']}; {r['reason']}." for r in summary["multipass_contracts"]]
    if summary.get("multipass_events"):
        lines += ["## Multipass observations", "", "Requested, ready and CPU-recorded pass counts are distinct; completion flags describe only the observed ticket.", ""]
        lines += [f"- Frame {r['frame']} pass {r['pass']}: {r['event']}, requested/ready/recorded {r['requested']}/{r['ready']}/{r['recorded']}, result {r['result']}, {r['reason']}." for r in summary["multipass_events"]]
    first = summary["first_anomaly"]
    lines.append("## First anomaly")
    lines.append("")
    lines.append("None recorded." if first is None else
                 f"Sequence {first['sequence']}, frame {first['frame']}: `{first['type']}` — {first['reason'] or hex(first['result'])}")
    lines.extend(["", "## Skip reasons", ""])
    counts = summary["skip_reason_counts"]
    lines.extend([f"- {reason}: {count}" for reason, count in sorted(counts.items())] or ["None recorded."])
    lines.extend(["", "## SR output rectangles", ""])
    rects = summary.get("sr_output_rects", [])
    lines.extend([f"- Frame {r['frame']}: reported {r['reported_output']}, render {r['sr_render']}, effective {r['effective_output']} ({r['action']})." for r in rects]
                 or ["None recorded; output rectangle coverage is unobserved."])
    lines.extend(["", "## DLSSG hook lifecycle", ""])
    lines.extend([f"- {r['action_stage']}: code 0x{r['result']:X}." for r in summary.get("dlssg_hooks", [])]
                 or ["None recorded; hook lifecycle is unobserved."])
    if summary.get("highlight_encoding_events"):
        lines.extend(["", "## Highlight encoding", ""])
        lines.extend([f"- Frame {r['frame']}: {r['event']} ({r['reason']}), mode {r['mode']}."
                      for r in summary["highlight_encoding_events"]])
    if summary.get("capture_write_failures"):
        lines.extend(["", "## Capture write failures", ""])
        lines.extend([f"- Frame {r['frame']}: {r['reason']}." for r in summary["capture_write_failures"]])
    if summary.get("high_resolution_contracts"):
        last=summary["high_resolution_contracts"][-1]
        lines += ["", "## High-resolution single NR", "",
                  f"- Last observed contract: output {last['output']}, network {last['network']}, guides {last['guides']}.",
                  "- One runtime evaluation and one final composition. CPU contract; GPU completion and gameplay remain separate evidence."]
    lines.extend(["", "## Native SR startup / hot-control observations", ""])
    progress = summary.get("native_sr_progress", [])
    lines += [f"- Frame {r['frame']}: {r['reason']}; enable flags {r['flags']}; coverage rejection bits {r['result']}; scale generation {r['feature_generation']}." for r in progress]
    if not progress: lines.append("Unobserved. No native SR startup diagnosis can be inferred from missing samples.")
    lines.append("Gates: 0=no matching target observed, 1=target, 2=SR off, 3=scale change, 4=transaction/owner/fault/deferred gate, 5=preset pending, 6=context/buffer validation, 7=fresh constants missing, 8=dimensions/guides/output validation, 9=coverage rejection, 10=validated inputs. Counters are cumulative; use changes, not nonzero totals, to assess progress. Target age distinguishes stale observations.")
    lines.extend(["", "## Evidence gaps", ""])
    lines.append(f"Missing queue evidence: {len(summary['evidence_gaps'])}; submitted incomplete fences: {len(summary['incomplete_fences'])}; recordings awaiting submission at capture time: {len(summary['pending_recordings'])}.")
    if "capture_evidence" in summary:
        capture = summary["capture_evidence"]
        eligible = sum(p["eligible"] for p in capture["temporal_pairs"])
        lines += ["", "## Matched frame capture", "",
                  f"- Paired frames: {len(capture['frames'])}; eligible temporal pairs: {eligible}.",
                  "- Same-frame SR/NR observation; strict cross-run guides/history replay is unavailable.",
                  "- Per-frame metrics and reset/warm-up evidence are included in the JSON summary."]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ring", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--markdown", type=pathlib.Path)
    parser.add_argument("--capture", type=pathlib.Path, help="Optional bounded four-stream capture directory")
    parser.add_argument("--capture-min-warmup", type=int, default=32)
    args = parser.parse_args()
    header, records = read_ring(args.ring)
    result = summarize(header, records)
    if args.capture:
        if args.capture_min_warmup < 1: parser.error("--capture-min-warmup must be positive")
        add_capture(result, args.capture, args.capture_min_warmup)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.json: args.json.write_text(encoded, encoding="utf-8")
    else: print(encoded, end="")
    if args.markdown: args.markdown.write_text(markdown(result), encoding="utf-8")
    return 0


def add_capture(summary: dict, directory: pathlib.Path, min_warmup: int = 32) -> None:
    # Keep normal ring summaries dependency-free; NumPy/SciPy are loaded only on request.
    import importlib.util
    spec = importlib.util.spec_from_file_location("capture_evidence", pathlib.Path(__file__).with_name("analyse-capture-evidence.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    summary["capture_evidence"] = module.analyse(directory, min_warmup)


if __name__ == "__main__":
    raise SystemExit(main())
