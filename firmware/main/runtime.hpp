#pragma once
#include "sirokko/ui.hpp"
#include "sirokko/service.hpp"

enum class FlashPurpose : uint8_t { LOCAL_SETTINGS, OTA, AUTOSAVE };
bool runtime_snapshot(sirokko::UiFrame*);
void runtime_remote_stop();
sirokko::ServiceResult runtime_acquire_flash(FlashPurpose);
bool runtime_flash_safe();
bool runtime_checkpoint();
void runtime_release_flash();
uint32_t runtime_control_passes();

bool service_network_begin();
bool service_network_configured();
bool service_network_connected();
// Called only from the local USB service CLI under a LOCAL_SETTINGS flash lease.
// Input is bounded base64 JSON, never logged or echoed; takes effect after reboot.
bool service_network_import(const char* encoded);
