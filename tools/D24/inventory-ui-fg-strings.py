import json,re
from pathlib import Path
root=Path(r'E:\DLSSNR');work=root/'workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler'
text=(work/'menu/menu_common.cpp').read_text(encoding='utf-8-sig');a=text.index('void MenuCommon::RenderFrameGenerationSelection');b=text.index('void MenuCommon::RenderD18DlssSrSettings',a)
known=json.loads((work/'menu/D18Translations.zh-CN.json').read_text(encoding='utf-8'));values=[]
for m in re.finditer(r'"(?:\\.|[^"\\])*"(?:\s*"(?:\\.|[^"\\])*")*',text[a:b]):
 try:s=''.join(json.loads(t.group()) for t in re.finditer(r'"(?:\\.|[^"\\])*"',m.group())).split('##')[0]
 except ValueError:continue
 if s and s not in known and s not in values and re.search('[A-Za-z]',s):values.append(s)
(root/'builds/D18_014/ui-fg-strings.json').write_text(json.dumps(values,ensure_ascii=False,indent=2),encoding='utf-8')
for i,s in enumerate(values):print(f'{i}: {s}')
