## Session Config

`fingerprint2` uses a *session config JSON* file to drive startup configuration (audio/video input selection, performance paths, logging, etc.).

### Selecting a config

- On startup, the app prompts for a JSON file (default directory: `~/Documents`).
- Hold **Shift** during launch to force the chooser again.

### Persistence

The last chosen config path is stored here:
- `~/Library/Application Support/fingerprint2/lastSessionConfig.json`

### Reference file

A reference example is checked into the repo:
- `session-config.reference.json`

Underscore-prefixed keys (e.g. `_micDeviceName`) are treated as inactive/commented-out alternatives.

### Recording + autosnapshots

- `startRecordingOnFirstWake`: if true, starts composite recording after first wake.
- Composite recording muxes audio+video automatically on stop/exit.
- `autoSnapshotsEnabled`/`autoSnapshotsIntervalSec`/`autoSnapshotsJitterSec` control full-res EXR autosnapshots.
