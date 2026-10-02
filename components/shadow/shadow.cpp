// Adapted from andrewjswan/esphome-components Shadow at
// 6b0202c47a5629358624271d062196b17ad3a340 (MIT, see LICENSE).
#include "shadow.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome::shadow {

void Shadow::setup() {
  ESP_LOGCONFIG(TAG, "Setting up shadow...");
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
  this->start();
}

#ifdef USE_OTA_STATE_LISTENER
void Shadow::on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) {
  if (state == ota::OTA_STARTED) {
    // No new heartbeat requests during an OTA. A request already in flight may
    // complete; do not terminate another task inside an ESPHome action.
    this->stop();
  } else if (state == ota::OTA_ABORT || state == ota::OTA_ERROR) {
    // A failed/aborted OTA must not permanently disable heartbeat.
    this->start();
  }
}
#endif

void Shadow::start() {
  this->suspended_.store(false, std::memory_order_release);
  if (this->shadow_handle != nullptr) {
    xTaskNotifyGive(this->shadow_handle);
    return; // Resume: never create a duplicate task.
  }
  const BaseType_t result = xTaskCreatePinnedToCore(
      Shadow::shadow_function, TAG, 8192, this, this->shadow_priority_,
      &this->shadow_handle, tskNO_AFFINITY);
  if (result != pdPASS || this->shadow_handle == nullptr) {
    this->shadow_handle = nullptr;
    ESP_LOGE(TAG, "Failed to create heartbeat task");
    this->mark_failed();
  }
}

void Shadow::shadow_function(void *params) {
  auto *owner = static_cast<Shadow *>(params);
  if (owner->startup_delay_ > 0) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(owner->startup_delay_ * 1000));
  }
  for (;;) {
    if (owner->suspended_.load(std::memory_order_acquire)) {
      // OTA may fail or abort. Keep the task, wait until resumed, and ensure
      // nobody can call vTaskDelete(nullptr) to delete the OTA callback task.
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    owner->execute_script();
    const uint32_t interval = std::max<uint32_t>(1, owner->shadow_interval_.load(std::memory_order_acquire));
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval * 1000));
  }
}

void Shadow::execute_script() {
  if (this->script == nullptr) {
    ESP_LOGE(TAG, "Heartbeat script missing");
    return;
  }
  if (this->suspended_.load(std::memory_order_acquire)) return;
  if (this->script->is_running()) {
    ESP_LOGD(TAG, "Previous heartbeat still running; skip overlapping launch");
    return;
  }
  this->script->execute();
}

void Shadow::stop() {
  this->suspended_.store(true, std::memory_order_release);
  if (this->shadow_handle != nullptr) xTaskNotifyGive(this->shadow_handle);
}

void Shadow::set_script(script::Script<> *script) {
  this->script = script;
  ESP_LOGCONFIG(TAG, "Registered heartbeat script");
}

void Shadow::dump_config() {
  ESP_LOGCONFIG(TAG, "Shadow: local OTA-safe fork of upstream %s", SHADOW_VERSION);
  ESP_LOGCONFIG(TAG, " Startup delay: %us", this->startup_delay_);
  ESP_LOGCONFIG(TAG, "      Interval: %us", this->shadow_interval_.load(std::memory_order_acquire));
  ESP_LOGCONFIG(TAG, "      Priority: %u", this->shadow_priority_);
}

} // namespace esphome::shadow
