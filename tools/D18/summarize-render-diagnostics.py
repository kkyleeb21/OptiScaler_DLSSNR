"""Compatibility entry point for the consolidated D18 ring summarizer."""
import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location("d18_shared_ring",Path(__file__).with_name("summarize-diagnostics.py"))
_shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(_shared)
for _name in ("read_ring","summarize","markdown","add_capture","main","HEADER","RECORD"):
 globals()[_name]=getattr(_shared,_name)
if __name__=="__main__":raise SystemExit(main())
