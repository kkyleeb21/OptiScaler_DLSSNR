"""Exercise the production function resolver with own, foreign, and missing targets."""
from pathlib import Path
import argparse
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
text = a.source.read_text()
start = text.index('    static void* ResolveOwnedDlssgFunction(')
end = text.index('\n    }', start) + len('\n    }')
body = text[start:end]
cpp = a.output / 'owned-fg-routing.cpp'
cpp.write_text(r'''
#include <cstring>
#include <iostream>
class StreamlineHooks {
public:
    static int hkslDLSSGSetOptions(int) { return 0; } // game-FG suppression
    static int hkslDLSSGGetState(int) { return 1; }
    static int originalOptions(int requested) { return requested; }
    static int originalState(int) { return 3; }
    inline static int (*o_slDLSSGSetOptions)(int) = originalOptions;
    inline static int (*o_slDLSSGGetState)(int) = originalState;
''' + body + r'''
};
int foreignOptions(int n) { return n + 10; }
int main() {
    using H = StreamlineHooks;
    using Fn = int(*)(int);
    int failures = 0;
    auto check = [&](bool ok) { if (!ok) ++failures; };
    auto own = (void*)&H::hkslDLSSGSetOptions;
    check(H::hkslDLSSGSetOptions(3) == 0);
    auto resolved = H::ResolveOwnedDlssgFunction("slDLSSGSetOptions", own);
    check(resolved == (void*)&H::originalOptions);
    check(((Fn)resolved)(3) == 3);
    auto state = H::ResolveOwnedDlssgFunction("slDLSSGGetState", (void*)&H::hkslDLSSGGetState);
    check(state == (void*)&H::originalState && ((Fn)state)(0) == 3);
    check(H::ResolveOwnedDlssgFunction("slDLSSGSetOptions", (void*)&foreignOptions) == (void*)&foreignOptions);
    check(H::ResolveOwnedDlssgFunction("different", own) == own);
    check(H::ResolveOwnedDlssgFunction("slDLSSGGetState", own) == own);
    H::o_slDLSSGSetOptions = nullptr;
    check(H::ResolveOwnedDlssgFunction("slDLSSGSetOptions", own) == nullptr);
    check(H::ResolveOwnedDlssgFunction("slDLSSGSetOptions", nullptr) == nullptr);
    std::cout << "9 routing checks, failures=" << failures << '\n';
    return failures ? 1 : 0;
}
''')
envtext = subprocess.check_output(['cmd', '/c', r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'], text=True)
env = {k.upper(): v for k, v in (line.split('=', 1) for line in envtext.splitlines() if '=' in line and not line.startswith('='))}
exe = a.output / 'owned-fg-routing.exe'
subprocess.run([r'C:\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe', '/nologo', '/EHsc', '/std:c++20', str(cpp), '/Fe:' + str(exe), '/Fo:' + str(a.output / 'test.obj')], env=env, check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
