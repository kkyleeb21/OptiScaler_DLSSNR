"""One-time source migration; preserve every existing control/config binding."""
from pathlib import Path
import re

p = Path(r'E:\DLSSNR\workspace\dlss5\worktrees\d18-012-re-integration\OptiScaler\dlssnr\DlssNr_Menu.cpp')
s = p.read_text(encoding='utf-8-sig')
def at(t):
    assert s.count(t) == 1, t
    return s.index(t)
def part(a,b): return s[at(a):at(b)]
diag='        ImGui::SeparatorText("Diagnostics");'
status='        if (native)\n        {\n            if(!vulkan)'
enlarge='        const bool reducedPhysical'
sampling='        ImGui::SeparatorText("Runtime sampling");'
compose='        ImGui::SeparatorText("Composition");'
detail='        float detail ='
colour='        float colour ='
frequency='        if (native) ImGui::TextDisabled("Frequency reconstruction'
model='        if (ImGui::TreeNodeEx("Model tuning"'
debug='        if (ImGui::TreeNodeEx("Debug & calibration"'
encoding='            ImGui::BeginDisabled(!vulkan);'
freqadv='            ImGui::BeginDisabled(native || !experimentalCompose);'
capture='            ImGui::BeginDisabled(native);'
exposure='            const auto nativeExposure='
debugview='            ImGui::BeginDisabled(false);'
compare='            static const char* compareNames[]'
nativecapture='            if (native)\n'
end='        ImGui::PopItemWidth();'
def section(name,hint,body):
    return '\n        if (ImGui::TreeNodeEx("'+name+'", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))\n        {\n            ImGui::TextWrapped("'+hint+'");\n'+body+'            ImGui::TreePop();\n        }\n'
def advanced(name,body):
    return '        if (ImGui::TreeNode("'+name+'"))\n        {\n'+body+'            ImGui::TreePop();\n        }\n'
clarity = part(detail,colour)
clarity += advanced('Advanced detail controls',part(enlarge,sampling)+part(sampling,compose).replace('        ImGui::SeparatorText("Runtime sampling");\n','')+part(frequency,model)+part(freqadv,capture).replace('!experimentalCompose','!config->DlssNrExperimentalCompose.value_or_default()'))
colours = part(colour,frequency)+advanced('Advanced colour and brightness',part(encoding,freqadv)+part(exposure,debugview))
models = part(model,debug)
models = models[models.index('            ImGui::TextDisabled'):models.rindex('            ImGui::TreePop();')]
comparisons=part(compare,nativecapture)
# The old BeginDisabled(false) enclosed debug + compare. Remove its matching close here.
comparisons=comparisons.rsplit('            ImGui::EndDisabled();',1)[0]
diagnostics=part(diag,status)+part(debugview,compare)+'            ImGui::EndDisabled();\n'+part(capture,exposure)
nc=part(nativecapture,end)
diagnostics+=nc[:nc.rindex('            ImGui::TreePop();')]
out=s[:at(diag)]+part(status,enlarge)
out+=section('Clarity and detail','Soft image? Start with Detail strength; sampling controls are under Advanced.',clarity)
out+=section('Colour and brightness','Colour shift? Start with Colour strength or transfer only model colour changes.',colours)
out+=section('Characters and style','Adjust skin, local structure and the overall model style.',models)
out+=section('Compare the result','Compare NR against the original image in the same scene.',comparisons)
out+=advanced('Advanced and diagnostics',diagnostics)+s[at(end):]
before=set(re.findall(r'config->(DlssNr\w+)',s));after=set(re.findall(r'config->(DlssNr\w+)',out))
assert before==after,(before-after,after-before)
assert out.count('ImGui::BeginDisabled(')==out.count('ImGui::EndDisabled();')
Path(r'E:\DLSSNR\backups\D24-menu-before-goal-order-20260908.cpp').write_text(s,encoding='utf-8')
p.write_text(out,encoding='utf-8')
print(f'Preserved {len(before)} NR config fields; disabled scopes balanced.')
