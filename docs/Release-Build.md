# Making a signed, notarized Release build

This app ships as **`Fingerprints.app`** — Developer ID signed + notarized. The
settings that matter for a release live in **xcconfig files, not the Xcode project**,
so they survive a `projectGenerator` regenerate.

## Where the release settings live (authoritative)

- **`Project.xcconfig`** — product name (`PRODUCT_NAME = Fingerprints`, Debug
  `FingerprintsDebug`), bundle id (`com.meyfroidt.fingerprints`),
  `MARKETING_VERSION` (1.0.0), `CURRENT_PROJECT_VERSION`, icon
  (`ICON_NAME = icon.icns`), `HIGH_RESOLUTION_CAPABLE` (currently `NO`).
  **`projectGenerator` overwrites this file** — restore it after regenerating:
  `git checkout -- Project.xcconfig`.
- **`App.xcconfig`** — code signing, scoped to Release: `DEVELOPMENT_TEAM = EZCN55TA7J`,
  `CODE_SIGN_STYLE = Manual`, `CODE_SIGN_IDENTITY = Developer ID Application`,
  `ENABLE_HARDENED_RUNTIME = YES`, `OTHER_CODE_SIGN_FLAGS = --timestamp`.
  **`projectGenerator` never touches this file**, so signing always survives a regenerate.
- **`of.entitlements`** — wired into the project via `CODE_SIGN_ENTITLEMENTS`.

Because both xcconfigs are authoritative and override the `.pbxproj`, a regenerated
project still builds a correctly named + signed app. Neither `.pbxproj` sets
`PRODUCT_NAME`, so `PRODUCT_NAME = Fingerprints` from `Project.xcconfig` wins.

### Regen residue (cosmetic only)

Running `projectGenerator` (to pick up new addon files) reformats the `.pbxproj`
(classic plist → JSON style) and reverts its product *reference* and the scheme
`BuildableName` to `fingerprint2.app`. This does **not** change the build output —
that follows `PRODUCT_NAME` from `Project.xcconfig` (so `xcodebuild` still emits
`bin/Fingerprints.app`). It only affects the Xcode **Run** button's product lookup.
To clear it, set `BuildableName = "Fingerprints.app"` (and `"FingerprintsDebug.app"`
for the Debug scheme) back in the two `*.xcscheme` files, or just ignore it and use
`./notarize.sh` below.

## After regenerating the Xcode project (to add new addon files)

`projectGenerator` is needed only to add new addon `.cpp`/`.hpp` to the **Xcode**
target. After running it:

1. `git checkout -- Project.xcconfig` — restore the custom icon, product name, and hi-res flag.
2. (optional) Restore the scheme `BuildableName`s if you want Xcode's Run button to find `Fingerprints.app`.

Day-to-day **`make` builds need none of this** — the makefile globs the addon `src/`,
so new Mods compile without regenerating the project.

## Make the release

1. Confirm `Project.xcconfig` is restored (above) and `App.xcconfig` is present.
2. Bump `CURRENT_PROJECT_VERSION` in `Project.xcconfig` — each notary submission needs a new build number.
3. Run `./notarize.sh` from the app dir. It:
   - builds the `fingerprint2 Release` scheme (signed by `App.xcconfig` → `bin/Fingerprints.app`),
   - verifies Developer ID + Hardened Runtime,
   - submits to the Apple notary service (keychain profile `fingerprints-notary`) and waits,
   - staples the ticket and runs a Gatekeeper assessment.
4. Ship `bin/Fingerprints.app`.

### One-time notary credential setup

If the `fingerprints-notary` keychain profile is missing:

```sh
xcrun notarytool store-credentials fingerprints-notary \
  --apple-id <your-apple-id> --team-id EZCN55TA7J
# then paste an app-specific password when prompted
```

> Signing uses the **paid** team `EZCN55TA7J` (not the personal team `SA227FF5Q2`).
