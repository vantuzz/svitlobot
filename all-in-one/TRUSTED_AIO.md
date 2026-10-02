# Trusted ESP32-C3 All-in-One

Only `all-in-one/aio_esp32c3.yaml` uses the trusted common, SvitloBot and delivery packages. The runtime-control and authenticated WebUI OTA profile is version 3.5.61. The shared HealthCheck package receives the safe URL-string-lifetime fix and strict server acknowledgment verification; this also changes HealthCheck success semantics for other profiles built from this feature branch. Custom URL only receives the safe URL-string-lifetime fix. The public `main` branch is unaffected.

## Build-time GitHub Actions Secrets

Compile this personalized firmware **only in a private repository or local environment**. The GitHub workflow refuses the ESP32-C3 build in a public repository: the compiled API encryption key and OTA/WebUI/fallback-AP credentials must not be exposed in downloadable public Actions artifacts. Keep the public `trusted-aio` branch source-only. Add these four Actions secrets to the **private** build repository before a manual `Build / All-In-One` run:

- `API_ENCRYPTION_KEY`: ESPHome API Noise key; base64 encoding of 32 random bytes.
- `OTA_PASSWORD`: native ESPHome OTA password.
- `WEB_PASSWORD`: WebUI password (username: `svitlobot`).
- `FALLBACK_AP_PASSWORD`: Wi-Fi recovery AP password (8+ characters).

The workflow creates a temporary `all-in-one/secrets.yaml` for the ESP32-C3 job only. No genuine secret belongs in git. Keep copies of the secrets, especially the API key for Home Assistant.

## Runtime configuration

`SvitloBot > Connection Mode` is a persistent select:

- `Direct` (default): needs `SvitloBot > Key`, sends HTTPS GET to the fixed SvitloBot API endpoint. This retains the existing key entity/restore state for OTA migration.
- `Custom Relay`: needs `SvitloBot > Relay URL` and `Relay Token`. URL must start with `https://`, have no query, fragment or embedded userinfo. Token must be at least 16 characters. ESP sends GET to the configured URL, with `Authorization: Bearer <token>`. It never includes the SvitloBot key in the relay request. Relay failures **never** fall back to Direct.

Relay URL and token are runtime text entities with `restore_value: true` and `mode: password`: neither is embedded in the repository or compiled firmware. The original SvitloBot key can be cleared after the relay is verified. ESPHome's password display mode masks the UI but is **not** encryption-at-rest or guaranteed redaction from authenticated WebUI JSON; restrict LAN and WebUI access.

Both connection modes share SvitloBot Status, Delivery Errors and Response Duration. Stock ESPHome WebUI may display all config fields simultaneously; the selected mode, not visibility of a field, determines which values are used.

Healthchecks remains a separate direct outbound HTTPS channel. Home Assistant uses encrypted native API on the LAN.

## Recovery diagnostics

This profile disables automatic reboots driven by SvitloBot, Healthchecks and Custom URL Delivery Errors. `Restart Recommended` becomes ON only when Wi-Fi is connected, both SvitloBot and Healthchecks have returned HTTP 200 in the current boot (with their current credentials/mode), and **both** have accumulated more than ten consecutive failed requests, with their most recent results both being transport failures. HTTP rejections, unknown Healthchecks UUIDs and rate limiting do not suggest a local ESP restart. This is a recommendation only: there is no automatic restart. The manual Restart button remains.

HTTP redirects are disabled for this trusted AIO profile, including Custom URL. HTTPS server certificates are verified.

## Provisioning order

1. Bring the trusted branch into a separate **private** build repository (or use a secure local checkout). Configure the four GitHub Actions secrets there and run `Build / All-In-One`; verify the ESP32-C3 build. Do not run a personalized firmware build as a public GitHub Actions artifact.
2. Keep the current Wi-Fi working while applying the normal OTA binary; do not erase NVS. Add the API encryption key to Home Assistant when prompted.
3. Deploy and test the Cloudflare Worker independently, with its own `SVITLOBOT_KEY` and `RELAY_TOKEN` secrets.
4. Enter Relay URL/Token in ESPHome via authenticated WebUI or (preferably) encrypted HA API. Change Connection Mode to Custom Relay and verify an HTTP 200 plus SvitloBot heartbeat.
5. Optionally clear the old Direct key after successful relay testing; retain a copy outside the ESP if you may use Direct again.

