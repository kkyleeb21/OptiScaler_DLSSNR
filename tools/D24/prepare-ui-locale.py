"""Inventory D18 UI strings, retaining C++ adjacent-literal concatenation."""
import re,json
from pathlib import Path
root=Path(r'E:\DLSSNR');work=root/'workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler'
text=(work/'menu/menu_common.cpp').read_text(encoding='utf-8-sig')
parts=[(work/'dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8-sig'),(work/'dlssnr/NativeControl.h').read_text(encoding='utf-8-sig')]
for start,end in [('static const char* D18HealthName','static const char* D18ApiName'),('void MenuCommon::RenderD18StatusDashboard','void MenuCommon::RenderMainMenuHeaderMessages'),('void MenuCommon::RenderD18DlssSrSettings','void MenuCommon::RenderMainMenuSettings')]:
 a=text.index(start)
 b=text.find(end,a+len(start))
 if b<0:b=text.index('void MenuCommon::RenderSettingsMenu',a) if 'void MenuCommon::RenderSettingsMenu' in text[a:] else a+25000
 parts.append(text[a:b])
pattern=r'"(?:\\.|[^"\\])*"(?:\s*"(?:\\.|[^"\\])*")*'
values=[]
for part in parts:
 part=re.sub(r'//[^\n]*','',part)
 for match in re.finditer(pattern,part):
  try:s=''.join(json.loads(x.group()) for x in re.finditer(r'"(?:\\.|[^"\\])*"',match.group()))
  except ValueError:continue
  s=s.split('##')[0]
  if re.search('[A-Za-z]',s) and s not in values:values.append(s)
out=root/'builds/D18_014/ui-strings.json';out.write_text(json.dumps(values,ensure_ascii=False,indent=2),encoding='utf-8')
for i,s in enumerate(values):print(f'{i}: {s}')
