#include "otaControl.h"
#include "configControl.h"
#include "appConfig.h"
#include "coreLogic.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

// Extract the host portion of a URL (parsing logic lives in coreLogic.h).
static String urlHost(const String& url) {
    size_t start, len;
    if (!core::urlHostSpan(url.c_str(), start, len)) return "";
    return url.substring(start, start + len);
}

static bool hostAllowed(const String& url) {
    if (cfg.otaHostAllow.length() == 0) {
        Serial.println("[OTA] WARNING: no host allowlist set — accepting any origin");
        return true;
    }
    String host = urlHost(url);
    bool ok = core::hostMatchesAllow(host.c_str(), cfg.otaHostAllow.c_str());
    if (!ok) {
        Serial.printf("[OTA] host '%s' not in allowlist '%s' — rejected\n",
                      host.c_str(), cfg.otaHostAllow.c_str());
    }
    return ok;
}

void performOtaUpdate(const String& url) {
    Serial.println("================ OTA ================");
    Serial.printf("[OTA] Current version : %s\n", FW_VERSION);
    Serial.printf("[OTA] URL             : %s\n", url.c_str());
    Serial.printf("[OTA] Free heap       : %u\n", ESP.getFreeHeap());
    Serial.println("=====================================");

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected — aborting");
        return;
    }
    if (url.length() == 0) {
        Serial.println("[OTA] Empty URL — aborting");
        return;
    }
    if (ESP.getFreeHeap() < OTA_MIN_FREE_HEAP) {
        Serial.printf("[OTA] Free heap below %u — aborting to avoid a failed flash\n",
                      OTA_MIN_FREE_HEAP);
        return;
    }
    if (!hostAllowed(url)) return;

    httpUpdate.rebootOnUpdate(true);
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    // Feed the task watchdog during the (potentially long) flash so a normal
    // download cannot trip it. esp_task_wdt_reset() is a no-op if this task is
    // not subscribed, so it is always safe to call here.
    httpUpdate.onProgress([](int cur, int total) {
        static int lastPct = -1;
        int pct = total ? (cur * 100 / total) : 0;
        if (pct != lastPct && (pct % 10 == 0)) {
            Serial.printf("[OTA] %d%%\n", pct);
            lastPct = pct;
        }
        esp_task_wdt_reset();
    });

    t_httpUpdate_return ret;
    bool isHttps = url.startsWith("https://");
    if (isHttps) {
        // Production: pin a CA (setCACert / setCACertBundle). setInsecure() here
        // is the TLS-ready hook against a known origin on a trusted network.
        Serial.println("[OTA] HTTPS — WARNING: server cert NOT validated");
        WiFiClientSecure secure;
        secure.setInsecure();
        ret = httpUpdate.update(secure, url);
    } else {
        WiFiClient client;
        ret = httpUpdate.update(client, url);
    }

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] FAILED (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No updates available at that URL");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] Update OK — rebooting...");
            break;
    }
}

void otaMarkAppValid() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.println("[OTA] New image confirmed healthy — rollback cancelled");
        }
    }
}
