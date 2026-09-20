# Updating the Xcode project

The Makefile build finds its sources by globbing, so it needs nothing when files come
and go. **Xcode lists every file by name**, so a new engine file (a new Mod in
`ofxMarkSynth`) or a new file in `src/` is invisible to an Xcode build until the project
is regenerated — the symptom is a link error naming a symbol that plainly exists.

Run [`../refresh-xcode-project.sh`](../refresh-xcode-project.sh) (`--build` to build and
check the bundle as well). It does the steps below, in this order, and refuses to start
if the files it restores have uncommitted edits.

## What projectGenerator gets wrong, and what it costs

**Its options do not take a space-separated value.** `-o <ofPath>` does not consume the
path: the path becomes the *project to generate*. Running

```
projectGenerator -o /path/to/openFrameworks -p osx /path/to/fingerprint2
```

writes a whole stray project into the **openFrameworks root** — `Makefile`,
`config.make`, `addons.make`, `of.entitlements`, `Project.xcconfig`,
`openFrameworks-Info.plist`, `src/`, `bin/` and a `.xcodeproj` — and never touches
fingerprint2. `-p osx .` leaves a stray `osx/` folder the same way. Pass the
openFrameworks path in `PG_OF_PATH`, run from this directory, and give it `.` and
nothing else.

**It rewrites this project's `Makefile`.** That Makefile is not the generated one: it
wraps the Release and Debug goals to copy `Syphon.framework` into the app bundle
afterwards. Restore it (`git checkout -- Makefile`) — see below for why.

**It overwrites `Project.xcconfig`** (product name, bundle id, version). Restore that
too; [Release-Build.md](Release-Build.md) covers the signing settings, which live in
`App.xcconfig` and are never touched.

**Every run rewrites the project's internal identifiers**, so a regeneration that added
nothing still shows as several thousand changed lines. The script compares the file
names the project lists and puts the project back when they are unchanged, so a run that
was not needed leaves no diff at all. It also restats the two scheme files without
changing them; `git diff` shows nothing for them.

## Syphon: two builds, two mechanisms

`ofxSyphon` ships a framework that has to sit inside the app bundle, because the binary
loads it from `@loader_path/../Frameworks/Syphon.framework`. The two builds get it there
differently, which is why it keeps causing trouble:

| Build | How Syphon gets in | How it breaks |
|---|---|---|
| Xcode | a copy-files build phase in the project | a regeneration that dropped the phase would ship a bundle without it |
| `make` | the wrapper in this project's `Makefile` (`bundle_syphon`) | openFrameworks' make re-creates `bin/<app>.app` from scratch on **every** build and ships it without the framework, so losing the wrapper means every `make` build dies at launch |

The failure looks like this, immediately at launch, with no other clue:

```
Library not loaded: @loader_path/../Frameworks/Syphon.framework/Versions/A/Syphon
```

So after regenerating, check all three: `bundle_syphon` is still in the `Makefile`, the
project still has its copy-files phase naming `Syphon.framework`, and a freshly built
bundle actually contains `Contents/Frameworks/Syphon.framework`. The script checks the
first two always and the third with `--build`.

## Verifying and committing

Build the **Debug** scheme to check the project — it produces its own bundle and leaves
the Release app in `bin/` as it was:

```
xcodebuild -project fingerprint2.xcodeproj -scheme "fingerprint2 Debug" -configuration Debug build
```

A regeneration rewrites the project file wholesale (thousands of lines, mostly
reordering), so read the diff for *file references* rather than line count. Commit the
project file **alone**: `fingerprint2.xcodeproj/project.xcworkspace/xcuserdata/...`
is Xcode's own window state and does not belong in the commit.

```
git commit --only fingerprint2.xcodeproj/project.pbxproj -m "Refresh Xcode references: <what was added>"
```
