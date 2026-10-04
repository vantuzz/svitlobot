# Trusted AIO 3.5.62 — developer and AI maintenance contract

**Scope:** personalized ESP32-C3 All-in-One profile in the `trusted-aio-runtime-controls` feature branch. This is a working-context document for a new AI session or a human maintainer, NOT a request for autonomous deployments. Read the [Ukrainian operator guide](TRUSTED_AIO.md) first. Keep both documents in sync with code changes. The two PRs are currently Draft; the generic upstream and other board profiles must not be described as having this firmware.

## 0. Ground truth, repository boundaries and known state

- Public/source fork: `vantuzz/svitlobot`, feature branch `trusted-aio-runtime-controls`, Draft PR #1 targeting `trusted-aio`. Source code and non-secret docs live here. Never publish a personalized firmware artifact.
- Private build repo: `vantuzz/svitlobot-private-build`, same feature branch, Draft PR #1. Four build secrets, fail-closed C3 CI, host tests and personalized artifacts live here. Changes to shared C3 firmware source/docs must be mirrored between repos; private-only workflow/test changes must remain private.
- Build target: `all-in-one/aio_esp32c3.yaml`, ESP32-C3, ESP-IDF, ESPHome **2026.5.1**. This is *not* a generic release of every board/profile.
- Effective source candidate project version is **3.5.62**, set by the last included `packages/trusted_aio_delivery.yaml`. A base substitution of `3.5.57` also exists in `trusted_aio_svitlobot.yaml`. The physically tested installation is **3.5.61** until a new 3.5.62 private CI/OTA/hardware signoff. Build manifest, running HTTP User-Agent and private validator must agree for the *same* build.
- Historical validated private run for the first hardware installation: `37065148538`; OTA image SHA-256 `85343D8E8362AA856ACBE348467257FD668953DEBB99EF259282FA1F05DBA2A2` (1,167,824 bytes), run's ZIP digest `e74a643578c1d1333b0651a1b98bd50e29bc19b40024a3d0195b6bef3943d52f`. These identify **one** built artifact; a later rebuild must be revalidated from its own manifest and hashes. Artifact links expire.
- Single physical-device smoke test (2026-10-03) passed; 24-hour stability observation was started but its outcome **has not yet been reported**. Do not claim a long-duration test or all fault-injection cases have passed. See ``7–8` below.

## 1. Source map (read this before proposing changes)

| File | Ownership / contract |
| --- | --- |
| `all-in-one/aio_esp32c3.yaml` | The only reviewed personalized C3 package composition, in include order |
| `packages/trusted_aio_common.yaml` | Native encrypted HA API, native + WebUI OTA, local authenticated WebUI, secrets, base scripts/diagnostics, log suppression |
| `packages/trusted_aio_svitlobot.yaml` | Direct / Custom Relay / Paused, runtime Key/URL/Token, status, request attempt counter, response and epoch checks |
| `packages/trusted_aio_delivery.yaml` | Effective 3.5.62 version, persistent interval, independent SNTP, manual Send Now buttons, after-request UTC diagnostics, error/reset policy, invalidation hooks |
| `packages/healthcheck.yaml` | Direct Healthchecks HTTPS, full-body exact acknowledgment classifier, per-service status |
| `packages/custom_url.yaml` | Optional GET third service and per-service status |
| `packages/esp32.yaml` | Local Shadow hookup; 5-second startup delay, templated initial interval |
| `components/shadow/{__init__.py,shadow.h,shadow.cpp}` | Vendored MIT adaptation and the single ESPHome-main-loop timer |
| `components/shadow/{LICENSE,UPSTREAM.md}` | Licensing, pinned upstream origin, local architectural differences |
| `.github/workflows/build_all_in_one.yaml` (private) | C3 source gate → ESPHome compile → binary/partition validation → PRIVATE OTA-only artifact → READY job |
| `.github/scripts/validate_c3_ota.py` (private) | Active config/source invariants, partition table, chip/image, manifest/SHA and OTA-slot checks |
| `.github/tests/shadow_host_test.cpp` + stubs (private) | Actual Shadow.cpp tested against synthetic main-loop scheduler/OTA callbacks |
| `.github/scripts/test_*.py` (private) | Offline source/validator regression fixtures |

Do not edit the shared `healthcheck.yaml` or `custom_url.yaml` assuming the effect is limited to Trusted C3. The stricter Healthchecks acknowledgment check and URL-string-lifetime fixes also affect other profiles built from the feature branch. By contrast, the trusted common/delivery/Direct-Relay mode are C3-only through package selection.

## 2. Data path and runtime semantics

`Shadow` starts the composite `site_ping` script. Package `!extend` actions build one ordered script for SvitloBot, HealthCheck and Custom URL. Each service's result is independent. `http_request` timeout is 15 seconds per enabled request; certificate verification is enabled, and the trusted delivery package disables redirects.

### SvitloBot modes (persistent select, preserve ordering)

| Exact option | Behavior |
| --- | --- |
| `Direct` (index 0) | GET official SvitloBot API with restored `SvitloBot Key`; selected default |
| `Custom Relay` (index 1) | GET the configured HTTPS URL; include `Authorization: Bearer <relay-token>`. No Direct key in the relay request and no implicit Direct fallback |
| `Paused` (index 2) | Skip new SvitloBot requests; Healthchecks and Custom URL continue; in-flight request may finish |

**DO NOT reorder existing select options**: ESPHome restores this select by index. Changing it requires an explicit NVS migration design and real device test.

Relay validation: URL must begin with `https://`, be longer than the bare prefix, and contain no query `?`, fragment `#` or userinfo marker `@`. Token length >=16; configured text field maximum 128. GET and Bearer are an explicit interface contract with the separately deployed Worker. The Worker must authenticate and forward the ping; the ESP receiving HTTP 200 from it alone does not prove upstream delivery. Avoid logging the URL or token. TLS enabled; no insecure fallback. A transient 429 is a failed attempt, not a reason to auto-reboot or retry immediately.

