#pragma once

#include "ofxMarkSynth.h"

// Single source of truth for the "memory bank is ready" threshold and the LED
// dim factor shared by the pad grid (ApcMini) and the iPad (Osc). The per-surface
// DIM APPLICATION stays separate; only the constants + predicate are shared.
namespace MemoryReadyPolicy {
inline constexpr int kReadyThreshold = 3;
inline constexpr float kDimFactor = 0.25f;
// NOTE: parameter is a non-const reference because Synth::getMemoryBankController()
// has no const overload; callers pass *synthPtr (a non-const Synth&), so this
// binds without a copy and is behaviour-identical to the inlined predicate.
inline bool isReady(ofxMarkSynth::Synth& s) {
  return s.getMemoryBankController().getMemoryBank().getFilledCount() >= kReadyThreshold;
}
}  // namespace MemoryReadyPolicy