If the configured relay is down, SvitloBot delivery fails rather than exposing the WAN IP by falling back to Direct. A separate router WAN fallback can still expose the WAN IP to Healthchecks; this firmware does not alter router routing policy.

## Runtime controls and safe diagnostics

- **Heartbeat Interval** (AIO parent device): persistent ESPHome template number, default **70 s**, selectable **70–300 s** in steps of 5 s. It changes the single Shadow scheduler for SvitloBot, HealthCheck and Custom URL. The current Shadow sleep may finish with the old interval; the next cycle uses the new one. No firmware rebuild or additional task is required for ordinary interval changes. Set the Healthchecks.io check period and grace time to match your chosen cadence (for example, period 2 min and grace 3 min with a 70 s heartbeat; tailor to desired alert latency).
- **Connection Mode** now includes `Paused` as a third option: it suppresses **new** SvitloBot requests (an in-flight request may still finish), never falls back to Direct, and leaves HealthCheck and Custom URL running. `Paused` was appended after Direct and Custom Relay so previously stored ESPHome select indices remain valid across OTA.
- **Request Count** (diagnostic): counts actual SvitloBot HTTP attempts since boot, including repeated attempts with an unchanged response code; this is not persisted to NVS.
- **Delivery State** (diagnostic): summarizes the result or a locally skipped request (e.g. Paused, Invalid Relay URL, Waiting for Wi-Fi, HTTP 400) without publishing credential values. Response Code and Delivery Errors remain independent entities.
- **Last Request** (SvitloBot, HealthCheck and Custom URL sub-devices): last **completed HTTP attempt** and result, shown in WebUI/HA, e.g. `2026-10-02 19:24:10 UTC | FAIL (HTTP 400)` or `... | OK (HTTP 200)`. Updates on every completion, even when the response code remains unchanged; skipped/paused cycles do not overwrite it. The device obtains time via its encrypted Home Assistant API, but time availability never gates delivery. If time has not synchronized, the field instead shows `uptime Ns (clock not synced)`. The field contains no URL, token, key, or response body. HealthCheck succeeds **only** on HTTP 200 with an exact `OK` response body. A 200 with `OK (not found)`, `OK (rate limited)`, empty/truncated or unexpected body increments Delivery Errors and sets Status to OFF. Last Request states the safe cause (e.g. `FAIL (HTTP 200: rate limited)`), while Response Code remains the actual HTTP code. No UUID or response body is shown. Only 64 response bytes are captured; HTTP 200 requires a fully read response (`is_read_complete()`) and matching byte count, so an incomplete `OK` prefix of a longer body cannot pass. TLS validation and disabled redirects are retained.
- ESPHome 2026.5.1 templates accept `std::string` for request URLs, while request header callbacks require `const char *`. Relay authorization is backed by persistent string storage until ESPHome copies it into its request header value. Direct, Healthchecks and Custom URL callbacks return URL strings by value.
- ESP-IDF's HTTP client may log the **entire URL** for non-2xx responses. The trusted C3 profile suppresses the `http_request.idf` logger tag because Direct and Healthchecks URLs carry credentials. Service-level status codes remain visible. Do not enable the suppressed logger while those secrets are provisioned.

### Privacy limitation retained for compatibility

Runtime password-mode ESPHome text entities are masked in the editor but still expose their actual state to authorized HA clients and may appear in other authenticated WebUI/API surfaces. Recorder exclusions are still recommended. A true write-only credential migration needs a separate staged design and should **not** be combined with this scheduling/HTTP fix, because an incorrect migration could overwrite existing NVS values.

### Acceptance checklist before deploying

