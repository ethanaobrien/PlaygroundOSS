# Linux platform gap analysis

## Completion status

The Linux platform implementation is complete for the engine's desktop
contract. The complete graph links, the SDL3/GLES2 lifecycle runs, encrypted
official assets load, and fresh 6,000-frame Debug and Release runs advance
through `start.lua`, `m_boot/start.lua`, and `m_login/start.lua`. Both runs exit
normally and create the expected SQLite files in an isolated external root.

`DesktopPlatform` implements the complete 120-method platform-request surface.
The size of that interface is misleading: most methods are small adapters, and
many mobile services are not prerequisites for playable desktop flows. Static
source inventory finds 232 direct `platform().method(...)` expressions across
77 engine files, including 23 Lua-library files. Calls made through local
`IPlatformRequest` aliases make this a lower bound, not a coverage claim.

No official Lua or application asset was modified. Mobile-only commerce and ad
services deliberately produce their ordinary failure callbacks on desktop;
that is a complete and observable desktop semantic, not a placeholder success.
An embedded web renderer is not required: web controls use the system browser,
while engine movie textures use FFmpeg directly.

## Implemented boundary

The modern Linux boundary provides:

- SDL3 windowing, GLES2 context creation, high-DPI framebuffer sizing,
  pointer/back-key/activity/resize delivery, and orderly shutdown;
- install/external/asset virtual paths, ordinary and temporary files,
  directory creation, free-space reporting, and shipped asset decryption;
- FreeType-backed engine fonts and text metrics;
- libcurl initialization, request setup, callbacks, form data, execution, and
  status retrieval;
- monotonic and wall-clock time, worker lifecycle/cancellation, mutexes,
  condition events, random bytes, logging, diagnostics, and readable assertions;
- persistent atomic secure/default state, stable UUID identity, exact request
  headers, Linux device-integrity properties, RSA verification/encryption, and
  AES-CBC interoperability;
- SDL3 audio output reusing the reconstructed mixer, voices, policy state, and
  Tremolo Ogg decoder, including loop/fade/pause/volume semantics;
- SDL text/password input and callbacks, system-browser web controls, activity
  controls, external movie-widget fallback, and FFmpeg RGBA movie textures;
- clipboard, screenshots, sleep inhibition, process/system memory, locale,
  notifications, configurable location/motion, and callback-complete mobile
  service failures.

The official archive contains 1,985 Lua files, 1,335 JSON files, 3,393 image
assets, 513 texture bundles, 341 Ogg Vorbis files, and the shipped fonts and
databases. Audio is therefore a real gameplay requirement even though it does
not block the login scene.

## Verification matrix

| Gate | Debug | Release |
| --- | --- | --- |
| Full engine graph / SDL host | Pass | Pass |
| Platform service probe | Pass | Pass |
| Official encrypted Ogg through SDL mixer | Pass | Pass |
| Synthetic FFmpeg MPEG-4 movie | Pass | Pass |
| Immutable official assets, 6,000 frames | `m_login/start.lua`, clean exit | `m_login/start.lua`, clean exit |

## Honest platform distinctions

- Store purchases/restoration and rewarded ads fail through their documented
  engine callbacks because no mobile billing/ad provider exists on Linux.
- Web content opens in the configured system browser rather than an embedded
  mobile WebView.
- Location and motion are opt-in deterministic inputs supplied by environment
  variables; they do not fabricate host sensors.
- Desktop notifications use `notify-send` when installed. The generated remote
  token is a stable local identifier, not a push-provider registration.
- Further login, download, story, and live-show validation depends on reachable
  community network endpoints and user interaction. It is integration testing,
  not unfinished platform implementation.
