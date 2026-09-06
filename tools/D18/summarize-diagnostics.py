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
    abnormal = {"nr_skip", "compose_skip"}
    first = next((r for r in records if r["type"] in abnormal or
                  (r["type"] in {"feature_create", "evaluate"} and r["result"] != 1)), None)
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
    return {"header": header, "record_count": len(records), "first_anomaly": first,
            "skip_reason_counts": dict(skip_counts), "incomplete_fences": gaps,
            "pending_recordings": pending_recordings, "evidence_gaps": evidence_gaps}


def markdown(summary: dict) -> str:
    h = summary["header"]
    lines = ["# D18 diagnostics summary", "", f"- Game: `{h['game']}`", f"- Backend: `{h['backend']}`",
             f"- Session: `{h['session']}`", f"- Records: {summary['record_count']}; overwritten: {h['dropped']}", ""]
    first = summary["first_anomaly"]
    lines.append("## First anomaly")
    lines.append("")
    lines.append("None recorded." if first is None else
                 f"Sequence {first['sequence']}, frame {first['frame']}: `{first['type']}` — {first['reason'] or hex(first['result'])}")
    lines.extend(["", "## Skip reasons", ""])
    counts = summary["skip_reason_counts"]
    lines.extend([f"- {reason}: {count}" for reason, count in sorted(counts.items())] or ["None recorded."])
    lines.extend(["", "## Evidence gaps", ""])
    lines.append(f"Missing queue evidence: {len(summary['evidence_gaps'])}; submitted incomplete fences: {len(summary['incomplete_fences'])}; recordings awaiting submission at capture time: {len(summary['pending_recordings'])}.")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ring", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--markdown", type=pathlib.Path)
    args = parser.parse_args()
    header, records = read_ring(args.ring)
    result = summarize(header, records)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.json: args.json.write_text(encoded, encoding="utf-8")
    else: print(encoded, end="")
    if args.markdown: args.markdown.write_text(markdown(result), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
