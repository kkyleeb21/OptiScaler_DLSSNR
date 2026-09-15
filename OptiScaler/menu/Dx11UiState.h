#pragma once
#include <hooks/D18ContextTracking.h>
#include <hooks/D18ContextTransaction.h>
#include <hooks/D18Dx11ManualState.h>
// Compatibility facade; shared snapshot owns all captured references.
class Dx11UiState {
 public:
 void Reset(){}
 class Scope {
  D18ContextTransaction::Scope transaction;
  D18InputProbe::ScopedInternalContext tracking;
  D18Dx11ManualState::Snapshot snapshot;
  HRESULT result;
 public:
  Scope(Dx11UiState&,ID3D11DeviceContext* c):transaction(c,1),result(transaction?snapshot.Capture(c):E_PENDING){if(SUCCEEDED(result))snapshot.Reset();}
  explicit operator bool()const{return SUCCEEDED(result);}
  HRESULT Result()const{return result;}
 };
};
