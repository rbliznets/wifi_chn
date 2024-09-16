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
#include "tasks/CUDPOut.h"
#include "tasks/CUDPInTask.h"
#include "tasks/CTCPClientTask.h"
#include "CTrace.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "CDateTimeSystem.h"

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
        else
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

bool WiFiStation::start(onWiFiConnect *connectCallback)
{
    if (mSrcIP != 0)
        return false;
    mConnectCallback = connectCallback;

    // ESP_LOGI(TAG,"%s %s",m_wifi_config.sta.ssid, m_wifi_config.sta.password);

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
    ESP_ERROR_CHECK(esp_wifi_start());
    // ESP_LOGI(TAG, "wifi_init_sta finished.");
    return true;
}

bool WiFiStation::stop()
{
    stopClient();
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

void WiFiStation::initFromFile(const char *fileName, CJsonParser *parser)
{
    if (fileName != nullptr)
    {
        std::string str = "/spiffs/";
        str += fileName;
        FILE *f = std::fopen(str.c_str(), "r");
        if (f == nullptr)
        {
            ESP_LOGW(TAG, "Failed to open file %s", fileName);
        }
        else
        {
            std::fseek(f, 0, SEEK_END);
            int32_t sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            uint8_t *data = new uint8_t[sz + 1];
            sz = std::fread(data, 1, sz, f);
            std::fclose(f);
            data[sz] = 0;
            int t1 = parser->parse((const char *)data);
            if (t1 == 1)
            {
                initFromJson(t1, parser);
            }
            delete[] data;
        }
    }
}

void WiFiStation::initFromJson(int index, CJsonParser *parser)
{
    std::string str;
    if (parser->getString(index, "ssid", str))
    {
        std::strncpy((char *)m_wifi_config.sta.ssid, str.c_str(), sizeof(m_wifi_config.sta.ssid));
    }
    if (parser->getString(index, "password", str))
    {
        std::strncpy((char *)m_wifi_config.sta.password, str.c_str(), sizeof(m_wifi_config.sta.password));
    }
    int t;
    if (parser->getObject(index, "client", t))
    {
        int x;
        if (parser->getString(t, "host", str))
        {
            in_addr_t a = inet_addr(str.c_str());
            if (a != INADDR_NONE)
            {
                mDestIP = a;
                ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR((esp_ip4_addr_t *)&mDestIP));
            }
            else
            {
                ESP_LOGE(TAG, "wrong host %s", str.c_str());
                return;
            }
        }
        else
        {
            ESP_LOGE(TAG, "client.host not found");
            return;
        }

        if (parser->getInt(t, "port", x))
        {
            mPort = x;
        }
        if (parser->getString(t, "type", str))
        {
            if (str == "udp")
                mClient = CLIENT_TYPE::UDP;
            else
                mClient = CLIENT_TYPE::TCP;
        }
    }
}

bool WiFiStation::startClient(onClientDataRx *clientDataRxCallback)
{
    if (mDestIP == 0xffffffffL)
        return false;

    mClientDataRxCallback = clientDataRxCallback;
    if (mClient == CLIENT_TYPE::UDP)
    {
        if (mUdpOut != nullptr)
            return false;
        if (mUdpIn != nullptr)
            return false;
        if (mTcpClient != nullptr)
        {
            delete mTcpClient;
            mTcpClient = nullptr;
        }

        mUdpOut = new CUDPOut(this, mDestIP, mPort);
        mUdpIn = new CUDPInTask(this, mPort);
    }
    else if(mClient == CLIENT_TYPE::TCP)
    {
        if (mTcpClient != nullptr)
           return false;
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

        mTcpClient = new CTCPClientTask(this, mDestIP, mPort);
    }

    return true;
}

void WiFiStation::sendData(uint8_t *data, uint16_t len)
{
    if ((mClient == CLIENT_TYPE::UDP) && (mUdpOut != nullptr))
    {
        mUdpOut->sendData(data, len);
    }
    else if((mClient == CLIENT_TYPE::TCP) && (mTcpClient != nullptr))
    {
        mTcpClient->sendData(data, len);
    }
}

void WiFiStation::stopClient()
{
    // LOG("stopClient");
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
    if(mTcpClient != nullptr)
    {
        delete mTcpClient;
        mTcpClient = nullptr;
    }
}

#if (CONFIG_WIFICHN_SYNC_TIME == 1)
void WiFiStation::time_sync_notification_cb(struct timeval *tv)
{
    // ESP_LOGI(TAG, "Notification of a time synchronization event");
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