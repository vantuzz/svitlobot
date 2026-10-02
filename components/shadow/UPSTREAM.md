# Local Shadow adaptation (MIT)

Derived from Andrew J. Swan's `andrewjswan/esphome-components/components/shadow`, pinned upstream commit `6b0202c47a5629358624271d062196b17ad3a340`. The copyright and MIT terms are retained in LICENSE.

## Changes from upstream

- Schedule the script **on ESPHome's own main-loop Component timers**, not a custom FreeRTOS task. This keeps runtime text/select entity reads, HTTP callback state changes, and HA/WebUI configuration callbacks in one ESPHome event-loop context.
- Execute the first heartbeat after `startup_delay`; re-arm exactly one named `shadow_next` timeout using the current runtime cadence. Changing cadence replaces its pending timeout; a running request is not aborted.
- Only an atomic pause flag is accessed from an OTA callback. `OTA_STARTED` / `OTA_COMPLETED` pause new heartbeats; `OTA_ABORT` / `OTA_ERROR` resume at the next ordinary tick. An in-flight request may still finish; there is no task deletion, scheduler mutation or ESPHome script manipulation from OTA callbacks.
- Atomic configuration epoch remains as defense-in-depth against stale responses after mode/credential changes.
- Removed obsolete FreeRTOS `priority` option. No separate task/stack is allocated.

## Tradeoff and limits

ESPHome `http_request` actions are synchronous: with three sequential outbound services and a configured 15-second timeout each, a worst-case cycle can delay the main loop for up to roughly 45 seconds plus overhead (not a timing guarantee). The old worker architecture avoided this but was unsafe with mutable ESPHome entities. Network-failure, watchdog and WebUI responsiveness require hardware stress tests. Existing device partition layout and runtime NVS migration are not validated by source-only tests. This is not firmware signing or secure boot.
