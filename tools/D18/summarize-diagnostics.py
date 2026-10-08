#!/usr/bin/env python3
"""Parse the fixed D18 diagnostics ring and emit deterministic JSON/Markdown summaries."""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import struct
import math
import importlib.util
_chain_spec = importlib.util.spec_from_file_location('fg_chain_summary', pathlib.Path(__file__).with_name('fg_chain_summary.py'))
_chain_module = importlib.util.module_from_spec(_chain_spec)
_chain_spec.loader.exec_module(_chain_module)
summarize_fg_chain = _chain_module.summarize_fg_chain

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
                  (r["type"] == "nr_outcome" and r["result"] != 1 and not r["reason"].startswith(("summary_overflow", "fg_off_paused"))) or
                  (r["type"] in {"feature_create", "evaluate", "mp_create", "mp_evaluate"} and r["result"] != 1)), None)
    skip_counts = collections.Counter(r["reason"] or "unknown" for r in records if r["type"] == "nr_skip")
    gaps = []
    pending_recordings = []
    for r in records:
        if r["type"] in {"fg_signal", "fg_chain_second", "fg_chain_change", "fg_chain_limits"}: continue  # FG uses these slots for NR counters, not fences.
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
            "model_calls": (flags >> 28) & 15,
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
    pause_outcomes = [o for o in known if o["reason"] == "fg_off_paused"]
    outcome_summary = {
        "fg_off_paused_attempts": sum(o["count"] for o in pause_outcomes),
        "fg_off_paused_evaluated_passes": sum(o["count"] * o["evaluated"] for o in pause_outcomes),
        "fg_off_paused_composed_passes": sum(o["count"] * o["composed"] for o in pause_outcomes),
        "observed_attempts": sum(o["count"] for o in known),
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
    exposure_channels = [decode_exposure(r) for r in records if r['type'] == 'exposure_probe']
    return {"header": header, "record_count": len(records), "first_anomaly": first, "ui_input": ui_summary,
            "fg_probe": summarize_fg_probe(header, records),
            "fg_chain": summarize_fg_chain(header, records),
            "exposure_channels": exposure_channels,
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


FG_SOURCES = ("sl_options", "sl_state", "sl_loaded", "ngx_create", "ngx_evaluate", "reflex_marker", "reflex_async")


def summarize_fg_probe(header: dict, records: list[dict]) -> dict:
    """Schema-1 FG metadata; absent/failed reads stay null, counters never imply FG off."""
    options = []
    for r in records:
        if r["type"] != "fg_options": continue
        path, _, reason = r["reason"].partition("/")
        bits = r["flags"]
        options.append({"sequence": r["sequence"], "time_ms": r["monotonic_ms"],
            "context": r["command_list"] if bits & 1 else None, "path": path, "reason": reason,
            "requested_mode": r["height"] if bits & 1 else None,
            "submitted_mode": r["network_width"] if bits & 2 else None,
            "requested_count": r["guide_width"] if bits & 1 else None,
            "submitted_count": r["guide_height"] if bits & 2 else None,
            "return_code": r["result"]})
    off = [v for v in options if v["requested_mode"] == 0 and v["submitted_mode"] == 0]
    option_observation = {"observed": bool(options), "options_content_changes": options,
        "off_requests": len(off), "off_successes": sum(v["return_code"] == 0 for v in off),
        "unclassified_requests": sum(v["requested_mode"] is None or v["submitted_mode"] is None for v in options),
        "counts_scope": "retained content changes only; use P3 closed windows for call totals",
        "request_rewriting": "removed in P4; no rewrite events are produced"}
    samples, groups = [], collections.defaultdict(list)
    states = {0: "unobserved", 1: "off", 2: "on", 3: "call_failed", 4: "auxiliary"}
    for r in records:
        if r["type"] != "fg_signal": continue
        parts = r["reason"].split(";")
        fields = dict(p.split("=", 1) for p in parts[1:] if "=" in p)
        bits = r["flags"]
        def valid(flag, key): return r[key] if bits & flag else None
        def number(key):
            try: return int(fields[key])
            except (ValueError, KeyError): return None
        age = r["width"] if r["width"] != 0xffffffff else None
        na = number("na")
        nr = number("nr")
        sample = {"source": parts[0], "snapshot_id": r["feature_generation"], "sequence": r["sequence"],
            "sample_ms": r["monotonic_ms"], "updated_ms": r["queue"] if age is not None else None,
            "age_ms": age, "context": r["command_list"] if age is not None else None,
            "d18_nr_attempt": r["frame"] if nr else None, "state": states.get((bits >> 8) & 255, "unobserved"),
            "mode": valid(1, "height"), "num_frames_to_generate": valid(2, "network_width"),
            "option_flags": valid(4, "network_height"), "aux0": valid(8, "guide_width"),
            "aux1": valid(16, "guide_height"), "return_code": valid(32, "result"),
            "observed_call": bool(bits & 64), "changed": bool(bits & 65536), "trigger": fields.get("t"),
            "nr_latest_evaluated": None if not nr else nr == 2,
            "nr_age_ms": None if na == 0xffffffff else na, "nr_source": number("ns") if nr else None,
            "nr_evaluated_frames_total": r["fence_target"], "nr_composed_frames_total": r["fence_completed"]}
        samples.append(sample); groups[sample["snapshot_id"]].append(sample)
    candidates, transitions = {}, []
    for source in FG_SOURCES:
        events = [s for s in samples if s["source"] == source and s["changed"]]
        # The first sample is coverage/initial observation, not a transition.
        candidates[source] = {"observed": any(s["observed_call"] for s in samples if s["source"] == source),
            "changes": max(0, len(events) - 1), "initial_observation": events[0] if events else None,
            "change_times_ms": [s["updated_ms"] for s in events[1:]], "events": events}
        transitions.extend(events)
    transitions.sort(key=lambda s: (s["updated_ms"] or 0, s["sequence"]))
    ordered = []
    for i, s in enumerate(transitions):
        previous = transitions[i - 1] if i else None
        ordered.append({"source": s["source"], "time_ms": s["updated_ms"], "context": s["context"],
            "state": s["state"], "delta_from_previous_ms":
                s["updated_ms"] - previous["updated_ms"] if previous else None,
            "other_signals": [{k: v[k] for k in ("source", "state", "context", "updated_ms", "age_ms", "mode",
                "num_frames_to_generate", "option_flags", "aux0", "aux1", "return_code")}
                for v in groups[s["snapshot_id"]]]})
    intervals, start = [], None
    def close_interval(end, censored):
        duration = max(0, end["sample_ms"] - start["sample_ms"])
        count = end["nr_evaluated_frames_total"] - start["nr_evaluated_frames_total"]
        composed = end["nr_composed_frames_total"] - start["nr_composed_frames_total"]
        intervals.append({"context": start["context"], "start_ms": start["sample_ms"], "end_ms": end["sample_ms"],
            "duration_ms": duration, "nr_evaluated_frames": count, "nr_composed_frames": composed,
            "estimated_nr_hz": count * 1000 / duration if duration and count >= 0 else None,
            "end_censored": censored, "evidence": "successful_sl_eOff_request_interval_global_CPU_NR_outcome_counters"})
    for s in [s for s in samples if s["source"] == "sl_options" and s["return_code"] == 0 and
              s["mode"] in (0, 1, 2, 3)]:
        if start and (s["state"] != "off" or s["context"] != start["context"]):
            close_interval(s, s["mode"] == 0 or s["context"] != start["context"]); start = None
        if start is None and s["state"] == "off" and s["changed"]: start = s
    if start:
        end = next((s for s in reversed(samples) if s["source"] == "sl_options"), start)
        close_interval(end, True)
    chain=summarize_fg_chain(header,records)
    if chain["observed"]:
        opts=[w for w in chain["windows"] if w["route"]=="options"]
        option_observation["counts_scope"]="P3 closed options windows; options_content_changes contains changes only"
        option_observation["options_calls"]=chain["options_calls"]
        option_observation["off_requests"]=sum(w["calls"] for w in opts if w["class"]=="B")
        option_observation["off_successes"]=sum(w["calls"]-w["failures"] for w in opts if w["class"]=="B")
        option_observation["unclassified_requests"]=sum(w["calls"] for w in opts if w["class"]=="unknown")
    return {"options": option_observation, "candidates": candidates, "ordered_changes": ordered, "off_request_intervals": intervals,
        "snapshots": [{"id": key, "complete": len(value) == len(FG_SOURCES), "signals": value} for key, value in groups.items()],
        "coverage": [{"time_ms": r["monotonic_ms"], "reason": r["reason"], "result": r["result"]}
                     for r in records if r["type"] == "fg_probe_coverage"],
        "limitations": ["on/off are accepted explicit requests, not proof of displayed FG",
            "sl_state aux0=status aux1=presented_since_last_query; no menu-hit or off inference",
            "ngx aux0=NotRenderingGameFrames aux1=MenuDetectionEnabled; neither is an FG mode",
            "reflex count is the existing heuristic, aux0/aux1 are low/high marker frame ID",
            "NR evaluated means an attempt with successful CPU-recorded Evaluate; no GPU/display acceptance",
            "NR on hook samples is the latest completed NR attempt, not necessarily the hook's frame",
            "keep diagnostics continuously enabled during one capture; off gaps are not reconstructed",
            "counts cover all NR sources; multi-viewport matching is deliberately not inferred"],
        "ring_overwritten": header.get("dropped", 0), "probe_observed": bool(samples)}


def markdown(summary: dict) -> str:
    h = summary["header"]
    lines = ["# D18 diagnostics summary", "", f"- Game: `{h['game']}`", f"- Backend: `{h['backend']}`",
             f"- Session: `{h['session']}`", f"- Records: {summary['record_count']}; overwritten: {h['dropped']}", ""]
    ui = summary["ui_input"]
    outcome = summary["nr_outcome_summary"]
    lines.append(f"- FG-off policy pauses: {outcome['fg_off_paused_attempts']}; evaluated passes: {outcome['fg_off_paused_evaluated_passes']}; composed passes: {outcome['fg_off_paused_composed_passes']}.")
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
    lines.extend(["", "## Exposure channels / pre-NR luminance", ""])
    channels = summary.get('exposure_channels', [])
    def observed(v):
        return 'unobserved' if v is None else str(v).lower() if isinstance(v, bool) else str(v)
    for r in channels:
        l = r['luma']
        lines.append(f"- Frame {r['frame']} {observed(r['driver'])}, NR={r['nr_enabled']}: texture={observed(r['texture_offered'])}, "
                     f"readback={observed(r['texture_readback'])} (frame {observed(r['texture_readback_frame'])}); "
                     f"Pre_Exposure={observed(r['pre_exposure'])}, Exposure_Scale={observed(r['exposure_scale'])}; "
                     f"AutoExposure={observed(r['auto_exposure'])}, HDR={observed(r['hdr'])}; "
                     f"white={r['white_point']} ({r['white_source']}, applied={r['white_point_applied']}); "
                     f"pre-NR mean log2(Y)={observed(l['mean_log2'])} (sample frame {observed(l['frame'])}, "
                     f"sample NR={observed(l['nr_enabled'])}, n={observed(l['valid_samples'])}); Lraw={observed(r['Lraw'])} b={observed(r['b'])} W={r['W']:.6g}; {r['status']}.")
    if not channels: lines.append('Unobserved. No exposure-channel events retained; absence does not mean zero/false.')
    lines.append('Actual Streamline DLSSSetOptions: unobserved. NGX getter success does not establish original setter provenance; Exposure_Scale=1 may be an OptiScaler initialized default. Luminance uses 64 probe points, or 4096 cells with 16 stratified points each and 2% tails trimmed in estimate mode, before NR writes and after actual Execute + fence completion; compare its sample frame/state, not the record frame. Legacy exposure texture readback uses frame age rather than a fence.')
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
    lines += _chain_module.markdown_fg_chain(summary.get("fg_chain", {}))
    fg = summary.get("fg_probe", {})
    lines.extend(["", "## FG passive probe / FG 旁听探针", ""])
    options = fg.get("options", {})
    lines.append(f"- FG off requests: {options.get('off_requests', 0)}; successful: {options.get('off_successes', 0)}; scope: {options.get('counts_scope', 'unknown')}. Request rewriting was removed in P4.")
    lines.append("Options counts use closed per-second windows when available; content-change records alone are not call totals. Overwritten or unobserved intervals remain unknown.")
    for source, candidate in fg.get("candidates", {}).items():
        lines.append(f"- {source}: observed={candidate['observed']}; changes={candidate['changes']}; times_ms={candidate['change_times_ms']}.")
    for interval in fg.get("off_request_intervals", []):
        lines.append(f"- Accepted eOff interval {interval['start_ms']}..{interval['end_ms']} ms, context={interval['context']}: "
            f"NR evaluated frames={interval['nr_evaluated_frames']}, composed={interval['nr_composed_frames']}, "
            f"estimated Hz={interval['estimated_nr_hz']}, end censored={interval['end_censored']}.")
    for change in fg.get("ordered_changes", []):
        lines.append(f"- {change['time_ms']} ms {change['source']} {change['state']}; delta from previous={change['delta_from_previous_ms']} ms.")
    lines.append("Snapshots in JSON contain all candidate values and ages. Initial observation is not counted as a change; "
        "accepted requests and CPU NR outcomes do not establish displayed FG or GPU completion. Missing means unobserved.")
    return "\n".join(lines) + "\n"


def decode_exposure(r: dict) -> dict:
    # Event-specific schema 1 slot interpretation, shared with ExposureProbe_Dx12.inl.
    f = r['flags']
    def number(key, known):
        if not f & known: return None
        v = r[key]
        return v if math.isfinite(v) else str(v)  # retain an observed NaN/Inf without invalid JSON
    def driver(flags):
        return ('RR' if flags & 512 else 'SR') if flags & 256 else None
    return {'sequence': r['sequence'], 'frame': r['frame'], 'source_id': r['feature_generation'],
            'status': r['reason'], 'driver': driver(f), 'nr_enabled': bool(f & 128),
            'pre_exposure': number('ratio', 1), 'exposure_scale': number('exposure', 2),
            'scalar_provenance': 'NGX_Get_success_not_original_Set_provenance',
            'texture_offered': bool(f & 8) if f & 4 else None,
            'texture_readback': number('mv_scale_x', 2048),
            'texture_readback_frame': r['command_list'] if f & 2048 and r['command_list'] else None,
            'texture_readback_completion': 'legacy_frame_age_not_fenced' if f & 2048 else None,
            'creation_flags': r['network_height'] if f & 16 else None,
            'auto_exposure': bool(f & 32) if f & 16 else None,
            'hdr': bool(f & 64) if f & 16 else None,
            'effective_hdr': bool(f & 16384) if f & 65536 else None, 'use_game_exposure': bool(f & 32768),
             'white_point': r['white_point'],
            'white_source': 'game_exposure' if f & 1024 else 'waiting_first_measurement' if f & 524288 else
                            'hold' if f & 262144 else 'scene_estimate' if f & 131072 else 'slider',
            'Lraw': number('mv_scale_y', 4096) if f & 131072 else None,
            'b': struct.unpack('<f',struct.pack('<I',r['result']))[0] if f & 1048576 else None,
            'W': r['white_point'],
            'white_point_applied': bool(f & 8192),
            'streamline_options': {'preExposure': None, 'exposureScale': None, 'useAutoExposure': None,
                                  'coverage': 'actual_DLSSSetOptions_not_intercepted'},
            'output': [r['width'], r['height']], 'output_format': r['network_width'],
            'luma': {'mean_log2': number('mv_scale_y', 4096), 'frame': r['queue'] if f & 4096 else None,
                     'valid_samples': r['guide_width'] if f & 4096 else None,
                     'nr_enabled': bool(r['guide_height'] & 128) if f & 4096 else None,
                     'driver': driver(r['guide_height']) if f & 4096 else None,
                     'stage': 'pre_NR_upscale_output', 'method': '64x64_cells_16_stratified_points_2pct_trim_log2' if f & 131072 else '8x8_uniform_points_log2_max_Y_1e-6',
                     'completion': 'actual_execute_then_fence' if f & 4096 else None}}


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
