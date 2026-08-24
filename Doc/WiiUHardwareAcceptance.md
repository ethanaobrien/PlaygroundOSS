# Nintendo Wii U hardware acceptance record

Compile success and Cemu are useful diagnostics, but neither replaces this
real-hardware record. Fill in the exact commit, console environment, and WUHB
hash used for release.

## Cemu qualification (not hardware acceptance)

The 2026-08-21 Cemu 2.6 qualification bundle has SHA-256
`6cde3ffdf5e7a0947b5cb4309c4a5623901969e0c016b22b3a1e46093d2b845f`.
It mounts the WUHB, loads CafeGLSL, creates account `80000001` SaveData,
rotates `state/entropy.seed`, validates the former account-scoped AppAssets
generation, and reaches the title/login scripts with graphics and music. That
archive-based bundle predates the read-only expanded-content packaging and is
retained only as historical platform evidence; the current bundle still needs
its own Cemu and hardware run.
An independent throwaway WUHB also made a live request to
`https://sif.ethanthesleepy.one/` through the same Wii U curl/mbedTLS stack and
bundled CA: it returned `CURLE_OK` and HTTP 200 with peer and hostname
verification enabled. A second request deliberately used a certificate for
the wrong hostname and failed with `CURLE_PEER_FAILED_VERIFICATION` and
`BADCERT_CN_MISMATCH`, proving that the successful request was not merely
accepting arbitrary certificates. This caught and justified disabling curl's
unreadable default CafeOS `CAPATH`. The build also passes the GX2 symbol/format,
hardware entropy override, pinned CA bundle, and explicit TLS configuration audits.
This is useful emulator evidence only; GamePad touch must still be exercised
manually in Cemu's GamePad View, and every table below remains a real-console
gate.

## Build identity

| Field | Value |
| --- | --- |
| Git commit | PENDING |
| WUHB SHA-256 | `ffabd0f05f3b1d8f92ed7e4ea03c9ca3c5b03e5013707a993a166525e56923f9` |
| Console firmware | PENDING |
| Aroma/version | PENDING |
| AppAssets SHA-256 | `12ba02478978819bd37407eb27bf54ed8b34a93ba814356ddbe2ad4c0c988c20` |

## Storage and bootstrap

| Test | Result | Evidence/notes |
| --- | --- | --- |
| Fresh account creates title SaveData | PENDING | |
| First launch validates bundled expanded AppAssets without extraction | PENDING | |
| Relaunch uses the same read-only AppAssets root | PENDING | |
| WUHB contains no legacy `AppAssets.zip` duplicate | PENDING | |
| Two Wii U users retain independent identity and state | PENDING | |
| Downloads are account-scoped; identity/state use title SaveData | PENDING | |
| Startup failure is readable rather than a black screen | PENDING | |

## Graphics, input, lifecycle, and audio

| Test | Result | Evidence/notes |
| --- | --- | --- |
| TV and GamePad render the complete title | PENDING | |
| Calibrated GamePad touch down/drag/up works | PENDING | |
| B/back and Plus/clean-exit work | PENDING | |
| Motion-backed engine queries return live values | PENDING | |
| Native software keyboard accepts normal and password text | PENDING | |
| HOME/resume x20 at title, home, live, and download | PENDING | |
| Touch is canceled on foreground release | PENDING | |
| Audio routes to TV and GamePad without underruns | PENDING | |
| Audio resumes cleanly after HOME and controller changes | PENDING | |

## Network and media

| Test | Result | Evidence/notes |
| --- | --- | --- |
| HTTPS login succeeds with certificate verification | PENDING | |
| Base and on-demand downloads publish atomically | PENDING | |
| Connectivity loss fails promptly and retry recovers | PENDING | |
| Movie playback renders and audio stays synchronized | PENDING | |
| Suspend during download leaves no partial final file | PENDING | |

## Release decision

- [ ] Every required row is PASS on real hardware.
- [ ] The final WUHB is rebuilt from a clean tree and its hash recorded.
- [ ] No AppAssets, console keys, generated RPX, or WUHB is committed.
- [ ] Cemu and hardware logs contain no assertion, SaveData, TLS, AX, or GX2
      failures.
