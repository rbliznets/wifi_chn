/*!
    \file
    \brief Unit test for WiFiStation/COTATask, covering the command
           {"wifi_ota":{"ssid":"Redmi_9430","password":"Foxtrot1",
           "file":"https://romasty.duckdns.org:7443/manet.bin","restart":false}}.
    \note  The "restart" field never reaches this component — it's handled by
           CLogicTask after OTA finishes, so it's not exercised here.
*/

#include "sdkconfig.h"

#if CONFIG_WIFICHN_OTA

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "WiFiStation.h"
#include "CDateTimeSystem.h"

static const char *TAG = "test_wifi_ota";
static const char *TEST_SSID = "Redmi_9430";
static const char *TEST_PASSWORD = "Foxtrot1";
static const char *TEST_OTA_URL = "https://romasty.duckdns.org:7443/manet.bin";

static volatile bool s_connected = false;
static volatile bool s_reached_server = false;
static volatile int16_t s_final_status = -1;
static int16_t s_last_progress_pct = -1;

static void onConnect(uint32_t *ip_addr)
{
    if (ip_addr != nullptr)
        s_connected = true;
}

static void onEvent(uint16_t id, const char *message)
{
    // "connecting was failed" here means a transient failure before an automatic
    // esp_wifi_connect() retry (see WiFiStation::event_handler), not a fatal error —
    // so the test doesn't abort on this event, it just keeps waiting until the overall timeout.
    (void)id;
    (void)message;
}

static void handleOtaProgress(uint16_t progress, int16_t status)
{
    // status 2/3 = COTATask connected to the server and is writing the image (see COTATask::event_ota_handler).
    if (status == 2 || status == 3)
        s_reached_server = true;
    // status==3: progress is the image write percentage (see COTATask::event_ota_handler, ESP_HTTPS_OTA_WRITE_FLASH).
    if (status == 3 && (int16_t)progress != s_last_progress_pct)
    {
        s_last_progress_pct = (int16_t)progress;
        ESP_LOGI(TAG, "OTA: %d%%", progress);
    }
    // The final call from COTATask::run() always uses progress==100.
    if ((status == 0) || (status == -1))
        s_final_status = status;
}

/// Verifies that WiFiStation/COTATask correctly use the ssid/password/file from
/// the wifi_ota command: connect to the access point and successfully complete
/// the HTTPS OTA against the given URL.
/// Requires real access to the "Redmi_9430" access point and the internet while running.
TEST_CASE("WiFiStation wifi_ota connect", "[wifi_chn]")
{
    s_connected = false;
    s_reached_server = false;
    s_final_status = -1;
    s_last_progress_pct = -1;

    // esp_wifi_init() requires NVS to already be initialized. In the real app this is
    // done by CNvsSystem::init() at startup; here (an isolated unity-test-app, where
    // no other component test touches WiFi) we need to do it ourselves.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    // Time is intentionally left unsynced here so the test also exercises the real
    // NTP wait (see the time-sync fix in COTATask::run()).
    // CDateTimeSystem::setDateTime(1767200000, true, false);

    TEST_ASSERT_TRUE(WiFiStation::Instance()->start(onConnect, onEvent, TEST_SSID, TEST_PASSWORD));

    int waited = 0;
    while (!s_connected && waited < 30000)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    TEST_ASSERT_TRUE_MESSAGE(s_connected, "failed to connect to Redmi_9430 within 30s - check that the access point is on and in range");

    TEST_ASSERT_TRUE(WiFiStation::Instance()->startOta(handleOtaProgress, TEST_OTA_URL));

    // Wait for the final OTA result (progress==100): 0 - success, 100 - canceled,
    // otherwise - an error code (see the res codes in COTATask::run()). Just "reached
    // the server" is not enough by itself - esp_https_ota_begin() must complete fully.
    // The timeout is generous: time isn't pre-synced, and NTP on this network has been
    // observed to take anywhere from a few seconds to ~13 minutes.
    waited = 0;
    while (s_final_status < 0 && waited < 1800000)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    if (s_final_status < 0)
    {
        TEST_FAIL_MESSAGE("COTATask did not finish OTA within the allotted time");
    }
    else if (s_final_status != 0)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "OTA finished with an error, status=%d (reached_server=%d)",
                 s_final_status, (int)s_reached_server);
        TEST_FAIL_MESSAGE(msg);
    }

    WiFiStation::Instance()->stopOta();
    WiFiStation::free();
    vTaskDelay(pdMS_TO_TICKS(100));
}

#endif // CONFIG_WIFICHN_OTA
