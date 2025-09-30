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
            // ESP_LOGI(TAG, "OTA finish");
            break;
        case ESP_HTTPS_OTA_ABORT:
            // ESP_LOGW(TAG, "OTA abort");
            break;
        }
    }
}

void COTATask::run()
{
#ifndef CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE
    UBaseType_t m1 = uxTaskGetStackHighWaterMark2(nullptr);
#endif
#if CONFIG_PM_ENABLE
	esp_pm_lock_handle_t mPMLock; ///< флаг запрета на понижение частоты CPU
	esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "ota", &mPMLock);
	esp_pm_lock_acquire(mPMLock);
#endif

    esp_http_client_config_t cfgHTTPS;
    memset(&cfgHTTPS, 0, sizeof(cfgHTTPS));
    // cfgHTTPS.addr_type = HTTP_ADDR_TYPE_INET;
    cfgHTTPS.url = mPath.c_str();
    // cfgHTTPS.skip_cert_common_name_check = true;
    // cfgHTTPS.cert_pem = (char *)server_cert_pem_start;
    cfgHTTPS.crt_bundle_attach = esp_crt_bundle_attach;
    // Указатель на TLS-сертификат
    cfgHTTPS.use_global_ca_store = true;
    cfgHTTPS.buffer_size_tx = 2048;
    cfgHTTPS.buffer_size = 4096;
    esp_https_ota_config_t ota_config = {
        .http_config = &cfgHTTPS,
        .partial_http_download = false,
#if CONFIG_SPIRAM
        .buffer_caps = MALLOC_CAP_SPIRAM,
#else
        .buffer_caps = MALLOC_CAP_DEFAULT,
#endif
        .ota_resumption = false,
#if CONFIG_SPIRAM
        .ota_image_bytes_written = 64 * 1024
#else
        .ota_image_bytes_written = 8 * 1024
#endif
    };

    int16_t res = 0;
    while (true)
    {
        if (ESP_OK != esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &event_ota_handler, this))
        {
            res = 101;
            break;
        }

        esp_https_ota_handle_t https_ota_handle;
        if (ESP_OK != esp_https_ota_begin(&ota_config, &https_ota_handle))
        {
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