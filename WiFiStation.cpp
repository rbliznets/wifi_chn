/*!
    \file
    \brief Класс для подключения к WiFi роутеру.
    \authors Близнец Р.А. (r.bliznets@gmail.com)
    \version 0.0.0.1
    \date 10.09.2024
*/

#include "WiFiStation.h"
#include "esp_log.h"
#include <arpa/inet.h>
#if CONFIG_WIFICHN_UDP
#include "tasks/CUDPOut.h"
#include "tasks/CUDPInTask.h"
#endif
#if CONFIG_WIFICHN_TCP
#include "tasks/CTCPClientTask.h"
#endif
#include "CTrace.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "CDateTimeSystem.h"

#include "esp_https_ota.h"
#include "esp_crt_bundle.h"

static const char *TAG = "wifi";

WiFiStation *WiFiStation::theSingleInstance = nullptr;

WiFiStation::WiFiStation()
{
    std::memset(&m_wifi_config, 0, sizeof(m_wifi_config));
    m_wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
}

WiFiStation::~WiFiStation()
{
    stop();
}

void WiFiStation::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (WiFiStation::Instance()->mSrcIP != 0)
        {
            WiFiStation::Instance()->mSrcIP = 0;
            if (WiFiStation::Instance()->mConnectCallback != nullptr)
                WiFiStation::Instance()->mConnectCallback(nullptr);
        }
        if(WiFiStation::Instance()->mConnecting)
        {
            esp_wifi_connect();
            ESP_LOGW(TAG, "connect to the AP fail");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        // ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        WiFiStation::Instance()->mSrcIP = event->ip_info.ip.addr;
#if (CONFIG_WIFICHN_SYNC_TIME == 1)
#if (CONFIG_LWIP_DHCP_GET_NTP_SRV == 1)
        esp_netif_sntp_start();
#else
        WiFiStation::Instance()->syncTime();
#endif
#endif
        if (WiFiStation::Instance()->mConnectCallback != nullptr)
            WiFiStation::Instance()->mConnectCallback(&event->ip_info.ip.addr);
    }
}

#ifdef CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS
void WiFiStation::event_ota_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "OTA started");
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "Connected to server");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "Reading Image Description");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_REVISION:
                ESP_LOGI(TAG, "Verifying chip revision of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                ESP_LOGI(TAG, "Callback to decrypt function");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                ESP_LOGW(TAG, "Writing to flash: %d written", *(int *)event_data);
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                ESP_LOGI(TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
                break;
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "OTA finish");
                break;
            case ESP_HTTPS_OTA_ABORT:
                ESP_LOGI(TAG, "OTA abort");
                break;
        }
    }
}

bool WiFiStation::startOta(onOtaProgress *otaProgressCallback, char* file)
{
    mOtaProgressCallback = otaProgressCallback;
    esp_http_client_config_t cfgHTTPS;
    memset(&cfgHTTPS, 0, sizeof(cfgHTTPS));
    // cfgHTTPS.addr_type = HTTP_ADDR_TYPE_INET;
    cfgHTTPS.url = file;
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
        .buffer_caps = MALLOC_CAP_SPIRAM,
        .ota_resumption = false,
        .ota_image_bytes_written = 64*1024
    };

    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &event_ota_handler, NULL));
    esp_err_t err = esp_https_ota(&ota_config);
    THEX("startOta",(uint32_t)err);
    return err == ESP_OK;
}
#endif

bool WiFiStation::start(onWiFiConnect *connectCallback, char* ssid, char* password)
{
    if (mConnecting)
        return false;
    mConnectCallback = connectCallback;

    if(ssid != nullptr)
        std::strncpy((char *)m_wifi_config.sta.ssid, ssid, sizeof(m_wifi_config.sta.ssid));
    if(password != nullptr)
        std::strncpy((char *)m_wifi_config.sta.password, password, sizeof(m_wifi_config.sta.password));

    ESP_LOGI(TAG,"%s %s",m_wifi_config.sta.ssid, m_wifi_config.sta.password);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if (CONFIG_WIFICHN_SYNC_TIME == 1) && (CONFIG_LWIP_DHCP_GET_NTP_SRV == 1)
    if (!CDateTimeSystem::isSync())
    {
        mStartSyncTime = true;
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        config.start = false;                     // start SNTP service explicitly (after connecting)
        config.server_from_dhcp = true;           // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
        config.renew_servers_after_new_IP = true; // let esp-netif update configured SNTP server(s) after receiving DHCP lease
        config.index_of_first_server = 1;         // updates from server num 1, leaving server 0 (from DHCP) intact
        config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
        config.sync_cb = time_sync_notification_cb; // only if we need the notification function
        esp_netif_sntp_init(&config);
    }
#endif
    m_net_if = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        nullptr,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        nullptr,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &m_wifi_config));
    mConnecting = true;
    ESP_ERROR_CHECK(esp_wifi_start());
    // ESP_LOGI(TAG, "wifi_init_sta finished.");
    return true;
}

bool WiFiStation::stop()
{
#if CONFIG_WIFICHN_TCP || CONFIG_WIFICHN_UDP
    stopClient();
#endif
    mConnecting = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_stop();
    while (mSrcIP != 0)
        vTaskDelay(1);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler);
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(m_net_if);
    esp_event_loop_delete_default();
    esp_netif_deinit();
    return true;
}

uint16_t WiFiStation::initFromFile(const char *fileName)
{
	try
	{
        std::string str = "/spiffs/";
        str += fileName;
		std::ifstream f(str);
		json data = json::parse(f, nullptr, true, true);
		f.close();
        return initFromJson(data);
	}
	catch (...)
	{
		ESP_LOGW(TAG, "Failed to open file %s or parse", fileName);
        return 0xff;
	}
}

