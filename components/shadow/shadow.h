#pragma once

// Main-loop adaptation of Andrew J. Swan's MIT-licensed Shadow component.
// All ESPHome script/entity access is serialized on ESPHome's scheduler.
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/script/script.h"
#include <atomic>
#include <cstdint>

#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

namespace esphome::shadow {

static const char *const SHADOW_VERSION = "2026.7.1-main-loop";
static const char *const TAG = "shadow";

class Shadow final : public Component
#ifdef USE_OTA_STATE_LISTENER
    , public ota::OTAGlobalStateListener
#endif
{
 public:
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  void setup() override;
  void dump_config() override;

  // OTA callbacks only touch this atomic flag. They do not mutate the scheduler.
  void start();
  void stop();

  void set_script(script::Script<> *script) { this->script_ = script; }
  void set_shadow_interval(uint32_t seconds);
  // Manual launches share the main-loop serialization gate with scheduled AIO.
  // Call end_manual() after the selected service script has completed.
  bool begin_manual();
  void end_manual();
  void set_startup_delay(uint32_t seconds) { this->startup_delay_ = seconds; }

  // Version checks are retained as a second defense against delayed callbacks.
  uint32_t config_epoch() const { return this->config_epoch_.load(std::memory_order_acquire); }
  void invalidate_config() { this->config_epoch_.fetch_add(1, std::memory_order_acq_rel); }

#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                           ota::OTAComponent *comp) override;
#endif

 protected:
  script::Script<> *script_{nullptr};
  std::atomic<uint32_t> shadow_interval_{60};
  std::atomic<bool> suspended_{false};
  std::atomic<uint32_t> config_epoch_{0};
  uint32_t startup_delay_{0};
  bool first_tick_complete_{false};  // Only touched on ESPHome's main loop.
  bool manual_active_{false};  // Also main-loop only; OTA uses suspended_ atomically.

  void execute_script_();
  void schedule_next_();
};

} // namespace esphome::shadow
