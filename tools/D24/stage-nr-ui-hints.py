"""Stage NR-only UI annotations and dictionary additions as minimal patches."""
import json, subprocess
from pathlib import Path
r=Path('E:/DLSSNR'); out=r/'builds/D18_014'; src=r/'workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler'
hints=json.loads((out/'nr-hints.json').read_text(encoding='utf-8'))
dictionary=json.loads((src/'menu/D18Translations.zh-CN.json').read_text(encoding='utf-8'))
for en,zh in hints.values():
 if en not in dictionary:dictionary[en]=zh
(out/'D18Translations.zh-CN.json').write_text(json.dumps(dictionary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
table='\n'.join('    {'+json.dumps(k)+', '+json.dumps(v[0])+'},' for k,v in hints.items())
header='''#pragma once
#include "D18Localization.h"
#include <imgui/imgui_internal.h>

// Only the NR panel calls these wrappers. Annotation items must not replace
// LastItemData: deferred sliders commit on IsItemDeactivatedAfterEdit().
namespace D18NrUi {
struct Hint { const char* label; const char* text; };
inline constexpr Hint hints[] = {
TABLE
};
inline void Annotate(const char* label) {
    std::string_view key(label); key=key.substr(0,key.find("##"));
    const char* hint=nullptr;
    for (const auto& entry:hints) if(key==entry.label){hint=D18Ui::Tr(entry.text);break;}
    if(!hint)return;
    auto& context=*ImGui::GetCurrentContext();
    const auto original=context.LastItemData;
    const auto* window=context.CurrentWindow;
    const float right=window->WorkRect.Max.x;
    const float start=original.Rect.Max.x+ImGui::GetStyle().ItemSpacing.x;
    const float width=ImGui::CalcTextSize(hint).x+ImGui::GetFontSize();
    // Leave room for the existing (?) tooltip marker as well.
    if(start+width+ImGui::GetFontSize()*3.0f<=right)ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("\\xc2\\xb7 %s",hint);
    ImGui::PopTextWrapPos();
    context.LastItemData=original;
}
#define D18_NR_HINT_WIDGET(name) template<class... A> inline bool name(const char* label,A&&... args) { \\
    const bool changed=D18Ui::name(label,std::forward<A>(args)...); Annotate(label); return changed; }
D18_NR_HINT_WIDGET(Checkbox)
D18_NR_HINT_WIDGET(SliderFloat)
D18_NR_HINT_WIDGET(SliderInt)
D18_NR_HINT_WIDGET(Combo)
D18_NR_HINT_WIDGET(Button)
#undef D18_NR_HINT_WIDGET
}
'''.replace('TABLE',table)
(out/'D18NrHints.h').write_text(header,encoding='ascii')
menu=(src/'dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8-sig')
menu=menu.replace('#include <menu/D18Localization.h>','#include <menu/D18NrHints.h>')
for name in ('Checkbox','SliderFloat','SliderInt','Combo','Button'):
 menu=menu.replace('D18Ui::'+name+'(', 'D18NrUi::'+name+'(')
(out/'DlssNr_Menu.hints.cpp').write_text(menu,encoding='utf-8')
for target,stage,name in [(src/'menu/D18Translations.zh-CN.json',out/'D18Translations.zh-CN.json','nr-hint-dictionary.patch'),(src/'dlssnr/DlssNr_Menu.cpp',out/'DlssNr_Menu.hints.cpp','nr-hint-menu.patch')]:
 subprocess.run(['python',str(r/'tools/D24/stage-file-diff.py'),str(target),str(stage),str(out/name)],check=True)
print(len(hints),'NR annotations staged')