uint16_t WiFiStation::initFromJson(json& config)
{
    uint16_t res = 0;
   	if (config.contains("ssid") && config["ssid"].is_string())
	{
        std::string str = config["ssid"].template get<std::string>();
        std::strncpy((char *)m_wifi_config.sta.ssid, str.c_str(), sizeof(m_wifi_config.sta.ssid));
    }
    else
    {
        res |= 0x01; 
    }
   	if (config.contains("password") && config["password"].is_string())
	{
        std::string str = config["password"].template get<std::string>();
        std::strncpy((char *)m_wifi_config.sta.password, str.c_str(), sizeof(m_wifi_config.sta.password));
    }
    else
    {
        res |= 0x02; 
    }
   	if (config.contains("client") && config["client"].is_object())
    {
   	    if (config["client"].contains("type") && config["client"]["type"].is_string())
        {
            std::string str = config["client"]["type"].template get<std::string>();
#if CONFIG_WIFICHN_TCP || CONFIG_WIFICHN_UDP
            if (str == "udp")
 #if CONFIG_WIFICHN_UDP
                mClient = CLIENT_TYPE::UDP;
#else
                mClient = CLIENT_TYPE::None;
#endif
            else if (str == "tcp")
#if CONFIG_WIFICHN_TCP
                mClient = CLIENT_TYPE::TCP;
#else
                mClient = CLIENT_TYPE::None;
#endif
            else
            {
                mClient = CLIENT_TYPE::None;
                res |= 0x04; 
                return res;
            }

            if(mClient != CLIENT_TYPE::None)
            {
                if (config["client"].contains("host") && config["client"]["host"].is_string())
                {
                    str = config["client"]["host"].template get<std::string>();
                    in_addr_t a = inet_addr(str.c_str());
                    if (a != INADDR_NONE)
                    {
                        mDestIP = a;
                        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR((esp_ip4_addr_t *)&mDestIP));
                    }
                    else
                    {
                        ESP_LOGE(TAG, "wrong host %s", str.c_str());
                        res |= 0x80;
                        return res;
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "client.host not found");
                        res |= 0x80;
                        return res;
                }

                if (config["client"].contains("port") && config["client"]["port"].is_number_unsigned())
                {
                    mPort = config["client"]["port"].template get<uint16_t>();
                }
                else
                {
                    mPort = 2000;
                }
            }
#endif
        }
    }
    return res;
}

#if CONFIG_WIFICHN_TCP || CONFIG_WIFICHN_UDP
bool WiFiStation::startClient(onClientDataRx *clientDataRxCallback)
{
    if (mDestIP == 0xffffffffL)
        return false;

    mClientDataRxCallback = clientDataRxCallback;
#if CONFIG_WIFICHN_UDP
    if (mClient == CLIENT_TYPE::UDP)
    {
        if (mUdpOut != nullptr)
            return false;
        if (mUdpIn != nullptr)
            return false;
#if CONFIG_WIFICHN_TCP
        if (mTcpClient != nullptr)
        {
            delete mTcpClient;
            mTcpClient = nullptr;
        }
#endif

        mUdpOut = new CUDPOut(this, mDestIP, mPort);
        mUdpIn = new CUDPInTask(this, mPort);
    }
#endif
#if CONFIG_WIFICHN_TCP
   if(mClient == CLIENT_TYPE::TCP)
    {
        if (mTcpClient != nullptr)
           return false;
#if CONFIG_WIFICHN_UDP
        if (mUdpOut != nullptr)
        {
            delete mUdpOut;
            mUdpOut = nullptr;
        }
        if (mUdpIn != nullptr)
        {
            delete mUdpIn;
            mUdpIn = nullptr;
        }
#endif

        mTcpClient = new CTCPClientTask(this, mDestIP, mPort);
    }
#endif

    return true;
}

void WiFiStation::sendData(uint8_t *data, uint16_t len)
{
#if CONFIG_WIFICHN_UDP
    if ((mClient == CLIENT_TYPE::UDP) && (mUdpOut != nullptr))
    {
        mUdpOut->sendData(data, len);
    }
#endif
#if CONFIG_WIFICHN_TCP
    if((mClient == CLIENT_TYPE::TCP) && (mTcpClient != nullptr))
    {
        mTcpClient->sendData(data, len);
    }
#endif
}

void WiFiStation::stopClient()
{
    // LOG("stopClient");
#if CONFIG_WIFICHN_UDP
    if (mUdpIn != nullptr)
    {
        delete mUdpIn;
        mUdpIn = nullptr;
    }
    if (mUdpOut != nullptr)
    {
        delete mUdpOut;
        mUdpOut = nullptr;
    }
#endif
#if CONFIG_WIFICHN_TCP
    if(mTcpClient != nullptr)
    {
        delete mTcpClient;
        mTcpClient = nullptr;
    }
#endif
}
#endif

#if (CONFIG_WIFICHN_SYNC_TIME == 1)
void WiFiStation::time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    if (!CDateTimeSystem::isSync())
        CDateTimeSystem::saveDateTime();
    esp_netif_sntp_deinit();
    WiFiStation::Instance()->mStartSyncTime = false;
}

void WiFiStation::syncTime()
{
    // ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = time_sync_notification_cb; // only if we need the notification function
    esp_netif_sntp_init(&config);

    esp_netif_sntp_start();
    mStartSyncTime = true;
}
#endif