Direct key, Relay URL and Relay Token are optimistic template texts with `restore_value: true` and `mode: password`; those values can still be accessed by authorized HA/WebUI clients and are not encrypted at rest. A new credential/mode invalidates `Shadow::config_epoch`, resets the prior success/errors/status/response, and delayed callbacks from older configurations are discarded via revision comparison. Keep this invariant when modifying callbacks or modes. Do not silently erase Direct Key after relay success: preserve manual rollback.

### Healthchecks and Custom URL

- Healthchecks is **always direct HTTPS** to `hc-ping.com`, independent of SvitloBot mode; key/UUID is runtime-persistent. A 200 response is credited **only** if read complete, read byte count matches captured body, and the full response body equals exactly `OK`. `OK (not found)`, `OK (rate limited)`, truncated/empty/unexpected bodies, non-200 and transport failures are failures. Response capture limit: 64 bytes. Never expose the UUID or body in diagnostics.
- Custom URL is optional, runtime persistent, and can still hold HTTP URLs for compatibility. It uses GET and currently treats HTTP 200 as success; this is **not** a verified remote application acknowledgment. Never embed sensitive credentials in an HTTP Custom URL. Actual Custom URL end-to-end operation was not covered by the initial hardware smoke test.

## 3. Scheduling and OTA concurrency invariants

**Current architecture: ESPHome main-loop scheduling, NOT a FreeRTOS heartbeat worker.** Earlier review notes discussed a separate worker; that interim design is obsolete. Only the final `components/shadow/shadow.cpp` is authoritative.

