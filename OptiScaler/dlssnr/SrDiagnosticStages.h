#pragma once
// Shared dependency-safe stages. API adapters own implementation and GPU drain.
namespace DlssNr::DiagnosticStages {
enum Stage : unsigned { Observe, StateOnly, Prepare, Create, Evaluate, OriginalWriteback, Full, CopyOnly, OffscreenReplay, Count };
inline const char* Name(unsigned s){
 switch(s){case Observe:return "0 - Observe inputs only";case StateOnly:return "1 - Manual snapshot / reset / restore";
 case Prepare:return "2 - Prepare inputs (no NGX)";case Create:return "3 - Create NGX feature (no evaluate)";
 case Evaluate:return "4 - Evaluate SR (no writeback)";case OriginalWriteback:return "5 - Original image writeback (no NGX)";
 case Full:return "6 - Full SR and writeback";case CopyOnly:return "7 - Copy original only (no draw)";case OffscreenReplay:return "8 - Replay offscreen (no game writeback)";default:return "Unknown";}
}
inline bool NeedsPrepare(unsigned s){return s==Prepare||s==Create||s==Evaluate||s==Full;}
inline bool NeedsNgx(unsigned s){return s==Create||s==Evaluate||s==Full;}
inline bool NeedsEvaluate(unsigned s){return s==Evaluate||s==Full;}
}
