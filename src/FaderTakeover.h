#pragma once

#include <cmath>

// Ableton-style "value scaling" fader takeover. Replaces hard pickup ("gate
// until fader crosses the param") with proportional convergence: the fader's
// movement is scaled against the runway remaining on the side it's moving.
// Properties:
//   - Pulling the fader down immediately ducks the param, reaching 0 at the
//     bottom (safe live "swipe to 0" gesture).
//   - At the endpoints (0 or max) fader and param align exactly, then track 1:1.
//   - No "armed" flag needed; reading the param fresh each call means external
//     changes self-correct on the next nudge.
namespace FaderTakeover {

// m, p in [0,1]. mPrev passed by reference: < 0 means "no prior sample" — the
// call just baselines and returns p unchanged. On return, mPrev is updated to m.
inline float valueScale(float m, float& mPrev, float p) {
  constexpr float eps = 1e-5f;
  if (mPrev < 0.0f) { mPrev = m; return p; }
  float newP = p;
  if (m > mPrev + eps) {
    float runway = 1.0f - mPrev;
    newP = (runway > eps) ? p + (m - mPrev) * (1.0f - p) / runway : m;
  } else if (m < mPrev - eps) {
    newP = (mPrev > eps) ? p * (m / mPrev) : m;
  }
  mPrev = m;
  if (newP < 0.0f) newP = 0.0f;
  if (newP > 1.0f) newP = 1.0f;
  return newP;
}

}  // namespace FaderTakeover
