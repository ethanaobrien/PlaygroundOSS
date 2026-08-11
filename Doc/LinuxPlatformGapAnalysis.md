# Linux platform gap analysis

## Executive summary

The Linux port is past the platform-bootstrap stage. The complete engine graph
links, the SDL3/GLES2 lifecycle runs, encrypted official assets load, and a
6,000-frame release run advances through `start.lua`, `m_boot/start.lua`, and
`m_login/start.lua`. It also creates the expected SQLite files in the external
root. Rendering, asset decryption, fonts, SQLite, temporary files, libcurl,
timing, and the basic thread/synchronization boundary are therefore already
operational.

`DesktopPlatform` implements the complete 120-method platform-request surface.
The size of that interface is misleading: most methods are small adapters, and
many mobile services are not prerequisites for playable desktop flows. Static
source inventory finds 232 direct `platform().method(...)` expressions across
77 engine files, including 23 Lua-library files. Calls made through local
`IPlatformRequest` aliases make this a lower bound, not a coverage claim.

The remaining work is approximately:

- **Minimum credible playable Linux port:** 1,500-2,800 lines of new or
  substantially adapted platform code.
- **Comfortable desktop port:** 2,500-4,000 lines.
- **Broad mobile-feature parity:** 4,000-6,500 lines, mostly optional widgets,
  movie/web integration, and mobile service substitutes.

These ranges exclude changes to reconstructed engine behavior. Existing engine
codecs, curl wrappers, rendering, font code, and Android audio mixer logic can
be reused behind new desktop boundaries.

## Measured current boundary

The initial Linux implementation is about 640 lines for `DesktopPlatform`,
desktop services, and the mute audio backend, plus roughly 500 lines for the
generic SDL host and engine lifecycle executable. It currently provides:

- SDL3 windowing, GLES2 context creation, high-DPI framebuffer sizing,
  pointer/back-key/activity/resize delivery, and orderly shutdown;
- install/external/asset virtual paths, ordinary and temporary files,
  directory creation, free-space reporting, and shipped asset decryption;
- FreeType-backed engine fonts and text metrics;
- libcurl initialization, request setup, callbacks, form data, execution, and
  status retrieval;
- monotonic and wall-clock time, threads, mutexes, condition events, random
  bytes, logging, and readable assertions;
- enough conservative service behavior to boot official Lua without pretending
  unavailable mobile features succeeded.

The official archive contains 1,985 Lua files, 1,335 JSON files, 3,393 image
assets, 513 texture bundles, 341 Ogg Vorbis files, and the shipped fonts and
databases. Audio is therefore a real gameplay requirement even though it does
not block the login scene.

## Remaining work by priority

| Priority | Subsystem | Current state | Needed work | Estimated code |
| --- | --- | --- | --- | ---: |
| P0 | Cryptography and login identity | Random bytes and curl work; RSA, AES-CBC, request-ID, and device-integrity data are unavailable or placeholders | Use OpenSSL for the exact RSA/AES contracts; provide stable configurable desktop identity and mechanically validate request/response buffers | 350-700 |
| P0 | Persistent key/value storage | Secure data and user defaults are process-local maps | Atomic file-backed store with stable device ID, permissions, corruption handling, and migration/versioning | 200-400 |
| P0 | Audio | Lifecycle-compatible mute backend | Reuse the reconstructed Ogg/Tremolo decoder and Android mixer/voice state behind an SDL3 audio stream; preserve loop, seek, fade, BGM/SE volume, pause, and command semantics | 600-1,200 |
| P0 | Runtime hardening | Boot and login scene run; longer interactive paths are not yet proven | Exercise downloads, unpacking, database reopen, suspend/resume, resize, shutdown, controller/key mapping, and error paths; repair platform adapters only | 250-500 |
| P1 | Native text input | `createControl` returns unavailable | SDL text-input-backed `IWidget` for text/password boxes, including selection, max length, visibility, enable state, and engine callback events | 250-450 |
| P1 | Desktop OS integration | Clipboard, sleep inhibition, browser/mail, screenshot export, memory metrics, and locale are placeholders or fixed values | SDL/system implementations and command-line configuration where host values must be deterministic | 150-300 |
| P1 | Filesystem/thread edge cases | Core paths work; encrypted writes and thread cancellation are incomplete | Atomic/encrypted writes if exercised, safer stream errors, cancellation/wakeup semantics, and path-policy tests | 150-300 |
| P2 | Web view | Unavailable | Prefer an external-browser fallback first; embedded WebKitGTK is a separate optional adapter | 50-600 |
| P2 | Movie playback | Unavailable | Decoder/presentation integration, most naturally FFmpeg or GStreamer into an engine texture | 600-1,200 |
| P2 | Store, ads, notifications, location, and motion | Explicit unavailable/no-op services | Keep unsupported for community desktop play, or add host-specific substitutes only when an official Lua flow demonstrably requires them | 0-1,000 |

The estimates overlap slightly because persistence, identity, and crypto should
share serialization/configuration code, while text, web, and movie controls
share widget/event plumbing.

## Recommended implementation order

1. Add observable platform-call tracing around only the unavailable/provisional
   methods and run the official login path. This turns assumptions about Lua
   usage into a concrete exercised-service list.
2. Implement persistent user defaults, secure data, and stable device identity.
3. Implement and vector-test RSA and AES-CBC using OpenSSL, then validate the
   first real HTTP/login request without changing Lua or the protocol.
4. Port the existing decoded-audio/mixer state to an SDL3 output callback.
5. Add text/password input when an exercised flow first requests it.
6. Continue representative gameplay, download, story, and live-show runs;
   implement optional desktop services only when reached.

This ordering should produce a usable networked, audible Linux build well
before broad mobile parity. Web views, movies, purchases, ads, sensors, and
notifications should not be allowed to inflate the critical path unless an
official flow proves otherwise.
