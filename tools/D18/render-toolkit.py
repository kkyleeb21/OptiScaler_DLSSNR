"""Explicit shared entry point. Listing and offline analysis never launch GPU probes."""
from pathlib import Path
import argparse,json,subprocess,sys
ROOT=Path(__file__).resolve().parent
ACTIONS={
 'input-discovery':('summarize-input-probe.py','offline','Bounded DX11 shader/input observations and hook coverage; no automatic SR/FG admission'),
 'gpu-vulkan-advanced':('test-vulkan-advanced.py','gpu','Real Vulkan NR chain, typed targets, partial-allocation rollback and neutral composition; isolated host, not game/hook acceptance'),
 'vulkan-nr':('summarize-vulkan-nr.py','offline','Vulkan advanced recordings, failures and separate submission/completion observations'),
 'gpu-native-instances':('test-native-instances.py','gpu','Bounded real NR feature ownership, chain, hot switches and typed formats; no gameplay'),
 'layer-style':('analyze-layer-style.py','offline','Same-frame independent model outputs: colour shares and spatial contrast; no game/model execution'),
 'gpu-neutral-compose':('test-neutral-pass-compose.py','gpu','Synthetic edited first model and neutral second model: isolate final composition; no game/model loading'),
 'gpu-color-formats':('test-advanced-color-formats.py','gpu','Bounded format matrix, signed FP16 intermediates and failure recovery; no game/model loading'),
 'summary':('summarize-diagnostics.py','offline','D18 ring v1: outcomes, memory, queue observation, highres, multipass, capture, allocation/device loss'),
 'native-sr':('summarize-input-sr.py','offline','Native SR/DLAA handoff and execution observations'),
 'native-nr':('summarize-native-nr.py','offline','Native DX11 NR results and bounded input observations'),
 'native-fg':('summarize-native-fg.py','offline','FG present/query/status recovery and generated-frame evidence'),
 'native-scale':('summarize-native-settings.py','offline','Queued native resolution requests and observed outcomes'),
 'render-hang':('summarize-render-hang.py','offline','Thread/checkpoint coverage; absent observations are not successful execution'),
 'command-lists':('summarize-command-lists.py','offline','Explicit research command-list capture'),
 'writer-journal':('summarize-writer-journal.py','offline','Bounded native post-replay writer metadata'),
 'profile-policy':('test-native-profile-policy.py','cpu','Release/research routing policy, no runtime or GPU loading'),
 'gpu-native-fg':('test-native-fg.py','gpu','Owned-host SR/NR/FG lifecycle checks, not game-quality acceptance'),
 'gpu-native-nr':('test-native-nr.py','gpu','Owned-host NR handoff, resize and failure isolation checks'),
 'layers':('analyze-layer-capture.py','offline','C7 layer capture completeness and numerical observations'),
 'compare-layers':('compare-layer-captures.py','offline','Matched captures; missing inputs are evidence gaps'),
 'feedback':('analyze-feedback-probe.py','offline','C8-C11 controlled host pixel comparisons'),
 'resources':('analyze-nr-resource-probe.py','offline','R1 process VRAM and measured Evaluate timings'),
 'runtime-state':('summarize-nr-runtime-state.py','offline','C12 requested/effective Reset observations'),
 'runtime-bindings':('summarize-nr-runtime-bindings.py','offline','C13 observed input bindings and hashes'),
 'counter-isolation':('summarize-nr-counter-isolation.py','offline','C14 intervention results, not a production fix'),
 'audit-runtime':('audit-nr-state-runtime.py','offline','Read-only runtime identity and candidate references'),
 'audit-state-access':('audit-nr-state-access.py','offline','Read-only candidate state offsets'),
 'storage':('audit-workspace-storage.py','offline','Workspace metadata inventory; no deletion'),
 'gpu-feedback':('run-shared-feedback-probe.py','gpu','Archived owned-host experiment; explicit runtime hashes and bounded runs'),
 'gpu-resources':('run-nr-resource-probe.py','gpu','Owned-host resource measurements, no gameplay claims'),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--allow-gpu',action='store_true');p.add_argument('action',nargs='?',choices=sorted(ACTIONS));p.add_argument('arguments',nargs=argparse.REMAINDER);a=p.parse_args()
 if not a.action:
  print(json.dumps({k:dict(script=v[0],execution=v[1],scope=v[2]) for k,v in ACTIONS.items()},ensure_ascii=False,indent=2));return 0
 file,kind,_=ACTIONS[a.action]
 if kind=='gpu' and not a.allow_gpu:p.error('GPU research requires explicit --allow-gpu before the action; it is not routine diagnostics')
 args=a.arguments[1:] if a.arguments[:1]==['--'] else a.arguments
 return subprocess.call([sys.executable,'-X','utf8',str(ROOT/file),*args])
if __name__=='__main__':sys.exit(main())
