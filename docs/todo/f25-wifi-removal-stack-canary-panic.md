# F25 — WiFi stack removal (~45KB RAM/flash) deferred: intermittent stack-canary panic

Status: **NOT SHIPPED / REVERTED.** Recommended for a future session to re-attempt with the
mitigations below, not to blindly reapply the same diff.

## Background — what F25 is and why it matters

This device is Ethernet-only (W5500 over SPI). It has no WiFi hardware use case, but two source
files still pull in the Arduino-ESP32 WiFi stack purely for incidental reasons:

- `src/globals.h:9` — `#include <WiFiUdp.h>`, used only to declare the OSC UDP socket type.
  `src/globals.h:48` — `extern WiFiUDP oscUdp;`
- `src/cantim_mqtt_new.cpp:9` — `#include <WiFi.h>`, used only for `WiFi.onEvent(...)`.
  `src/cantim_mqtt_new.cpp:16` — `WiFiUDP oscUdp;` (definition matching the extern above).
  `src/cantim_mqtt_new.cpp:177` — `WiFi.onEvent(WiFiEvent);`, registered purely to catch
  **Ethernet** events (`ARDUINO_EVENT_ETH_*`) — this project has no WiFi radio use.
- `src/mqtt.cpp:5` — `static void writeOscString(WiFiUDP &udp, const char *text)`, a function
  signature that takes `WiFiUDP` by reference (downstream consumer of the same type).

A controlled re-link (audit session, 2026-08-02) measured the true cost of keeping `<WiFi.h>` /
`<WiFiUdp.h>` linked in for a device that never uses WiFi:

- **18,456 bytes static RAM**
- **26,904 bytes heap**
- **353,572 bytes flash**

This was the single largest recoverable chunk found in the entire P0–P11 audit — hence "F25" and
the ~45KB RAM+heap headline number (18,456 + 26,904 ≈ 45.4KB; flash savings are separate/larger).

## The fix that was attempted (and why it should be semantically a no-op)

Change performed, all three sites above:

1. `src/globals.h`: `#include <WiFiUdp.h>` → `#include <NetworkUdp.h>`; `WiFiUDP oscUdp` (extern)
   → `NetworkUDP oscUdp`.
2. `src/cantim_mqtt_new.cpp`: `#include <WiFi.h>` removed entirely; `WiFiUDP oscUdp` (definition)
   → `NetworkUDP oscUdp`; `WiFi.onEvent(WiFiEvent);` → `Network.onEvent(WiFiEvent);`.
3. `src/mqtt.cpp`: `WiFiUDP &udp` parameter → `NetworkUDP &udp`.

Rationale this was expected to be behavior-preserving, confirmed by reading the Arduino-ESP32
core source for the framework version this project pins:

- `WiFiUDP` is already `typedef NetworkUDP WiFiUDP` in this Arduino core — same underlying type,
  just renamed at the call site. No behavior change possible from the typedef swap itself.
- `WiFiGenericClass::onEvent(...)` (what `WiFi.onEvent` resolves to) forwards directly to
  `Network.onEvent(...)` internally — i.e. `WiFi.onEvent` was already just a thin wrapper around
  `Network.onEvent` in this core. Calling `Network.onEvent` directly skips the wrapper but
  registers the exact same callback against the exact same event bus.

Build result: clean compile/link, and the RAM/heap/flash deltas matched the predicted savings
from the earlier measurement pass exactly.

## What went wrong — on-device crash