1. Compile the ESP32-C3 source in the separate **private** build repository with ESPHome 2026.5.1 and the existing four build-time secrets. The public fork and this PR contain source code only.
2. Verify that an OTA install preserves Wi-Fi/API/OTA credentials and restores Direct or Custom Relay by index; `Paused` is the third option.
3. In HA, set `Paused`: no SvitloBot requests, while Healthchecks continues. Then select Custom Relay and verify Request Count increases and Delivery State/Response Code reflect each result.
4. Change Heartbeat Interval to 90 s, reboot normally and confirm it restores; restore 70 s afterwards. Check Healthchecks period/grace settings before selecting long intervals.
5. Verify there is no credential value in emitted service diagnostics or ESP-IDF error logs. Never post token/key or personalized firmware publicly.
6. Check that each service's WebUI `Last Request` timestamp changes on each completed request even if its HTTP status code is unchanged. Verify that a cold boot without HA time sync shows the uptime fallback and still sends scheduled requests.
7. HealthCheck: check valid `HTTP 200 / OK`, unknown UUID (`HTTP 200 / OK (not found)`), rate limited (`HTTP 200 / OK (rate limited)`), unexpected/empty body, non-200 code and transport failure. Only the exact acknowledgment sets Status ON and clears errors. Never publish the real UUID or entire response. If live rate-limit testing would disturb the check, validate the classifier using synthetic fixtures instead.
8. Confirm HTTP rejections do not raise Restart Recommended; both last failures must be transport failures after both services previously succeeded and exceeded the existing threshold. Verify no automatic reboot. Do not attempt OTA before successful PRIVATE compile and user approval.

## HealthCheck validation and retry policy

- The Healthchecks.io Ping API uses an exact `OK` response body for a credited success; HTTP 200 alone also covers missing checks and rate limits. GET is retained so the response body can be validated. A missing or unrecognized acknowledgment fails closed; neither response body nor UUID is logged.
- Keep the shared Shadow cadence at 70 seconds by default (approximately 0.86 pings/minute per enabled check), and configure Healthchecks Period 2 minutes / Grace 3 minutes manually. If the runtime heartbeat interval changes, adjust the remote check accordingly.
- No immediate retries in this change: a new scheduled heartbeat already follows, and retrying a rate-limited or unknown check is counterproductive. The existing request timeout remains 15 seconds. Healthchecks.io recommends bounded retries in general, but this small persistent device does not need a separate retry scheduler for the initial release.
- Canonical local UUID syntax validation is deferred: this version preserves restored credentials and reports server rejection, rather than silently skipping a stored value. A future validation may be added with an explicit non-secret configuration diagnostic after private acceptance testing.

## Private CI release gate and WebUI OTA

The PRIVATE repository's `Build / All-In-One` workflow is scoped to ESP32-C3 and gates its installable artifact on source checks, real ESPHome 2026.5.1 compilation, actual OTA/factory SHA-256 against the ESPHome manifest, ESP32-C3 image header, and OTA image size against **both** compiled OTA slots from `partitions.bin`. Its unit tests use synthetic fixtures. The final `READY FOR OTA` job appears only after successful verification and private artifact upload. The package intentionally includes **only** `*.ota.bin`, safe metadata/checksums and installation instructions: factory images are not for WebUI OTA. Store credentials only in private Actions Secrets; the binary itself embeds build-time credentials, so do not expose artifacts outside the private repository.

`web_server.ota: false` was removed so the existing `ota: - platform: web_server` enables authenticated OTA in the regular local WebUI. **The current device may still run an older firmware with WebUI OTA disabled**: install this version the first time via the existing native ESPHome OTA channel, preserving flash/NVS; subsequent releases can use authenticated local WebUI and the `*.ota.bin` file. Never upload a `*.factory.bin` through WebUI.

ESPHome WebUI uses HTTP Basic authentication on port 80: the WebUI password can be observed on an untrusted LAN segment. Restrict device access to a trusted LAN/VPN, never expose port 80 to WAN, prefer native OTA when security of the local network is uncertain, and leave the existing native OTA password enabled. CI verifies the newly built partition layout, **not** the partition table installed on the physical device or NVS migration; inspect the device's first deployment path and perform a post-install heartbeat/relay test. CI status is not a guarantee of runtime availability or network privacy.

