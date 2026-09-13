# Native Studio regression checks

Build the host with `make Release`, then run on a logged-in macOS desktop:

```sh
python3 tests/run_studio_native.py context
python3 tests/run_studio_native.py sessions /tmp/performance-a/session.json /tmp/performance-b/session.json
```

Use disposable performance copies for the session test. It uses the normal host
queue and replacement path, draws Studio between four switches, checks that the
requested session and a valid ImGui context are present, then exits. It does not
change the remembered startup session. Use recorded, silent inputs for unattended
runs; configured controllers and video are initialised normally.

The context test exercises same-instance and new-instance reuse of a live window,
shared-instance exit in both orders, rejection of an ownerless shared context,
repeated shutdown, and drawing after replacement. The stale-context bug reproduced
as a crash immediately after the first shutdown, before the lifecycle fix.

The runner takes compiler/link flags from the host Makefile and reuses its Release
objects. It replaces only the entry-point object in a temporary test executable;
it never overwrites the app executable or its object/dependency files. The fixtures
are outside `src/` so neither the normal Makefile nor Xcode compiles them into the
performance app.
