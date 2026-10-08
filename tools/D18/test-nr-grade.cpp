// Pure CPU fixture: no Windows/GPU/runtime DLL is loaded.
#include <dlssnr/NrGradeTable.h>
#include <cassert>
#include <limits>
#include <fstream>
#include <iostream>
using namespace DlssNr::Grade;
struct FakeMemory {
    uintptr_t current=1;
    std::array<Table,3> tables{Original(),Original(),Original()};
    bool supported=true,failWrite=false;
    unsigned calls=0,pins=0,unpins=0,stores=0;
    uintptr_t Current(){++calls;return current;}
    bool Pin(uintptr_t){++calls;++pins;return true;}
    void Unpin(uintptr_t){++calls;++unpins;}
    bool Identity(uintptr_t){++calls;return supported;}
    bool Read(uintptr_t base,Table& t){++calls;t=tables[base];return true;}
    bool Write(uintptr_t base,const Table& t){++calls;++stores;tables[base]=t;return !failWrite;}
};
static bool same(const Table& a,const Table& b){return std::memcmp(a.data(),b.data(),sizeof(a))==0;}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--dump") {
        std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(ExpectedWords.data()),sizeof(Table));return out?0:1;
    }
    const auto original=Original();
    const auto none=Pack(Defaults,1);
    for(unsigned i=0;i<3;++i){assert(none[i].enabled==1);assert(none[i].style==(i==2?0u:i+1));assert(none[i].mask==0);}
    for(unsigned i=3;i<8;++i)assert(std::memcmp(&none[i],&original[i],sizeof(Row))==0);
    const auto natural=Pack(Preset(1),1),cinema=Pack(Preset(2),1);
    assert(std::memcmp(&natural[0],&original[0],sizeof(Row))==0);
    assert(std::memcmp(&cinema[1],&original[1],sizeof(Row))==0);
    for(unsigned i=0;i<3;++i) {assert(natural[i].mask==0x34);assert(cinema[i].mask==0x20);}
    auto v=Maximum;
    for(float t:{0.f,.049f,.05f,.25f,.5f,1.f,2.f,-1.f}) {
        const auto packed=Pack(v,t);assert(packed[0].mask==0x3fff);
        for(size_t i=0;i<14;++i) {
            assert(std::isfinite(packed[0].values[i]));
            if(Tone(t)>=MinimumTone) {
                const auto output=(packed[0].values[i]-Defaults[i])*Tone(t)+Defaults[i];
                assert(std::abs(output-v[i])<1e-5f);
            } else assert(packed[0].values[i]==Pack(v,MinimumTone)[0].values[i]);
        }
    }
    v=Defaults;v[1]=1.5f;v[2]=-.1f;
    const auto half=Pack(v,.5f);assert(half[0].values[1]==2.f);assert(half[0].values[2]==-.2f);
    assert(half[0].mask==((1u<<1)|(1u<<2)));
    v[0]=std::numeric_limits<float>::quiet_NaN();v[1]=std::numeric_limits<float>::infinity();v[2]=-999;v[3]=999;
    const auto clipped=Sanitize(v);assert(clipped[0]==0 && clipped[1]==1 && clipped[2]==-2 && clipped[3]==1);
    assert(Tone(std::numeric_limits<float>::quiet_NaN())==0);
    FakeMemory m;Session s;
    for(unsigned i=0;i<100;++i)s.Update(false,Preset(1),.5f,m);
    assert(m.calls==0 && s.reads==0 && s.writes==0 && s.disabledBypasses==100);
    s.Update(true,Preset(1),1,m);assert(s.active && s.status==Status::Applied && m.stores==1 && m.pins==1);
    assert(same(m.tables[1],natural));
    s.Update(true,Preset(1),1,m);assert(m.stores==1); // unchanged values: no write
    s.Update(true,Preset(1),.5f,m);assert(m.stores==2 && m.tables[1][0].values[2]==-.2f);
    s.Update(false,Defaults,1,m);assert(!s.active && !s.base && m.unpins==1 && same(m.tables[1],original));
    const auto dormant=m.calls;s.Update(false,Defaults,1,m);assert(m.calls==dormant);
    // Unsupported bytes/version never make memory writable.
    m.tables[1][7].values[13]=.1f;s.Update(true,Preset(1),1,m);
    assert(s.status==Status::Unsupported && m.stores==3 && m.pins==m.unpins);
    s.Update(false,Defaults,1,m);m.tables[1]=original;m.supported=false;
    s.Update(true,Preset(1),1,m);assert(s.status==Status::Unsupported && m.stores==3);
    s.Update(false,Defaults,1,m);m.supported=true;
    // Changed base restores the old image before validating/writing the new image.
    s.Update(true,Preset(2),1,m);m.current=2;s.Update(true,Preset(1),.5f,m);
    const auto lowWrites=m.stores;s.Update(true,Preset(1),.01f,m);assert(m.stores==lowWrites);
    assert(same(m.tables[1],original) && s.base==2 && s.status==Status::LowTone);
    // Failed write/protection cleanup retains the backup and module reference.
    m.failWrite=true;s.Update(false,Defaults,1,m);assert(s.active && s.base==2 && s.status==Status::WriteFailed);
    m.failWrite=false;s.Update(false,Defaults,1,m);assert(same(m.tables[2],original) && m.pins==m.unpins);
    // S1 audit: successful A -> B bytes changed but cleanup failed -> A again.
    auto a=Defaults;a[2]=.2f;auto b=Defaults;b[2]=.4f;
    s.Update(true,a,1,m);m.failWrite=true;s.Update(true,b,1,m);
    assert(s.pending && s.status==Status::WriteFailed && m.tables[2][0].values[2]==.4f);
    const auto failedWrites=m.stores;s.Update(true,a,1,m);
    assert(s.status==Status::WriteFailed && s.pending && m.stores==failedWrites+1);
    m.failWrite=false;s.Update(true,a,1,m);
    assert(s.status==Status::Applied && !s.pending && m.tables[2][0].values[2]==.2f);
    // Invalid compensation keeps the last legal table, never publishes Applied.
    auto bad=Defaults;bad[1]=.5f;const auto legal=m.tables[2];const auto count=m.stores;
    for(float t:{.5f,.05f}){s.Update(true,bad,t,m);assert(s.status==Status::InvalidValues && same(m.tables[2],legal) && m.stores==count);}
    m.failWrite=true;s.Update(true,b,1,m);assert(s.pending);
    const auto dirtyWrites=m.stores;s.Update(true,bad,.5f,m);
    assert(s.pending && s.status==Status::WriteFailed && m.stores==dirtyWrites+1);
    m.failWrite=false;s.Update(true,bad,.5f,m);
    assert(!s.pending && s.status==Status::InvalidValues && same(m.tables[2],legal));
    const auto cleanWrites=m.stores;
    s.Update(true,a,.049f,m);assert(s.status==Status::LowTone && m.stores==cleanWrites);
    s.Update(true,a,.05f,m);assert(s.status==Status::Applied && m.tables[2][0].values[2]==4.f);
    s.Update(true,a,2.f,m);assert(s.status==Status::Applied && m.tables[2][0].values[2]==.2f);
    bad=Defaults;bad[0]=.5f;bad[1]=.5f;s.Update(true,bad,1,m);assert(s.status==Status::InvalidValues);
    auto nonfinite=Pack(Defaults,1);nonfinite[0].values[2]=std::numeric_limits<float>::infinity();assert(!Legal(nonfinite));
    nonfinite=Pack(Defaults,1);nonfinite[0].values[1]=MinimumWhiteGap*.5f;assert(!Legal(nonfinite));
    nonfinite[0].values[1]=MinimumWhiteGap;assert(Legal(nonfinite));
    s.Update(false,Defaults,1,m);
    // G6 audit: rejected module replaced by a valid image at exactly the same base.
    m.supported=false;s.Update(true,a,1,m);assert(s.status==Status::Unsupported);
    m.supported=true;s.Update(true,a,1,m);assert(s.status==Status::Applied);
    s.Update(false,Defaults,1,m);
    const auto calls=m.calls;s.Update(false,Defaults,1,m);assert(m.calls==calls);
    m.current=0;s.Update(true,Defaults,1,m);assert(s.status==Status::Waiting && !s.active);
    std::cout<<"PASS S1 audit A -> failed B -> A; G4 white=0/negative, .049/.05 and tone=2; G6 same-base replacement\n";
    std::cout<<"PASS: packing, presets, all masks, compensation, bounds, dormant zero access, identity gates, restore, reload, failure retry\n";
}
