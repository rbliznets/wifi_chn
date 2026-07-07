/*!
    \file
    \brief Класс задачи для HTTPS OTA.
    \authors Близнец Р.А. (r.bliznets@gmail.com)
    \version 0.0.0.1
    \date 24.09.2025
*/

#include "COTATask.h"
#include "CTrace.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include <cstring>
#include "esp_pm.h"
#include "CDateTimeSystem.h"

#if CONFIG_WIFICHN_OTA
static const char *TAG = "ota";

COTATask::COTATask(WiFiStation *parent, const char *file) : CBaseTask(), mParent(parent), mPath(file)
{
    CBaseTask::init(OTATASK_NAME, OTATASK_STACKSIZE, OTATASK_PRIOR, OTATASK_LENGTH, OTATASK_CPU);
}

COTATask::~COTATask()
{
    mCancel = true;
    do
    {
        vTaskDelay(1);
    } while (mTaskQueue != nullptr);
}

void COTATask::event_ota_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT)
    {
        COTATask *ota = (COTATask *)arg;
        switch (event_id)
        {
        case ESP_HTTPS_OTA_START:
            if (ota->mParent->mOtaProgressCallback != nullptr)
                ota->mParent->mOtaProgressCallback(0, 1);
            // ESP_LOGI(TAG, "OTA started");
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            if (ota->mParent->mOtaProgressCallback != nullptr)
                ota->mParent->mOtaProgressCallback(0, 2);
            // ESP_LOGI(TAG, "Connected to server");
            break;
        case ESP_HTTPS_OTA_GET_IMG_DESC:
            // ESP_LOGW(TAG, "Reading Image Description");
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
            // ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_REVISION:
            // ESP_LOGI(TAG, "Verifying chip revision of new image: %d", *(esp_chip_id_t *)event_data);
            break;
        case ESP_HTTPS_OTA_DECRYPT_CB:
            // ESP_LOGW(TAG, "Callback to decrypt function");
            break;
        case ESP_HTTPS_OTA_WRITE_FLASH:
            if (ota->mParent->mOtaProgressCallback != nullptr)
            {
                uint16_t prg = ((*(int *)event_data) * 100) / ota->mImageSize;
                if (ota->mProgress != prg)
                {
                    ota->mProgress = prg;
                    ota->mParent->mOtaProgressCallback(prg, 3);
                }
            }
            // ESP_LOGW(TAG, "Writing to flash: %d written", *(int *)event_data);
            break;
        case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
            // ESP_LOGW(TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
            break;
        case ESP_HTTPS_OTA_FINISH:
            if (ota->mParent->mOtaProgressCallback != nullptr)
            {
                ota->mParent->mOtaProgressCallback(100, 4);
            }
            // ESP_LOGI(TAG, "OTA finish");
            break;
        case ESP_HTTPS_OTA_ABORT:
            if (ota->mParent->mOtaProgressCallback != nullptr)
            {
                ota->mParent->mOtaProgressCallback(ota->mProgress, -1);
            }
            // ESP_LOGW(TAG, "OTA abort");
            break;
        }
    }
}

void COTATask::run()
{
    // Временная диагностика TLS-рукопожатия при OTA.
    // esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
    // esp_log_level_set("esp-tls-mbedtls", ESP_LOG_DEBUG);
    // esp_log_level_set("esp_https_ota", ESP_LOG_DEBUG);
    // esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_DEBUG);
    // esp_log_level_set("HTTP_CLIENT", ESP_LOG_DEBUG);
    // esp_log_level_set("transport_base", ESP_LOG_DEBUG);
    // esp_log_level_set("transport", ESP_LOG_DEBUG);

#ifndef CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE
    UBaseType_t m1 = uxTaskGetStackHighWaterMark2(nullptr);
#endif
#if CONFIG_PM_ENABLE
    esp_pm_lock_handle_t mPMLock; ///< флаг запрета на понижение частоты CPU
    esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "ota", &mPMLock);
    esp_pm_lock_acquire(mPMLock);
#endif

    // На время закачки отключаем WiFi power-save (по умолчанию WIFI_PS_MIN_MODEM):
    // модемный сон добавляет задержку на каждый пакет (ожидание DTIM-интервала),
    // что заметно тормозит именно передачу большого образа по HTTPS.
    wifi_ps_type_t prevPsType = WIFI_PS_MIN_MODEM;
    esp_wifi_get_ps(&prevPsType);
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_http_client_config_t cfgHTTPS;
    memset(&cfgHTTPS, 0, sizeof(cfgHTTPS));
    // cfgHTTPS.addr_type = HTTP_ADDR_TYPE_INET;
    cfgHTTPS.url = mPath.c_str();
    // cfgHTTPS.skip_cert_common_name_check = true;
    // cfgHTTPS.cert_pem = (char *)server_cert_pem_start;
    cfgHTTPS.crt_bundle_attach = esp_crt_bundle_attach;
    cfgHTTPS.buffer_size_tx = 2048;
