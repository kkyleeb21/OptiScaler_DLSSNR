"""Stage minimal hunks only for D18 UI functions; never rewrite source files."""
import re,difflib
from pathlib import Path
root=Path(r'E:\DLSSNR');work=root/'workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler'
widgets=r'ImGui::(TextUnformatted|TextWrapped|TextDisabled|TextColored|Text|SeparatorText|Checkbox|SliderFloat|SliderInt|InputFloat|InputInt|RadioButton|Button|SmallButton|TreeNodeEx|TreeNode|BeginCombo|Combo|Selectable|CalcTextSize)\('
def span(text,marker):
 start=text.index(marker);masked=re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',lambda m:' '*len(m.group()),text)
 opening=masked.index('{',start);depth=1;end=opening+1
 while depth:
  if masked[end]=='{':depth+=1
  elif masked[end]=='}':depth-=1
  end+=1
 return start,end
for relative in ['menu/menu_common.cpp','dlssnr/DlssNr_Menu.cpp']:
 path=work/relative;original=path.read_text(encoding='utf-8-sig');text=original
 if relative.startswith('menu'):
  markers=['static void RenderD18Indicator','static void RenderD18PipelineNode','void MenuCommon::RenderD18StatusDashboard','void MenuCommon::RenderD18Diagnostics','void MenuCommon::RenderD18DlssSrSettings','void MenuCommon::RenderD18DlssFgSettings','void MenuCommon::RenderD18SharpnessSettings','void MenuCommon::RenderMainMenuTable','void MenuCommon::RenderMainMenuBottomBar','class Keybind']
  markers += ['void MenuCommon::ShowTooltip','void MenuCommon::SeparatorWithHelpMarker','void MenuCommon::PopulateCombo','void MenuCommon::AddDLSSRenderPreset','void MenuCommon::RenderFrameGenerationSelection','void MenuCommon::RenderFrameGenerationRuntimeSettings']
  for marker in markers:
   a,b=span(text,marker);text=text[:a]+re.sub(widgets,r'D18Ui::\1(',text[a:b])+text[b:]
  # Format translation is restricted to the D18 dashboard/diagnostics/status paths.
  for marker in markers:
   a,b=span(text,marker);text=text[:a]+text[a:b].replace('StrFmt(', 'D18Ui::Format(')+text[b:]
 else:
  text=text.replace('#include "NativeControl.h"','#include "NativeControl.h"\n#include <menu/D18Localization.h>')
  text=re.sub(widgets,r'D18Ui::\1(',text)
 diff=list(difflib.unified_diff(original.splitlines(),text.splitlines(),n=3))
 hunks=['@@' if line.startswith('@@') else line for line in diff[2:]]
 patch='*** Begin Patch\n*** Update File: '+str(path)+'\n'+'\n'.join(hunks)+'\n*** End Patch\n'
 (root/'builds/D18_014'/('locale-'+path.stem+'.patch')).write_text(patch,encoding='utf-8')
 print(relative,'changed lines:',sum(l.startswith(('+','-')) for l in diff[2:]))
