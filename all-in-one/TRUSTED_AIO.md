# Trusted ESP32-C3 All-in-One

Only `all-in-one/aio_esp32c3.yaml` uses the trusted common, SvitloBot and delivery packages. Other firmware profiles and `main` are unaffected.

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

This profile disables automatic reboots driven by SvitloBot, Healthchecks and Custom URL Delivery Errors. `Restart Recommended` becomes ON only when Wi-Fi is connected, both SvitloBot and Healthchecks have returned HTTP 200 in the current boot (with their current credentials/mode), and **both** have accumulated more than ten consecutive failed requests. This is a recommendation only: there is no automatic restart. The manual Restart button remains.

HTTP redirects are disabled for this trusted AIO profile, including Custom URL. HTTPS server certificates are verified.

## Provisioning order

1. Bring the trusted branch into a separate **private** build repository (or use a secure local checkout). Configure the four GitHub Actions secrets there and run `Build / All-In-One`; verify the ESP32-C3 build. Do not run a personalized firmware build as a public GitHub Actions artifact.
2. Keep the current Wi-Fi working while applying the normal OTA binary; do not erase NVS. Add the API encryption key to Home Assistant when prompted.
3. Deploy and test the Cloudflare Worker independently, with its own `SVITLOBOT_KEY` and `RELAY_TOKEN` secrets.
4. Enter Relay URL/Token in ESPHome via authenticated WebUI or (preferably) encrypted HA API. Change Connection Mode to Custom Relay and verify an HTTP 200 plus SvitloBot heartbeat.
5. Optionally clear the old Direct key after successful relay testing; retain a copy outside the ESP if you may use Direct again.

If the configured relay is down, SvitloBot delivery fails rather than exposing the WAN IP by falling back to Direct. A separate router WAN fallback can still expose the WAN IP to Healthchecks; this firmware does not alter router routing policy.

## Runtime controls and safe diagnostics

- **Heartbeat Interval** (AIO parent device): persistent ESPHome template number, default **70 s**, selectable **70–300 s** in steps of 5 s. It changes the single Shadow scheduler for SvitloBot, HealthCheck and Custom URL. The current Shadow sleep may finish with the old interval; the next cycle uses the new one. No firmware rebuild or additional task is required for ordinary interval changes. Set the Healthchecks.io check period and grace time to match your chosen cadence (e.g. period 2 min and grace 5 min with a 70 s heartbeat).
- **Connection Mode** now includes `Paused` as a third option: it suppresses **only** SvitloBot requests, never falls back to Direct, and leaves HealthCheck and Custom URL running. `Paused` was appended after Direct and Custom Relay so previously stored ESPHome select indices remain valid across OTA.
- **Request Count** (diagnostic): counts actual SvitloBot HTTP attempts since boot, including repeated attempts with an unchanged response code; this is not persisted to NVS.
- **Delivery State** (diagnostic): summarizes the result or a locally skipped request (e.g. Paused, Invalid Relay URL, Waiting for Wi-Fi, HTTP 400) without publishing credential values. Response Code and Delivery Errors remain independent entities.
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
