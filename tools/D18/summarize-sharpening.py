"""Read bounded SH0 observations through existing native API adapters."""
import argparse,importlib.util,json
from pathlib import Path

def summarize(path,api):
    name='summarize-native-nr' if api=='DX11' else 'summarize-vulkan-nr'
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter)
    result=adapter.summarize(path)
    return dict(schema='d18-sh0-summary-v1',api=api,source_sha256=result['sha256'],
                **result['sharpening'],gameplay_verified=False,
                boundary='Bounded state-change observations only. Applied means dispatch recorded, not GPU completion or correct appearance. Missing observations are unknown; diagnostics-off and uninstrumented advanced paths must not be reported as successful execution.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--api',choices=['DX11','Vulkan'],required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(summarize(a.log,a.api),indent=2),encoding='utf-8')