- Shadow's `setup()` arms a named `shadow_initial` timeout (5 s). The restored persistent TemplateNumber is initialized earlier than Shadow setup. After first tick, one named `shadow_next` timeout is rearmed using current `shadow_interval_`.
- `Heartbeat Interval` is a persistent runtime number, default 70 s, min 70, max 300, step 5. Its callback calls `set_shadow_interval()`; this replaces the pending named timer after initial execution. Never spawn a second scheduler or launch overlapping `site_ping` scripts. An already executing HTTP action is not forcibly cancelled.
- `script_->execute()`, runtime text/select reads, and entity publication run from the ESPHome main-loop context, not an auxiliary FreeRTOS task. Keep runtime code serialized in this context or provide a separately reviewed snapshot/concurrency design.
- OTA callback changes the atomic suspension flag only: `OTA_STARTED` and `OTA_COMPLETED` block NEW launches; `OTA_ABORT` and `OTA_ERROR` resume at the next ordinary tick. An already running HTTP request can finish. Do not delete tasks, mutate ESPHome scheduler state or force network abort from asynchronous OTA callbacks.
- Atomic `config_epoch` protects against older SvitloBot callbacks overwriting status after key/URL/token/mode changes. Request revision capture and equality check are required in **both** Direct and Relay on-response/on-error paths.
- **3.5.62 manual request gate:** `Shadow::begin_manual()` is accepted only after its first scheduled tick, when not OTA-suspended, the composite `site_ping` is idle and no other manual request owns the gate. Two Trusted-C3-only template buttons run `svitlobot_ping` or `healthcheck_ping` through the original service scripts and `script.wait`; `end_manual()` then re-arms the single existing scheduled timer. Scheduled launches skip while the manual gate is held. SvitloBot `Paused` is honored. Each manual button checks a minimum 30-second gap from that service's last *actual* HTTP attempt; after SvitloBot HTTP 429 or Healthchecks rate limitation use full Heartbeat Interval. No queue, instant retry, second scheduler, third Custom URL button, mode fallback or implicit key reset.
- **Known tradeoff:** HTTP actions are synchronous on the main loop. Three enabled sequential 15-second requests can delay main-loop responsiveness for roughly 45 s plus other waits/overhead during network failure. Network-outage/TLS stall/watchdog/HA-WebUI responsiveness are still untested live. Do not present this as solved by a green compile.

## 4. Persistence, diagnostics and time

| Persistent across reboot | Deliberately boot-local |
| --- | --- |
| Direct Key, Relay URL, Relay Token, mode, Heartbeat Interval, Healthcheck Key, Custom URL (ESPHome NVS preferences) | Request Count, delivery error counters, previous-success flags, response timing/state and Last Request |

- `Last Request` is emitted on each **completed attempt** via after-state hooks, even if consecutive codes are identical. It is *not* a scheduled tick timestamp; skipped Paused cycles do not overwrite it.
- 3.5.62 UTC time is supplied independently by SNTP (`0/1/2.pool.ntp.org`) using IoT DNS plus outbound UDP/123; HA API is still available for administration but no longer a time dependency. Until synchronized, preserve `uptime Ns (clock not synced)`, never mark heartbeat failed solely for absent time. Previously installed 3.5.61 used Home Assistant time.
- `Delivery State` retains semantically distinct cases (Success, Paused, Invalid Relay URL, HTTP code, Transport error). HTTP 429 must remain an HTTP failure, not a connectivity/ESP reboot reason.
- `Request Count` increments only before a valid SvitloBot HTTP attempt, not on Paused/skipped cycles; restart resets it.
- `Restart Recommended` is an informational binary sensor, **not an auto reboot**. It requires Wi-Fi, successful SvitloBot AND Healthchecks at least once in the current boot/current config, more than 10 consecutive failures for BOTH, last SvitloBot state `Transport error` and HC response code `---`. Remote rejection/rate limit is insufficient. Automatic error-driven reboot triggers were removed for trusted C3.

## 5. Security invariants and build boundary

Compile *personalized* C3 firmware only in a private repository/local secure environment. The private workflow must refuse builds if repository visibility is public. Four required build-time Actions Secrets: `API_ENCRYPTION_KEY`, `OTA_PASSWORD`, `WEB_PASSWORD`, `FALLBACK_AP_PASSWORD`. The workflow creates temporary `all-in-one/secrets.yaml` (mode 0600); NEVER commit a real secrets file. Runtime `SvitloBot Key`, `Relay URL`, `Relay Token`, HC key and Custom URL are configured after boot; Worker-side upstream key/token are stored independently.

