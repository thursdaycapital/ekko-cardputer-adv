# Remote app host harness

The runner compiles the actual `app_remote.cpp` and protocol header with ASan
and UBSan. Before every app frame the fake HAL updates the keyboard, clearing
the previous event and then taking at most one queued press/release event.
As in the real keyboard, the getter returns a non-consuming const reference;
clearing an idle frame resets all event fields, matching the hardened driver.
Tests cover multiple reads in one frame and a single press across busy,
completion, release, and many idle frames without retransmission. The fake
also models clearing the launch event;
the fake HAL copies transmitted pulse data and lets tests control asynchronous
busy/completion/failure/cancellation results. The REPL fake exposes the actual
production command callback, so parsing and submission logic are not duplicated.

These tests check UI command routing and lifecycle only. They do not drive GPIO,
read a physical screen, simulate the full Mooncake/REPL event subscriptions, or
prove compatibility with any air conditioner. Protocol and RMT timing have
separate production/reference and ESP-IDF transport tests.

The NVS fake models readonly missing namespaces, staged blob writes, atomic
commit, handle closing, and injected open/read/write/commit failures. Tests
verify that only `ac_remote/settings` is accessed and that successful sending
is independent of preference persistence. This does not measure real flash
wear, power-loss atomicity, or the hardware NVS partition.