### Review notes for version 3.5.61

- Security: WebUI log streaming now starts **OFF** and the Show Log switch is opt-in (`ALWAYS_OFF`). ESP-IDF HTTP URL logging remains suppressed regardless of that switch. A compromised authenticated HA/WebUI client can still read current runtime password-mode entity state; the compatible write-only credential migration is deferred.
- WebUI OTA is an authenticated HTTP endpoint, **not HTTPS**; HTTP Basic credentials traverse the local network. Keep the device WebUI LAN/VPN-only and prefer native challenge-response OTA whenever the LAN cannot be trusted. Do not forward WebUI port 80 from WAN.
- Only the private CI's `READY FOR OTA` artifact should be used for routine WebUI updates. First switch from a previous version with web OTA disabled via native OTA. The CI's compiled OTA partition sizes cannot prove the layout installed on the physical device; review that first migration explicitly. CI does not confirm live relay/heartbeat function or preservation of NVS until hardware smoke testing.
- Shared packages still expose an optional user-configurable Custom URL, including HTTP URLs. Never put credentials in a plaintext HTTP URL, and restrict who can change this entity. Rejecting historic HTTP URLs automatically would be a potentially breaking migration and is not done here.

## Follow-up independent review: 3.5.61

- The ESP32-C3 profile now vendors the MIT-licensed Shadow component locally (`components/shadow`), adapted from the upstream pinned revision. The worker no longer deletes its FreeRTOS task on `OTA_STARTED`: it suspends NEW heartbeat starts and resumes on `OTA_ABORT`/`OTA_ERROR`. An in-flight HTTP request can still finish; OTA runtime behavior needs actual device verification.
- Shadow's interval and pause/configuration epoch are atomic. Worker task allocation is checked and `start()` does not create duplicates. Changing SvitloBot credentials or Connection Mode increments the epoch; stale SvitloBot response/error callbacks are dropped instead of incorrectly confirming changed settings.
- Direct Key, Relay URL and Relay Token updates explicitly reset SvitloBot Status, error counter, Response Code and success history. The accepted next request can re-arm success without relying on same-value binary sensor callbacks.
- The PRIVATE C3 CI source preflight reads active scalar settings rather than accepting a misleading `verify_ssl: false # verify_ssl: true` substring. Additional targeted regression tests cover the Shadow OTA, atomic fields and callback epoch wiring.
- **Review boundary:** an atomic interval does not by itself prove the ESPHome runtime entities are fully thread-safe when changed via WebUI/HA while a Shadow-script HTTP action executes. The external-task architecture still needs real concurrency/hardware stress testing or a larger immutable-snapshot / main-loop publication redesign. A green build/READY job is not a substitute for that test. Keep all PRs Draft and do not treat this revision as approved for automatic deployment.

## Architecture resolution: 3.5.61 main-loop heartbeat

The local MIT-adapted Shadow implementation no longer creates a FreeRTOS task or executes ESPHome script/entities from a separate worker. It uses one named timeout under ESPHome's main scheduler; initialization waits for the restored Heartbeat Interval before Shadow setup. Interval edits re-arm the pending timer. OTA_STARTED/OTA_COMPLETED suspend new starts, OTA_ABORT/OTA_ERROR resume on an ordinary tick. An already-running HTTP action is not forcibly terminated. This structurally removes the earlier cross-task access to mutable ESPHome text/select entities. The atomic configuration epoch remains as an extra guard against stale results. See `components/shadow/UPSTREAM.md`.

Tradeoff: main-loop HTTP operations are synchronous. With three services at 15 s each, network failure can delay main-loop responsiveness for around 45 s plus overhead. Validate Wi-Fi outage, TLS stall, watchdog behavior and WebUI/HA responsiveness on real hardware before rolling out. This CI release gate checks source and firmware image construction, not live behavior. The first migration still requires confirming the currently flashed partition layout; OTA from a prior image with disabled WebUI requires native ESPHome OTA. Do not expose local HTTP Basic WebUI to the public Internet.
