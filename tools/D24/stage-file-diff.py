"""Create reviewable minimal apply_patch hunks from a staged text file."""
import sys,difflib
from pathlib import Path
target,stage,out=map(Path,sys.argv[1:])
old=target.read_text(encoding='utf-8-sig');new=stage.read_text(encoding='utf-8-sig')
diff=list(difflib.unified_diff(old.splitlines(),new.splitlines(),n=3))
hunks=['@@' if x.startswith('@@') else x for x in diff[2:]]
out.write_text('*** Begin Patch\n*** Update File: '+str(target)+'\n'+'\n'.join(hunks)+'\n*** End Patch\n',encoding='utf-8')
print('Changed lines:',sum(x.startswith(('+','-')) for x in diff[2:]))
