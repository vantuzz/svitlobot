// Main-loop adaptation of Andrew J. Swan's MIT-licensed Shadow at
// 6b0202c47a5629358624271d062196b17ad3a340; see LICENSE.
#include "shadow.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome::shadow {

void Shadow::setup() {
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
  // ESPHome component timers execute in its main loop; no extra FreeRTOS task.
  // TemplateNumber restores its cadence at HARDWARE priority, ahead of us.
  this->set_timeout("shadow_initial", this->startup_delay_ * 1000U, [this]() {
    this->first_tick_complete_ = true;
    this->execute_script_();
    this->schedule_next_();
  });
}

void Shadow::execute_script_() {
  if (this->suspended_.load(std::memory_order_acquire)) return;
  if (this->script_ == nullptr) {
    ESP_LOGE(TAG, "Heartbeat script missing");
    this->mark_failed();
    return;
  }
  if (this->manual_active_ || this->script_->is_running()) {
    ESP_LOGD(TAG, "Previous heartbeat still running; skip overlapping launch");
    return;
  }
  this->script_->execute();
}

void Shadow::schedule_next_() {
  const uint32_t seconds = std::max<uint32_t>(1, this->shadow_interval_.load(std::memory_order_acquire));
  // Re-arm one named timer after the preceding callback. Updating cadence replaces
  // this pending timer, without spawning a second heartbeat scheduler.
  this->set_timeout("shadow_next", seconds * 1000U, [this]() {
    this->execute_script_();
    this->schedule_next_();
  });
}

bool Shadow::begin_manual() {
  // First automatic tick establishes the scheduler. Never start during OTA,
  // an active composite heartbeat, or another manual request.
  if (!this->first_tick_complete_ || this->suspended_.load(std::memory_order_acquire) ||
      this->manual_active_ || this->script_ == nullptr || this->script_->is_running()) {
    return false;
  }
  this->manual_active_ = true;
  return true;
}

void Shadow::end_manual() {
  if (!this->manual_active_) return;
  this->manual_active_ = false;
  // A manual request replaces, rather than duplicates, the pending cycle.
  // This also prevents an immediate scheduled follow-up after a manual ping.
  if (this->first_tick_complete_) this->schedule_next_();
}

void Shadow::set_shadow_interval(uint32_t seconds) {
  this->shadow_interval_.store(std::max<uint32_t>(1, seconds), std::memory_order_release);
  // ESPHome number callbacks run on the same main-loop scheduler as this timer.
  // Before setup / first callback merely store the restored interval.
  if (this->first_tick_complete_) this->schedule_next_();
}

void Shadow::stop() {
  // Do not stop an already running request: no FreeRTOS task is deleted or
  // ESPHome script state manipulated from an OTA transport callback.
  this->suspended_.store(true, std::memory_order_release);
}

void Shadow::start() {
  this->suspended_.store(false, std::memory_order_release);
  // The named timer keeps running through OTA and will resume on its next tick.
}

#ifdef USE_OTA_STATE_LISTENER
void Shadow::on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                                 ota::OTAComponent *comp) {
  if (state == ota::OTA_STARTED || state == ota::OTA_COMPLETED) {
    this->stop();
  } else if (state == ota::OTA_ABORT || state == ota::OTA_ERROR) {
    this->start();
  }
}
#endif

void Shadow::dump_config() {
  ESP_LOGCONFIG(TAG, "Shadow: main-loop scheduler (upstream %s)", SHADOW_VERSION);
  ESP_LOGCONFIG(TAG, " Initial delay: %u s", this->startup_delay_);
  ESP_LOGCONFIG(TAG, " Cadence: %u s", this->shadow_interval_.load(std::memory_order_acquire));
}

} // namespace esphome::shadow
