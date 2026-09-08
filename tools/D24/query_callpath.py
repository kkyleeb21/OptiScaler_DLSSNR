#!/usr/bin/env python3
"""Find observed direct-edge paths in an ngx_audit S1_graph.json.

No path in a bounded graph means UNKNOWN, never unreachable. A path is not
proof that it executes with Feature 18 or shares an internal state object.
"""
import argparse
import collections
import json
import pathlib


def find_path(graph, start, target):
    nodes = {n['rva']: n for n in graph['nodes']}
    parents = {start: None}
    todo = collections.deque([start])
    while todo:
        here = todo.popleft()
        if here == target:
            path = []
            while here is not None:
                path.append(here)
                here = parents[here]
            return dict(status='OBSERVED_DIRECT_PATH', path=[hex(r) for r in reversed(path)],
                        qualification='Conditional/static path only; does not prove runtime reachability or state layout.')
        for edge in nodes.get(here, {}).get('edges', []):
            next_rva = edge['target']
            if next_rva is not None and next_rva not in parents:
                parents[next_rva] = here
                todo.append(next_rva)
    return dict(status='UNKNOWN', reason='No path observed within this bounded direct-edge graph; indirect targets or budgets may hide it.')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('graph', type=pathlib.Path)
    ap.add_argument('start', type=lambda x: int(x, 0))
    ap.add_argument('target', type=lambda x: int(x, 0))
    args = ap.parse_args()
    print(json.dumps(find_path(json.loads(args.graph.read_text(encoding='utf-8')), args.start, args.target), indent=2))