On-device serial verification on `COM15` (this project's real target board), using
`tools/serial_capture.py` with fresh flashes and repeated DTR/RTS resets (this project's standard
cold-boot trial methodology), found an **intermittent crash on cold boot** with the fix applied:

```
Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception)
Debug exception reason: Stack canary watchpoint triggered (ipc1)
```

Trial results, same board, same methodology, fresh flash before each trial set:

| Build                                    | Trials | Panics |
|-------------------------------------------|--------|--------|
| With F25 fix (Network-only, WiFi.h removed) | 8      | 2      |
| Baseline (unmodified, WiFi.h-based)        | 8      | 0      |

After each panic, the device **auto-recovered on the next boot** (ETH Got IP / HTTP Server
Started came up normally) — this is a boot-time crash-and-reboot, not a persistent brick or hang.
Still, a device meant to run unattended through a live show randomly rebooting on ~1 in 4 cold
boots is not acceptable to ship without understanding root cause.

**Sample size caveat:** 8 trials / 2 panics is a small sample with a wide confidence interval.
0/8 on baseline does not prove baseline is panic-free, only that it didn't reproduce in this
sample size. See recommended next steps below.

## Leading theory (UNCONFIRMED — not root-caused)

Since the source-level change is behaviorally a no-op at the call-site level (same event bus via
`Network.onEvent`, same `NetworkUDP` type merely renamed), the working hypothesis is:

> Removing ~350KB of WiFi library from the link shifts the RAM/flash layout (link order, task
> stack placement, etc.) enough to expose a **pre-existing marginal stack overflow** in the
> ESP-IDF `ipc1` system task — i.e. the WiFi-removal change likely did not *introduce* the bug, it
> just changed memory layout enough to make an already-marginal stack budget occasionally
> overflow during boot-time IPC init.

This is a theory, not a finding. It has not been verified by inspecting actual stack usage,
`ipc1` task stack size configuration, or a debugger backtrace at the panic site.

## Disposition

The change was **fully reverted** before merge: `src/mqtt.cpp` diffed at 0 lines versus baseline;
`WiFi.h` / `WiFiUDP` / `WiFi.onEvent` restored verbatim in `src/globals.h` and
`src/cantim_mqtt_new.cpp`. Re-verified at **4/4 clean cold boots** with the revert applied.

The surrounding commit that *did* ship, `9e14592` ("fix: F5 - remove dead
oscAddress/messengerEnabled variables"), went in **without** the WiFi-removal part — only the
confirmed-safe dead-code removal (F5) shipped in that commit. F25 itself never landed on `main`;
this doc is the only artifact of the attempt.

## Recommended next steps for whoever picks this up

None of the following has been attempted yet — these are proposals only.

1. **Cheap first experiment:** increase the `ipc1` task stack size via a build flag
   (`CONFIG_ESP_IPC_TASK_STACK_SIZE` or whatever the exact Kconfig option is named for this
   IDF/Arduino-core version — verify the exact name against the actual `sdkconfig`/Kconfig for
   the pinned core version before assuming; it may differ across IDF versions) and re-run the same
   cold-boot trial methodology against the F25 diff.
2. **Statistical rigor:** re-run with a much larger trial count (20+ cold boots) on *both* the
   F25 variant and the unmodified baseline before drawing conclusions — 8 trials / 2 panics is not
   enough to distinguish real signal from noise, and 0/8 on baseline is not proof of safety either.
3. **Check whether this is latent in shipped code today:** if a real `ipc1` stack overflow is
   confirmed, do not assume the currently-shipped WiFi.h-based build is safe just because this
   diagnostic session's 8 baseline trials didn't catch it — run the larger trial count (per #2)
   against current `main` too, since the bug may be pre-existing and merely layout-sensitive
   rather than caused by the WiFi removal.

## File/line pointers (as of this writing, branch `fix/p0-p11-audit-2026-08-02` / `main`)

- `src/globals.h:9` — `#include <WiFiUdp.h>`
- `src/globals.h:48` — `extern WiFiUDP oscUdp;`
- `src/cantim_mqtt_new.cpp:9` — `#include <WiFi.h>`
- `src/cantim_mqtt_new.cpp:16` — `WiFiUDP oscUdp;` (definition)
- `src/cantim_mqtt_new.cpp:177` — `WiFi.onEvent(WiFiEvent);`
- `src/mqtt.cpp:5` — `static void writeOscString(WiFiUDP &udp, const char *text)`
- `tools/serial_capture.py` — cold-boot trial capture tool used for the on-device verification
  described above (fresh flash + repeated DTR/RTS reset, COM15 in this project).

## Related

- Commit `9e14592` — the F5 dead-code-removal commit whose message documents this deferral in
  brief; this doc is the full write-up.
- Merge commit `e66e9a6` — merged the P0–P11 audit fix branch (`fix/p0-p11-audit-2026-08-02`,
  9 commits) into `main`; F25 is **not** included in that set.