- `ota` has both `platform: esphome` (native OTA) and `platform: web_server`. WebUI v3 uses HTTP Basic on **HTTP port 80**, user `svitlobot`. It is not TLS: allow only trusted LAN/VPN and do not publish it to WAN. HA native API uses Noise encryption; prefer it for configuration when available.
- `web_server.log: false` plus manually enabled `Show Log` with `restore_mode: ALWAYS_OFF`. `logger.logs.http_request.idf: NONE` prevents the lower-level non-2xx log from exposing full Direct/HC URLs that carry credentials. Do not re-enable without a secure redesign.
- `http_request.verify_ssl: true` is active. All trusted requests have `follow_redirects: false`. Tests must check **active YAML values**, not a keyword appearing in a comment.
- Template text `mode: password` is UI masking, not write-only storage or encryption. Recorder exclusions/access controls are appropriate. A future write-only migration needs its own feature/test plan; never casually replace template keys and discard NVS restore keys.
- Full flash backups and compiled private binaries may contain sensitive credentials. Keep encrypted/local, never attach to a public issue or PR; no secure boot, binary signing or encryption-at-rest is claimed.
- Native OTA authorization for the **first migration** uses the password present in the *currently running* firmware, which can differ from secrets used for the new build. Check installed partition table, running target, signed-off backup and file SHA before uploading. Native OTA writes the inactive app partition; NVS should remain intact, but validate actual restored settings. No factory binary through WebUI; no blind flash erase.

## 6. Private CI acceptance contract

The private `Build / All-In-One` workflow gates as follows:

1. `python3 -m unittest discover -s .github/scripts -p 'test_*.py' -v` — validator fixtures and runtime source regression checks.
2. Compile the **actual** `components/shadow/shadow.cpp` against host scheduler/OTA stubs and run the host binary (tests cadence, no overlap, OTA start/abort/error/completed and epoch). Refer to the exact compiler invocation in the workflow.
3. `python3 .github/scripts/validate_c3_ota.py --source-only` — source/security preflight.
4. Require a **private** repository and all four required secrets; compile actual `all-in-one/aio_esp32c3.yaml` with ESPHome 2026.5.1.
5. Validate manifest version/file names/mandatory SHA-256, image header/chip family, actual binary and compiled partition table MD5/slot size. Export a **PRIVATE OTA-only** package with manifest and safe checksums. The `READY FOR OTA` job must be green before proposing any operator deployment.

`Workflow / Validate` and `Build / Tests` are additional gates. A green CI says the source/build/artifact passed those checks; it **cannot** prove hardware partition compatibility, NVS restoration, Relay end-to-end, future reliability, HTTPS/LAN privacy or watchdog behavior. After any source change requiring a new binary, obtain fresh hashes from that new run; never reuse the historical SHA above.

When changing source in one repo, mirror shared files to the other feature branch, compare content, check both Draft PRs/CI and identify the binary provenance. A documentation-only commit does not constitute a firmware rebuild.

## 7. Recorded physical smoke tests (2026-10-03)

One ESP32-C3 Super Mini, 4 MiB flash, installed partition table separately read and validated: two app slots of `0x1C0000` each and NVS near the flash end. Full 4 MiB flash backup taken locally before first migration. No backup bytes or secret values entered GitHub. Native upload used verified 3.5.61 OTA image; uploader reported success, device booted, WebUI:80 and native OTA:3232 reachable.

Observed (do not turn these into broader reliability claims):

