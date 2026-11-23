#ifndef OTA_CONTROL_H
#define OTA_CONTROL_H

#include <Arduino.h>

// =============================================================================
//  otaControl.h — Guarded HTTP/HTTPS firmware update.
//
//  The firmware version is a compile-time constant (FW_VERSION in appConfig.h),
//  bumped by ship-ota.ps1. performOtaUpdate() enforces a host allowlist and a
//  free-heap precheck before flashing; the MQTT layer enforces the shared-secret
//  token. otaMarkAppValid() confirms a healthy boot so the bootloader does not
//  roll the image back.
// =============================================================================

// Download + flash from url. Reboots on success. Validates host against
// cfg.otaHostAllow and refuses below OTA_MIN_FREE_HEAP. Uses TLS for https URLs.
void performOtaUpdate(const String& url);

// Cancel pending-verify rollback once the new image has proven healthy (network
// up). No-op unless the bootloader booted this image in PENDING_VERIFY state.
void otaMarkAppValid();

#endif // OTA_CONTROL_H
