#pragma once

#include "FaderTakeover.h"
#include "ofParameter.h"

// Ableton-style value-scaling fader takeover applied to an ofParameter<float>.
// Reproduces the shape used by the MIDI controllers (pre-normalise the CC to
// [0,1], scale against the current param position, write back). The span==0
// guard is the FIX: a zero-width param range would previously compute
// (value-min)/0 = NaN; now it treats the position as 0 (mirrors normOf).
inline void applyPickup(ofParameter<float>& param, int ccValue, float& lastMidiValue) {
  const float min = param.getMin();
  const float max = param.getMax();
  const float span = max - min;
  const float normalized = static_cast<float>(ccValue) / 127.0f;
  const float p = (span != 0.0f) ? (param.get() - min) / span : 0.0f;
  const float newP = FaderTakeover::valueScale(normalized, lastMidiValue, p);
  param.set(min + newP * span);
}
