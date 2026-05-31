# Performance-Awareness Rollout — Implementation Plan

> **Status:** Plan only. No implementation yet.
> **Scope of this document:** Extend the existing per-mod adaptive performance-throttling system from its current two-mod footprint (FluidMod, ParticleSetMod) to a reusable helper plus a wider set of heavy mods.
> **Explicitly out of scope here:** the cross-fade-between-configs work and any "feed-forward transition pressure" idea. Those are tracked in a separate thread. This work is a *standalone prerequisite* that happens to make that later work cheaper, but it stands on its own as steady-state overload protection.

All paths below are relative to the addon root:
`/Users/steve/Development/openframeworks/of_v20251123_osx_release/addons/ofxMarkSynth/`
(referenced from the app at `apps/myApps/fingerprint2/`, which lists `ofxMarkSynth` in `addons.make`).

---

## 1. Background: what the system does today

The synth has a closed-loop adaptive throttle. Each frame the `Synth` measures frame-time pressure; mods that opt in ask the synth "how much should I back off right now?" and scale down their own most-expensive knob. It is **reactive** (responds to measured overload), **per-mod** (each mod throttles its own work), and **state-safe** (it reduces work in ways that don't cause visual discontinuities — e.g. fewer solver iterations, not a buffer resize).

### 1.1 The pressure signal — `Synth`

`src/core/Synth.cpp:560-594` — `Synth::updatePerformanceBudget()` (called at the top of `Synth::update()`, `Synth.cpp:550`):

- Maintains an **asymmetric EMA** of `ofGetLastFrameTime()`: fast attack (`emaAlphaAttack = 0.25f`, ~4-frame window) so spikes are caught quickly, slow release (`emaAlphaRelease = 0.05f`, ~20-frame window) so recovery doesn't jitter.
- Computes `performancePressureCurrent` ∈ [0,1] = how far the EMA frame time exceeds the target frame time, clamped so it saturates at 2× target.
- Target comes from `getTargetFrameRate()` (`Synth.hpp:188`).

`src/core/Synth.cpp:596-613` — `Synth::getPerformanceShareFor(const Mod* mod)`:

```cpp
float Synth::getPerformanceShareFor(const Mod* mod) const {
  if (mod == nullptr) return 0.0f;
  if (performancePressureCurrent <= 0.0f) return 0.0f;
  const float myWeight = mod->getPerformanceWeight();
  if (myWeight <= 0.0f) return 0.0f;
  const float curvedPressure = std::sqrt(performancePressureCurrent);
  return std::clamp(curvedPressure * myWeight, 0.0f, 1.0f);
}
```

**Two design facts that matter for the rollout:**

1. **`sqrt(pressure)` curve** — modest pressure produces meaningful throttle; a linear mapping settles with large steady-state error.
2. **No division by total weight.** Adding more aware mods does *not* split a fixed pot thinner — each mod's `weight` is an independent responsiveness multiplier (default `1.0`). Consequence: **`weight` must be calibrated to a mod's real relative GPU cost**, or under pressure everything throttles roughly uniformly instead of shedding the expensive work first. See §6.

Other relevant `Synth` members (`src/core/Synth.hpp`): `getPerformancePressure()` (`:200`), `performancePressureCurrent` (`:265`), EMA state (`:266-267`).

### 1.2 The per-mod contract — `Mod` base

`src/core/Mod.hpp:194-210` — two virtuals, both with non-aware defaults:

```cpp
virtual float getPerformanceWeight() const { return 0.0f; }      // 0 = synth allocates us no share
virtual float getCurrentThrottleFactor() const { return 1.0f; }  // 1 = full quality (drives a GUI warning indicator)
```

There is **no registration** — `Synth` iterates all mods each frame and calls these. A mod becomes "aware" simply by overriding them. There is **no shared base or mixin**; today each aware mod hand-rolls identical boilerplate (this is the thing the rollout fixes — see §3).

### 1.3 The crucial layer-state nuance (carry-over from prior analysis)

A drawing layer has **two independent states**, and only one gates work:

- **Alpha** (per-layer opacity, `LayerController` alpha parameters) is read only at composite time. At `src/rendering/CompositeRenderer.cpp:157` a layer with `alpha == 0` hits `continue` — it is not *drawn*, but the mod upstream **already did its full simulation**. Alpha 0 ≠ work saved.
- **Pause** (`DrawingLayer::pauseState`) *does* gate work. `Mod::getNamedDrawingLayerPtr()` (`src/core/Mod.cpp:305-318`) returns `std::nullopt` when `pauseState == PAUSED`, and every heavy mod early-outs at the top of `update()` when it has no current drawing layer.

This matters here because **the convention for `getPerformanceWeight()` is to return `0` whenever the mod has no live drawing layer** (paused / absent), so the synth allocates it no share when it isn't doing GPU work. The flag that records this is set inside `update()` and read by the `const` weight accessor.

---

## 2. The two existing implementations (the template to generalise)

### 2.1 FluidMod

Header (`src/layerMods/FluidMod.hpp`):
- `:40` `float getPerformanceWeight() const override;`
- `:41` `float getCurrentThrottleFactor() const override { return budgetThrottleFactor; }`
- `:142` `ofParameter<float> performanceWeightParameter { "performanceWeight", 1.0f, 0.0f, 10.0f };`
- `:143` `ofParameter<int> pressureIterationsFloorParameter { "pressureIterationsFloor", 4, 0, 30 };` (mod-specific floor)
- `:148-149` `bool budgetCurrentlyHasDrawingLayer = false; float budgetThrottleFactor = 1.0f;`
- `:151-154` diagnostic state (`budgetDiagCountdown`, `budgetDiagFrames = 120`, `budgetLastAppliedPressureIterations`)

Weight accessor (`src/layerMods/FluidMod.cpp:70-77`):
```cpp
float FluidMod::getPerformanceWeight() const {
  if (!budgetCurrentlyHasDrawingLayer) return 0.0f;   // const path: reads the cached flag
  return std::max(0.0f, performanceWeightParameter.get());
}
```

In `update()` (`src/layerMods/FluidMod.cpp:138-297`):
- `:170-176` early-out + set `budgetCurrentlyHasDrawingLayer` from `getCurrentNamedDrawingLayerPtr(...)`.
- `:188-203` **the shared throttle loop** — pull share, compute `targetThrottle = clamp(1 - share, 0, 1)`, asymmetric rate-limit:
  ```cpp
  float targetThrottle = 1.0f;
  if (auto s = getSynth()) {
    const float share = s->getPerformanceShareFor(this); // [0,1]
    targetThrottle = std::clamp(1.0f - share, 0.0f, 1.0f);
  }
  constexpr float maxShrinkPerFrame = 0.08f;   // fast shrink
  constexpr float maxGrowPerFrame   = 0.02f;   // slow grow
  const float delta = targetThrottle - budgetThrottleFactor;
  if (delta < -maxShrinkPerFrame)   budgetThrottleFactor -= maxShrinkPerFrame;
  else if (delta > maxGrowPerFrame) budgetThrottleFactor += maxGrowPerFrame;
  else                              budgetThrottleFactor = targetThrottle;
  budgetThrottleFactor = std::clamp(budgetThrottleFactor, 0.0f, 1.0f);
  ```
- `:205-229` **mod-specific application** — lerp the throttled knob between a floor and the base value, write it as a `FluidSimulation::ParameterOverrides`. The knob here is **Pressure (Jacobi) iterations**; floor-lerp:
  ```cpp
  const int floorIterations = std::min(pressureIterationsFloorParameter.get(), baseIterations);
  const float throttledF = floorIterations + (baseIterations - floorIterations) * budgetThrottleFactor;
  ```
  At factor `1.0` it clears the override so the user's parameter drives the value.
- `:275-296` periodic VERBOSE diagnostic line.

### 2.2 ParticleSetMod

Header (`src/sinkMods/ParticleSetMod.hpp`): same shape —
- `:38,:41` the two overrides; `:114` `performanceWeightParameter`; `:115-116` two floor params (`connectionRadiusFloor`, `sortNeighborWindowFloor`); `:121,:127` `budgetCurrentlyHasDrawingLayer` / `budgetThrottleFactor`; `:132` `budgetSpawnGateOpen`; `:135-138` diag state.

In `update()` (budget block ~`:92-196`): identical early-out + identical asymmetric rate-limit loop, then applies the throttle to **two** knobs (`connectionRadius` = fill-rate, `sortNeighborWindow` = per-particle work) via floor-lerp, plus a **spawn gate** `budgetSpawnGateOpen = (budgetThrottleFactor > 0.5f)`.

### 2.3 What is identical vs what varies

| Identical (→ extract to helper) | Varies per mod (→ stays in mod) |
|---|---|
| `performanceWeightParameter` declaration & default range | Which knob(s) get throttled |
| `budgetCurrentlyHasDrawingLayer` flag + the "weight 0 when no layer" rule | Floor parameter(s) for those knobs |
| `budgetThrottleFactor` + asymmetric rate-limit (0.08/0.02) + clamp | How the throttled value is written back (override struct / param / spawn gate) |
| `getCurrentThrottleFactor()` returning the factor | Diagnostic detail (which applied values to log) |
| The floor-lerp arithmetic `floor + (base - floor) * factor` | — |

This split is the whole basis of the plan.

---

## 3. Core work item — extract a reusable budget helper

**Problem:** every new aware mod currently re-pays ~40 lines of identical plumbing, and the two copies can already drift (FluidMod and ParticleSetMod both hand-maintain the `0.08/0.02` constants). Rolling out to 4–6 more mods without consolidating multiplies that risk.

**Recommended approach: composition, not inheritance.** A small member struct the mod owns, *not* a base class. Reasons:
- The mods that need this live in different existing hierarchies — `FluidMod`, `SmearMod`, `AdvectOnlyMod` are layer mods; `BlurMod` extends `TextureFilterMod`; `ParticleSetMod`/`ParticleFieldMod` are sink mods. There is no single place to insert a base class without multiple-inheritance gymnastics over the abstract `Mod` base.
- A member helper composes cleanly with any mod regardless of hierarchy and keeps the `const`-correctness of `getPerformanceWeight()` straightforward.

### 3.1 Proposed helper shape (illustrative — not final code)

New files, e.g. `src/core/ModPerformanceBudget.hpp` (+ `.cpp` if needed):

```cpp
// Illustrative shape only — finalise names/signatures during implementation.
namespace ofxMarkSynth {

class Synth;
class Mod;

class ModPerformanceBudget {
public:
  // The weight parameter the mod adds to its own parameter group.
  ofParameter<float>& weightParameter() { return performanceWeightParameter_; }

  // Call once at the top of update() with whether the mod has a live (unpaused) layer.
  // Pulls share from the synth and advances the rate-limited throttle factor.
  void update(Synth* synth, const Mod* self, bool hasLiveDrawingLayer);

  // For getPerformanceWeight(): weight, or 0 when inactive. const-safe (reads cached flag).
  float weight() const {
    return hasLiveDrawingLayer_ ? std::max(0.0f, performanceWeightParameter_.get()) : 0.0f;
  }

  // For getCurrentThrottleFactor().
  float factor() const { return throttleFactor_; }

  // Convenience: lerp a knob between a floor and its base by the throttle factor.
  // Returns base when factor == 1.0 (caller can then skip writing an override).
  template <typename T>
  T applyFloor(T base, T floor) const {
    if (throttleFactor_ >= 1.0f) return base;
    const T f = std::min(floor, base);
    return static_cast<T>(f + (base - f) * throttleFactor_);
  }
  bool isThrottling() const { return throttleFactor_ < 1.0f; }

private:
  ofParameter<float> performanceWeightParameter_ { "performanceWeight", 1.0f, 0.0f, 10.0f };
  bool  hasLiveDrawingLayer_ = false;
  float throttleFactor_ = 1.0f;
};

} // ofxMarkSynth
```

`ModPerformanceBudget::update(...)` encapsulates exactly the FluidMod `:188-203` loop (record `hasLiveDrawingLayer_`, pull `synth->getPerformanceShareFor(self)`, asymmetric rate-limit with the shared `0.08`/`0.02` constants, clamp).

### 3.2 How a mod uses it

```cpp
// header
ModPerformanceBudget budget_;
ofParameter<int> myKnobFloorParameter { "myKnobFloor", /*default*/, /*min*/, /*max*/ };
float getPerformanceWeight() const override { return budget_.weight(); }
float getCurrentThrottleFactor() const override { return budget_.factor(); }

// initParameters()
parameters.add(budget_.weightParameter());
parameters.add(myKnobFloorParameter);

// update()
auto layer = getCurrentNamedDrawingLayerPtr(DEFAULT_DRAWING_LAYER_PTR_NAME);
budget_.update(getSynth().get(), this, layer.has_value());
if (!layer) return;                       // existing early-out semantics preserved
...
int throttledKnob = budget_.applyFloor(baseKnob, myKnobFloorParameter.get());
// write throttledKnob to wherever the expensive work reads it
```

### 3.3 Optional: shared diagnostic

The periodic VERBOSE log (FluidMod `:275-296`) is also boilerplate. Consider a `budget_.shouldLogDiag()` / a helper that formats `weight / pressure / share / factor`. Keep mod-specific applied-value reporting in the mod. Low priority; nice-to-have.

---

## 4. Migrate the two existing mods onto the helper first

Before adding any new mods, port `FluidMod` and `ParticleSetMod` to `ModPerformanceBudget`. This:
- Proves the helper against the two known-good implementations.
- Removes the duplicated constants so they can't drift.
- Must be **behaviour-preserving** — same throttle response, same floor semantics, same GUI throttle indicator. Verify the diagnostic numbers match pre/post migration under forced pressure (§7).

Watch points during migration:
- `FluidMod` writes via `FluidSimulation::ParameterOverrides` and clears the override at factor 1.0 — keep that "clear when not throttling" behaviour (use `budget_.isThrottling()`).
- `ParticleSetMod` has the extra `budgetSpawnGateOpen = factor > 0.5` rule — keep it in the mod (it's mod-specific), driven off `budget_.factor()`.

---

## 5. Rollout to new heavy mods

Add awareness to the genuinely heavy, currently-unthrottled mods, in priority order. For each: add the `budget_` member, add a floor parameter for its knob, wire the two overrides, and apply `budget_.applyFloor(...)` to the knob.

| Priority | Mod | File | Expensive knob to throttle | Notes |
|---|---|---|---|---|
| 1 | **SmearMod** | `src/layerMods/SmearMod.cpp:96-176` | number of feedback / ghost / fold shader passes (`smearShader.render` is called up to 3×, `:150/:162/:171`); and/or grid resolution (`gridSize`, `gridLevels`) | Currently declares weight 0 and just `return`s when no layer (`:109-110`); needs full treatment incl. setting the has-layer flag. Highest-value target — heavy feedback pass run every frame. |
| 2 | **BlurMod** | `src/filterMods/BlurMod.cpp:50-130` | **pass count** — `iterationsParameter`, already clamped to [1,8] at `:95`. Add `iterationsFloor`, throttle passes. Optionally also `radius`. | Cleanest possible candidate: an explicit integer pass count. Extends `TextureFilterMod` — confirm the base `update()` flow plays nicely with adding the budget call. |
| 3 | **ParticleFieldMod** | `src/sinkMods/ParticleFieldMod.cpp` | mirror `ParticleSetMod`: per-particle physics work / count / connection knob | Closely analogous to the already-done `ParticleSetMod`; reuse its floor-param choices. |
| 4 | **AdvectOnlyMod** | `src/layerMods/AdvectOnlyMod.cpp` | advection step count / sub-steps | Lighter than full fluid but still a per-frame GPU advection pass. |
| 5 (opt) | EdgeDetectMod / TextureFilterMod / SandLineMod | `src/filterMods/`, `src/sinkMods/` | kernel size / samples per segment / grain density | Lower value; add only if profiling shows them material. |

For each new mod, identify the **single dominant cost** first (don't throttle five knobs when one matters), and pick a **floor** that keeps the result visually coherent at maximum throttle (the floor is the "ugliest acceptable" setting, not zero).

---

## 6. Weight calibration (do not skip)

Because the share formula does **not** normalise by total weight (§1.1), the `performanceWeight` of each mod must reflect its **real relative GPU cost** for the system to shed expensive work preferentially:

- Default `1.0` is fine only if all aware mods cost roughly the same — they don't (a 7200² fluid sim ≫ a blur pass).
- Suggested method: under controlled forced pressure (§7), measure each mod's contribution to frame time (toggle each mod / use `ofxTimeMeasurements` already in `addons.make`), then set weights so the heaviest mods carry proportionally larger weights and therefore back off first/most.
- Treat this as a tuning pass *after* the mechanical rollout, ideally across a few representative configs (configs vary a lot in which mods dominate).

---

## 7. Verification

No automated harness exists for this; verify by observation:

1. **Force pressure deterministically.** Easiest lever: temporarily lower the synth target frame rate (raises `pressure` without changing workload), or load a known-heavy config. Pressure is visible via `Synth::getPerformancePressure()`.
2. **Watch throttle factors.** `getCurrentThrottleFactor()` already feeds a GUI warning indicator — confirm each newly-aware mod shows throttling under pressure and returns to 1.0 on recovery. The per-mod VERBOSE diag block (FluidMod `:275-296`) is the model; enable with `-v`.
3. **Behaviour-preserving migration check (step §4).** Capture the diag numbers (weight / share / factor / applied knob) for FluidMod + ParticleSetMod before and after the helper migration under identical forced pressure; they should match.
4. **No visual pop.** Confirm throttling changes only continuous/quality knobs (iterations, radius, density) — never anything that resizes a buffer or resets feedback state. The floor-lerp approach guarantees this if knobs are chosen correctly.
5. **Build.** This is an openFrameworks/Xcode project (`fingerprint2.xcodeproj`, `Makefile`). Build via the existing toolchain after each mod is ported; the addon is shared, so a clean build of the app picks up addon changes.

---

## 8. Suggested order of work

1. Write `ModPerformanceBudget` helper (§3).
2. Migrate `FluidMod` onto it; verify behaviour-preserving (§4, §7.3).
3. Migrate `ParticleSetMod` onto it (incl. spawn gate); verify.
4. Add `SmearMod` (§5 #1) — first genuinely new coverage.
5. Add `BlurMod`, `ParticleFieldMod`, `AdvectOnlyMod`.
6. Weight-calibration tuning pass across representative configs (§6).
7. (Optional) shared diagnostic helper (§3.3); optional lower-priority mods (§5 #5).

Each step is independently shippable and independently verifiable.

---

## 9. Reference index (exact locations)

Addon root: `/Users/steve/Development/openframeworks/of_v20251123_osx_release/addons/ofxMarkSynth/`

- Base hooks: `src/core/Mod.hpp:194-210` (`getPerformanceWeight`, `getCurrentThrottleFactor`); layer-pause gate `src/core/Mod.cpp:305-318`, `Mod.hpp:261-263`.
- Synth budget: `src/core/Synth.cpp:542-558` (update order), `:560-594` (pressure EMA), `:596-613` (share). Decls: `src/core/Synth.hpp:188,200,204,265-268`.
- FluidMod: `src/layerMods/FluidMod.hpp:40-41,142-154`; `src/layerMods/FluidMod.cpp:70-77` (weight), `:170-229` (budget loop + apply), `:275-296` (diag).
- ParticleSetMod: `src/sinkMods/ParticleSetMod.hpp:38-138`; `src/sinkMods/ParticleSetMod.cpp` budget block ~`:92-196`.
- SmearMod: `src/layerMods/SmearMod.cpp:96-176`.
- BlurMod: `src/filterMods/BlurMod.cpp:50-130`.
- ParticleFieldMod: `src/sinkMods/ParticleFieldMod.cpp`. AdvectOnlyMod: `src/layerMods/AdvectOnlyMod.cpp`.
- Layer alpha vs pause (context only): `src/rendering/CompositeRenderer.cpp:150-176`.

---

## 10. Out of scope (deferred to the cross-fade thread)

- Cross-fading between two configs / two live synths.
- "Feed-forward" deliberate pressure injection during a known transition (would reuse this system but is a separate change).
- Layer alpha-ramp-then-pause choreography for graceful outgoing-config dissolve.
- Any change to the cross-fade `ConfigTransitionManager` / config load-unload lifecycle.

This rollout deliberately stays a steady-state, reactive overload-protection improvement. It makes the later cross-fade work cheaper (more mods that can shed load on command) but does not depend on it and should land independently.