#if CONFIG_SPIRAM
    cfgHTTPS.buffer_size = 16384; // с PSRAM можно позволить буфер побольше — меньше циклов чтения на закачку.
#else
    cfgHTTPS.buffer_size = 4096;
#endif
    cfgHTTPS.timeout_ms = 10000;
    esp_https_ota_config_t ota_config = {
        .http_config = &cfgHTTPS,
        .http_client_init_cb = nullptr,
#if CONFIG_ESP_HTTPS_OTA_ENABLE_PARTIAL_DOWNLOAD
        .partial_http_download = false,
#endif
#if CONFIG_SPIRAM
        .buffer_caps = MALLOC_CAP_SPIRAM,
#else
        .buffer_caps = MALLOC_CAP_DEFAULT,
#endif
        .ota_resumption = false,
    };

    // if (!CDateTimeSystem::isSync() && mParent->mStartSyncTime)
    // {
    //     // Проверка TLS-сертификата сервера требует верного системного времени,
    //     // поэтому ждём завершения NTP-синхронизации по WiFi (WiFiStation::mStartSyncTime),
    //     // запущенной при подключении, прежде чем начинать HTTPS OTA.
    //     // ESP_LOGI(TAG, "Waiting for time sync before HTTPS OTA");
    //     uint16_t cnt = 5;
    //     while (mParent->mStartSyncTime && !mCancel)
    //     {
    //         if(cnt == 5)
    //         {
    //             if (mParent->mOtaProgressCallback != nullptr)
    //                 mParent->mOtaProgressCallback(0, 10);
    //             cnt = 0;
    //         }
    //         vTaskDelay(pdMS_TO_TICKS(200));
    //         cnt++;
    //     }
    // }

    int16_t res = 0;
    if (mCancel)
        res = 100;
    else
        while (true)
        {
            if (ESP_OK != esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &event_ota_handler, this))
            {
                res = 101;
                break;
            }

            esp_https_ota_handle_t https_ota_handle;
            esp_err_t begin_err;
            // Обрыв связи на этом этапе (до записи во флеш) не фатален - это ещё не начало
            // закачки, а лишь установка HTTPS-соединения. Точки доступа вроде мобильных
            // хотспотов иногда на несколько минут теряют исходящую связность, поэтому
            // повторяем попытку вместо немедленного отказа.
            for (uint8_t attempt = 0; attempt < OTATASK_BEGIN_RETRIES; attempt++)
            {
                begin_err = esp_https_ota_begin(&ota_config, &https_ota_handle);
                if (ESP_OK == begin_err || mCancel)
                    break;
                ESP_LOGW(TAG, "esp_https_ota_begin failed: %s (attempt %d/%d)", esp_err_to_name(begin_err), attempt + 1, OTATASK_BEGIN_RETRIES);
                vTaskDelay(pdMS_TO_TICKS(OTATASK_RETRY_DELAY_MS));
            }
            if (mCancel)
            {
                res = 100;
                break;
            }
            if (ESP_OK != begin_err)
            {
                ESP_LOGE(TAG, "esp_https_ota_begin failed after %d attempts: %s", OTATASK_BEGIN_RETRIES, esp_err_to_name(begin_err));
                res = 102;
                break;
            }

            mImageSize = esp_https_ota_get_image_size(https_ota_handle);
            if (mImageSize < 0)
            {
                res = 112;
                break;
            }

            if (mParent->mOtaImageDesc != nullptr)
            {
                esp_app_desc_t desc;
                if (ESP_OK != esp_https_ota_get_img_desc(https_ota_handle, &desc))
                {
                    res = 113;
                    break;
                }
                mParent->mOtaImageDesc(desc);
                // ESP_LOGI(TAG, "%s,%s,%s,%s",desc.project_name,desc.version,desc.date,desc.time);
            }

            while (true)
            {
                if (mCancel)
                {
                    res = 100;
                    esp_https_ota_abort(https_ota_handle);
                    break;
                }

                esp_err_t err = esp_https_ota_perform(https_ota_handle);
                if (ESP_OK == err)
                    break;
                if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
                {
                    res = 103;
                    esp_https_ota_abort(https_ota_handle);
                    break;
                }
            }
            if (res != 0)
                break;

            if (mCancel)
            {
                res = 100;
                esp_https_ota_abort(https_ota_handle);
                break;
            }

            if (ESP_OK != esp_https_ota_finish(https_ota_handle))
            {
                res = 104;
            }

            break;
        }
    if (mParent->mOtaProgressCallback != nullptr)
    {
        mParent->mOtaProgressCallback(100, res);
    }

    esp_wifi_set_ps(prevPsType);

#if CONFIG_PM_ENABLE
    esp_pm_lock_release(mPMLock);
    esp_pm_lock_delete(mPMLock);
#endif

#ifndef CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE
    UBaseType_t m2 = uxTaskGetStackHighWaterMark2(nullptr);
    if (m2 != m1)
    {
        m1 = m2;
        TDEC("free ota stack", m2);
    }
#endif
}
#endif