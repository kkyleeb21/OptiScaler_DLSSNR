"""P3 shared-ring decoder. No timing or FG-off inference from missing routes."""
import collections
import math


def summarize_fg_chain(header, records):
    windows, changes = [], []
    classes = {(1, 1): "A", (2, 2): "A", (3, 3): "A", (0, 0): "B"}
    for r in records:
        if r["type"] not in {"fg_chain_second", "fg_chain_change"}:
            continue
        req, sub = (r["flags"] >> 8) & 15, (r["flags"] >> 12) & 15
        n, duration = r["guide_width"], r["feature_generation"] - r["frame"]
        sample = {"sequence": r["sequence"], "route": r["reason"], "class": classes.get((req, sub), "unknown"),
            "requested_mode": req, "submitted_mode": sub, "begin_ms": r["frame"], "end_ms": r["feature_generation"],
            "duration_ms": duration, "calls": n, "failures": r["guide_height"], "last_result": r["result"],
            "changed": bool(r["flags"] & 1), "values_observed_in_window": bool(r["flags"] & 2),
            "first": [r["queue"], r["command_list"], r["width"], r["height"]] if n else None,
            "last": [r["fence_target"], r["fence_completed"], r["network_width"], r["network_height"]] if n else None,
            "carried_last": [r["fence_target"], r["fence_completed"], r["network_width"], r["network_height"]],
            "sum": r["ratio"] if math.isfinite(r["ratio"]) else None,
            "hz": n * 1000 / duration if duration > 0 else None}
        (changes if r["type"] == "fg_chain_change" else windows).append(sample)
    durations = collections.defaultdict(dict)
    for w in windows:
        durations[w["class"]][(w["begin_ms"], w["end_ms"])] = w["duration_ms"]
    routes = sorted({w["route"] for w in windows})
    comparison = []
    for label in ("A", "B", "unknown"):
        duration = sum(durations[label].values())
        for route in routes:
            group = [w for w in windows if w["class"] == label and w["route"] == route]
            live = [w for w in group if w["calls"]]
            calls = sum(w["calls"] for w in group)
            covered = sum(w["duration_ms"] for w in group)
            values = {tuple(v) for w in live for v in (w["first"], w["last"])}
            comparison.append({"class": label, "route": route, "duration_ms": duration,
                "covered_ms": covered, "complete_time_coverage": covered==duration, "calls": calls if group else None,
                "hz": calls * 1000 / covered if group and covered else None,
                "failures": sum(w["failures"] for w in group) if group else None,
                "first": live[0]["first"] if live else None, "last": live[-1]["last"] if live else None,
                "values": sorted(values), "changed": any(w["changed"] for w in live) or len(values) > 1,
                "zero_windows": sum(w["calls"] == 0 for w in group),
                "carried_last": group[-1]["carried_last"] if group else None,
                "sum": sum(w["sum"] or 0 for w in group) if group else None})
    limits = [w for w in windows if w["route"] == "collector.limits"]
    options = [w for w in windows if w["route"] == "options"]
    return {"observed": bool(windows), "windows": windows, "changes": changes, "comparison": comparison,
        "class_duration_ms": {k: sum(v.values()) for k, v in durations.items()},
        "options_calls": sum(w["calls"] for w in options),
        "slot_overflow": max((w["last"][0] for w in limits if w["last"]), default=0),
        "transitions_suppressed": max((w["last"][1] for w in limits if w["last"]), default=0),
        "records_suppressed": sum(r["frame"] for r in records if r["type"] == "diagnostic_budget"),
        "ring_overwritten": header.get("dropped", 0),
        "limitations": ["A/B classify equal request/submitted modes (A=on/auto/dynamic, B=off); unequal or unreadable pairs are unknown",
            "Window closes on next observed call; final partial second requires slShutdown",
            "Zero requires previously seen route and continuing observer heartbeat; absence is unknown",
            "First/last plus changed does not retain intermediate values or per-frame ordering",
            "Multi-viewport/chain observations are pooled; compare context IDs before assigning causality",
            "Failed reads and suppressed records are gaps, not unchanged values"]}


def markdown_fg_chain(chain):
    if not chain.get("observed"):
        return []
    lines = ["", "## FG chain A / B (request x submitted mode)", "",
        "| State | Route | covered/total seconds | calls/s | failures | first -> last | changed |",
        "|---|---|---:|---:|---:|---|---|"]
    for row in chain["comparison"]:
        if row["class"] == "unknown" or row["calls"] is None:
            continue
        hz = f"{row['hz']:.2f}" if row["hz"] is not None else "unknown"
        lines.append(f"| {row['class']} | {row['route']} | {row['covered_ms']/1000:.3f}/{row['duration_ms']/1000:.3f} | {hz} | {row['failures']} | {row['first']} -> {row['last']} | {row['changed']} |")
    lines += ["", f"Ring overwrite={chain['ring_overwritten']}; record suppression={chain['records_suppressed']}; slot overflow={chain['slot_overflow']}; immediate transitions suppressed={chain['transitions_suppressed']}",
        "Zero calls require a previously observed route; absence is unknown. Metadata is not GPU/display acceptance."]
    return lines
