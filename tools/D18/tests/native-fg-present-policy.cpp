#include "dlssnr/NativeFgPresentPolicy.h"
#include <cassert>
#include <cstdio>
int main() {
 using P=DlssNr::NativeFgPresentPolicy;using D=P::Decision;
 P p;
 assert(p.Observe(0,0,0,0)==D::Ready);
 assert(p.Observe(8,0x087A0007u,0,0)==D::PresentRetry); // MODE_CHANGED, e.g. settings transition
 assert(p.Observe(25,0,0,0)==D::Ready && !p.waiting); // next 60 Hz frame can resume
 assert(p.Observe(30,0x087A0001u,0,0)==D::PresentRetry);
 assert(p.Observe(30000,0x087A0001u,0,0)==D::PresentRetry); // occlusion is not a GPU timeout
 assert(p.Observe(30016,0,0,0)==D::Ready);
 assert(p.Observe(30020,0x887A000Au,0,0)==D::PresentRetry); // WAS_STILL_DRAWING
 assert(p.Observe(30036,0,0,0)==D::Ready);
 assert(p.Observe(31000,0,1,0)==D::RuntimeRetry);
 assert(p.Observe(31016,0,0,0)==D::Ready);
 assert(p.Observe(32000,0,0,8)==D::RuntimeRetry);
 assert(p.Observe(32016,0,0,0)==D::Ready);
 assert(p.Observe(33000,0,1,0)==D::RuntimeRetry);
 assert(p.Observe(34999,0,0,8)==D::RuntimeRetry); // changing error must not extend the timer
 assert(p.Observe(35000,0,2,0)==D::RuntimeExpired);
 assert(p.Observe(36000,0x887A0005u,0,0)==D::PresentFailed); // device removed
 assert(p.Observe(36001,0x80004005u,0,0)==D::PresentFailed);
 puts("present policy: settings, long occlusion, backpressure, query/status recovery and hard failures passed");
}
