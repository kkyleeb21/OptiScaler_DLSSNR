"""Generate only D18 patch-site hashes from the installer manifest; no host-region allowlist."""
from pathlib import Path
import json,base64,hashlib,argparse
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
r=Path(__file__).parent
manifest=r.parents[2]/'community/d18-installer/runtime_patch.json'
patch=json.loads(manifest.read_text(encoding='utf-8-sig'))
out=['#pragma once','namespace D18RuntimeGuard {','inline constexpr const char* Rule="d18-patch-sites-v1";']
sites=[]
for i,h in enumerate(patch['hunks']):
 values=[base64.b64decode(h['expected_base64']),base64.b64decode(h.get('replacement_base64',''.join(h.get('replacement_base64_parts',[]))))]
 values.extend(base64.b64decode(v) for v in h.get('compatible_input_base64',[]))
 values=list(dict.fromkeys(values))
 out.append(f'inline constexpr Variant Variants{i}[]={{')
 out.extend('{%du,"%s"},'%(len(v),hashlib.sha256(v).hexdigest()) for v in values);out.append('};')
 sites.append('{%du,Variants%d,sizeof(Variants%d)/sizeof(Variant)},'%(h['offset'],i,i))
out+=['inline constexpr Site Sites[]={']+sites+['};','}']
# Git checkouts can use CRLF: provenance hashes normalized UTF-8/LF JSON text.
outputs={'patch-sites.generated.h':'\n'.join(out)+'\n','patch-sites.json':json.dumps({'rule':'d18-patch-sites-v1','manifest_sha256':hashlib.sha256(manifest.read_text(encoding='utf-8-sig').encode('utf-8')).hexdigest(),'manifest_hash_encoding':'utf8-no-bom-lf','site_count':len(sites),'native_patch_rva':'0x20f2c','no_host_region_fingerprints':True},indent=2)}
for name,content in outputs.items():
 if args.check:
  if (r/name).read_text()!=content:raise SystemExit('Stale generated file: '+name+'; run generate-patch-sites.py')
 else:(r/name).write_text(content,encoding='ascii')
print('Verified' if args.check else 'Generated',len(sites),'patch sites')
