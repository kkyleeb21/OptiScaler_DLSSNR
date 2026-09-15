"""Create an isolated SR-to-NR candidate from the live-verified V33 baseline."""
from pathlib import Path
import shutil

root = Path('E:/DLSSNR')
base = root/'builds/D18_Wildlands_DlaaV33_20260914'
target = root/'builds/D18_Wildlands_NrV34_20260914'
target.mkdir(exist_ok=True)
if not (target/'core-source').exists():
    shutil.copytree(base/'core-source', target/'core-source')
for name in ('build-core.py','sr-host.cpp','test-sr.py','prepare-fixture.py'):
    if not (target/name).exists(): shutil.copy2(base/name,target/name)
for source in base.glob('*.cso'): shutil.copy2(source,target/source.name)
native = root/'builds/D18_RuntimeGuards_20260911/source/tools/D24'
for name in ('bg3-native','runtime-guard'):
    if not (target/'native-source'/name).exists():
        shutil.copytree(native/name,target/'native-source'/name)
print(target)
