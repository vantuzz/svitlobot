#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/script/script.h"
#include <atomic>

#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

namespace esphome::shadow {

static const char *const SHADOW_VERSION = "2026.7.1";
static const char *const TAG = "shadow";

class Shadow final : public Component
#ifdef USE_OTA_STATE_LISTENER
    ,
                     public ota::OTAGlobalStateListener
#endif
{
 public:
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void setup() override;
  void start();
  // Suspend new requests while OTA is active; never forcibly delete a running task.
  void stop();

  void dump_config() override;

  void set_script(script::Script<> *script);
  void set_shadow_interval(uint32_t shadow_interval) { this->shadow_interval_.store(shadow_interval, std::memory_order_release); }
  void set_startup_delay(uint32_t startup_delay) { this->startup_delay_ = startup_delay; }
  void set_shadow_priority(uint8_t shadow_priority) { this->shadow_priority_ = shadow_priority; }

#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) override;
#endif

 protected:
  TaskHandle_t shadow_handle{nullptr};
  script::Script<> *script{nullptr};
  std::atomic<uint32_t> shadow_interval_{60};
  std::atomic<bool> suspended_{false};
  uint32_t startup_delay_{0};
  uint8_t shadow_priority_{1};

  void execute_script();

  static void shadow_function(void *params);
};  // Shadow

}  // namespace esphome::shadow