1. Direct SvitloBot and Healthchecks successful; HC response `OK (HTTP 200: acknowledged)`.
2. Persistent Heartbeat Interval 70→90, one normal reboot, 90 restored; return to 70 without reflashing and heartbeat continued.
3. `Direct → Paused`: SvitloBot attempt count stayed at 14; HC continued; `Paused → Direct`: count increased to 15, HTTP 200.
4. Enter Relay URL/Token, `Custom Relay` returned HTTP 200; Cloudflare observed GET `/ping`, Authorization header present (redacted), TLS, and `esphome/all-in-one (3.5.61)` User-Agent.
5. Real SvitloBot last-contact time matched ESP completion within ~1 s, establishing end-to-end Worker forwarding.
6. Normal reboot restored `Custom Relay` and credential configuration. First request HTTP 429 was classified as failure; following request succeeded at the 70-second cadence. Real SvitloBot contact time again matched ~1 s later.
7. HC resumed after reboot with exact acknowledgment and **0 Delivery Errors**.

These observations are a functional smoke test. The first 429's origin (Worker vs upstream) was **not proven**. Custom URL was not exercised live. At the latest user checkpoint, the device was about to be moved to its permanent power supply for an untouched 24-hour observation. **Do not invent the result.**

## 8. Pending validation and safer roadmap

**Next non-destructive field checks:** after a full day collect uptime/reset reason, Wi-Fi signal, SvitloBot/HC Last Request + Delivery Errors, Restart Recommended, any 429/other failures from redacted Worker logs, and verify user-facing last-contact. Distinguish an intentional power-cycle from an unexpected reboot. After a reviewed 3.5.62 OTA, test SNTP with HA disconnected (DNS/UDP/123 allowed), both Send Now buttons (including Paused, active heartbeat, rapid double-click, 429 cooldown and OTA gate), matching Last Request and counter updates, and restored runtime settings. **No live 3.5.62 results are claimed here.**

**Pending controlled fault tests (only with owner approval):** Wi-Fi outage / DNS unavailable / TLS stall and watchdog/HA/WebUI responsiveness; credential/URL change during an in-flight request; simulated HC `OK (not found)` / `OK (rate limited)` / truncated body (prefer synthetic fixtures to disturbing production checks); Custom URL success and failure; OTA abort/error recovery with safe tooling, never intentionally power-cut an in-progress flash write. Check HTTP rejection never triggers auto-reboot. A 24-hour observation alone does not prove these cases.

**Potential future changes requiring separate designs:** avoid synchronous main-loop stalls while retaining correct ESPHome state isolation; write-only credential handling with preserved NVS migration; clearer effective firmware version entity; safe relay/upstream acknowledgment semantics and rate-limit observability; controlled logging and TLS transport for local administration. Preserve backward-compatible stored enum indices and old key until migration is explicitly verified.

## 9. Instructions to an AI continuing this project

1. Start by reading **this file**, `TRUSTED_AIO.md` and the **current** feature-branch source/CI; check branch head and PR state before modifying anything. History/earlier assistant guesses are less authoritative than the running revision and reproducible tests.
2. State assumptions and separate **source-inspected**, **CI-tested**, **hardware-observed** and **untested** assertions. Do not call `3.5.61` an upstream/general release.
3. Never request or echo live secret values, signed URLs, credential-bearing firmware or a 4 MiB flash dump in chat/public repos. Treat device IP/worker deployment specifics as operator configuration; examples should use placeholders.
4. Maintain public/private source parity without copying private CI credentials, binary artifacts or workflow secrets into public. Keep PRs Draft. Do not merge, flash, factory-reset, erase NVS, rotate credentials or alter the real Worker autonomously.
5. Before code changes, identify affected files, compatibility/NVS impacts, OTA/HTTP concurrency and security tradeoffs; keep scope incremental. Afterward update documentation, run relevant offline/host tests and a **new** private compile/artifact validation. User authorizes hardware tests step by step.
6. No silent Direct fallback, immediate retry storm, auto-reboot-on-HTTP-error, unchecked TLS, parallel duplicate timers, FreeRTOS deletion from OTA, misleading `HTTP 200` HC success or credential-bearing logs.
7. For operator support, give one controlled test at a time, define expected measurements, preserve rollback path, and keep genuine server/client outcomes distinct.

**Release decision:** successful source CI + first smoke test are necessary, not sufficient. Require completion/review of pending reliability/security checks and explicit owner approval before changing Draft PR state or distributing binaries.
