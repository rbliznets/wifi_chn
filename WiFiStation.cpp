/*!
    \file
    \brief Класс для подключения к WiFi роутеру.
    \authors Близнец Р.А. (r.bliznets@gmail.com)
    \version 0.0.0.1
    \date 10.09.2024
*/

#include "WiFiStation.h"
#include "esp_log.h"
#include "CTrace.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "CDateTimeSystem.h"
#ifdef CONFIG_WIFICHN_OTA
#include "tasks/COTATask.h"
#endif

static const char *TAG = "wifi";

// // ===== ВРЕМЕННАЯ ОТЛАДКА ПОРЧИ КУЧИ =====
// // По дампам падений слово ~0x3fcc80b0 (бывшая память аудио, переиспользуемая
// // структурами WiFi-сессии) затирается значениями 0x18 / 0xc03403c0 (похоже на
// // слова GDMA-дескриптора). Захватываем этот участок канареечным блоком до
// // инициализации WiFi и ставим на него watchpoint на запись (оба ядра) +
// // периодический опрос. CPU-писатель даст Debug exception с backtrace виновника;
// // если сработает только опрос - пишет DMA.
// #include "esp_cpu.h"
// #include "esp_ipc.h"
// #include "esp_timer.h"
// #include "esp_system.h"

// // Область, в которую попадали дикие записи (по дампам): 0x3fcc7f94, 0x3fcc80b0..c4.
// // Цель писателя плавает вместе с раскладкой, поэтому распыляем канарейки по всей
// // зоне и при попадании перевешиваем watchpoint на адрес попадания: писатель
// // повторяет запись каждую WiFi-сессию, следующая даст Debug exception с его PC.
// static constexpr uintptr_t kDbgZoneLo = 0x3fcc7000;
// static constexpr uintptr_t kDbgZoneHi = 0x3fcca000;
// static constexpr uint8_t kDbgFill = 0xA5;
// static constexpr size_t kDbgBlk = 64;
// static constexpr int kDbgMaxBlk = 200;

// static uint8_t *sDbgBlocks[kDbgMaxBlk];
// static int sDbgBlockCount = 0;
// static esp_timer_handle_t sDbgPollTimer = nullptr;
// static volatile uintptr_t sDbgWpAddr = 0;

// static void dbg_wp_arm(void *)
// {
//     // 32 байта, выравнивание по 32.
//     // Индекс 0 свободен: CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK занимает последний.
//     esp_cpu_set_watchpoint(0, (void *)(sDbgWpAddr & ~(uintptr_t)31), 32, ESP_CPU_WATCHPOINT_STORE);
// }

// // Цель писателя стабильна между загрузками с включённым BT: записи u32=24 по
// // 0x3fcc8114 и u8=8 по 0x3fcc8118 (две загрузки подряд). Ставим watchpoint прямо
// // на неё на время WiFi-сессии: легитимные записи туда - только alloc/free
// // аллокатора (отличимы по backtrace), настоящий писатель попадётся с точным PC.
// static constexpr uintptr_t kDbgHardTarget = 0x3fcc8114;

// static uint8_t *sDbgHitBlock = nullptr; // блок, в который попал stale free/запись
// static volatile bool sDbgReleasing = false;
// static volatile uintptr_t sDbgBaitLo = ~(uintptr_t)0, sDbgBaitHi = 0;

// #include "esp_debug_helpers.h"
// // Hook вызывается в начале КАЖДОГО heap_caps_free - в контексте вызвавшего.
// // free() по адресу удерживаемого нами блока-приманки = заведомо stale free:
// // печатаем backtrace виновника и останавливаемся.
// extern "C" void esp_heap_trace_free_hook(void *ptr)
// {
//     if (sDbgReleasing || sDbgBlockCount == 0)
//         return;
//     uintptr_t p = (uintptr_t)ptr;
//     if (p < sDbgBaitLo || p > sDbgBaitHi)
//         return;
//     for (int i = 0; i < sDbgBlockCount; ++i)
//     {
//         if (sDbgBlocks[i] == (uint8_t *)ptr)
//         {
//             esp_rom_printf("\nSTALE FREE of bait %p! Culprit backtrace:\n", ptr);
//             esp_backtrace_print(20);
//             esp_system_abort("stale free caught");
//         }
//     }
// }

// static void dbg_poll(void *)
// {
//     for (int b = 0; b < sDbgBlockCount; ++b)
//     {
//         uint32_t *w = (uint32_t *)sDbgBlocks[b];
//         if ((uint8_t *)w == sDbgHitBlock)
//             continue; // блок-жертву сторожит watchpoint, опрос не нужен
//         for (size_t i = 0; i < kDbgBlk / 4; ++i)
//         {
//             if (w[i] != 0xA5A5A5A5)
//             {
//                 ESP_LOGE(TAG, "CANARY HIT %p: %08lx %08lx %08lx %08lx", &w[i],
//                          w[i], w[i + 1], w[i + 2], w[i + 3]);
//                 // Блок-жертву держим навсегда и вешаем на него watchpoint:
//                 // виновник повторяет free/запись по этому адресу каждую WiFi-сессию,
//                 // следующее обращение даст Debug exception с его backtrace.
//                 std::memset(w, kDbgFill, kDbgBlk);
//                 sDbgHitBlock = (uint8_t *)w;
//                 sDbgWpAddr = (uintptr_t)&w[i];
//                 esp_ipc_call_blocking(0, dbg_wp_arm, nullptr);
//                 esp_ipc_call_blocking(1, dbg_wp_arm, nullptr);
//                 ESP_LOGE(TAG, "wp armed @%p (block %p held)", (void *)(sDbgWpAddr & ~(uintptr_t)31), w);
//                 return;
//             }
//         }
//     }
// }

// static void dbg_canary_arm()
// {
//     // Виновник найден (гонка CSoftwareTimer, см. CSoftwareTimer.cpp) - приманки
//     // отключены: они фрагментировали кучу и мешали пересозданию аудио (27КБ стек).
//     return;
//     if (sDbgBlockCount != 0)
//         return;
//     // Распыляем блоки по зоне: держим те, что попали в [kDbgZoneLo,kDbgZoneHi).
//     // Аллокации TLSF не упорядочены по адресам, поэтому не прерываемся рано.
//     static void *drop[600];
//     int nd = 0;
//     while (sDbgBlockCount < kDbgMaxBlk && nd < 600)
//     {
//         uint8_t *p = (uint8_t *)heap_caps_malloc(kDbgBlk, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
//         if (p == nullptr)
//             break;
//         // Берём любые блоки: TLSF отдаёт свежеосвобождённую (аудио) память первой -
//         // именно там живут цели stale-указателей.
//         std::memset(p, kDbgFill, kDbgBlk);
//         sDbgBlocks[sDbgBlockCount++] = p;
//         if ((uintptr_t)p < sDbgBaitLo)
//             sDbgBaitLo = (uintptr_t)p;
//         if ((uintptr_t)p > sDbgBaitHi)
//             sDbgBaitHi = (uintptr_t)p;
//         (void)drop;
//         (void)nd;
//     }
//     for (int i = 0; i < nd; ++i)
//         heap_caps_free(drop[i]);
//     if (sDbgBlockCount > 0)
//     {
//         if (sDbgPollTimer == nullptr)
//         {
//             const esp_timer_create_args_t cfg = {
//                 .callback = dbg_poll, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK, .name = "wifidbg", .skip_unhandled_events = true};
//             esp_timer_create(&cfg, &sDbgPollTimer);
//             esp_timer_start_periodic(sDbgPollTimer, 10000); // 10 мс
//         }
//         ESP_LOGW(TAG, "dbg spray: %d blk, %p..%p", sDbgBlockCount,
//                  sDbgBlocks[0], sDbgBlocks[sDbgBlockCount - 1] + kDbgBlk);
//     }
//     else
//     {
//         ESP_LOGW(TAG, "dbg spray: zone busy, not armed");
//     }
// }

// static void dbg_wp_clear(void *)
// {
//     esp_cpu_clear_watchpoint(0);
// }

// // Освобождаем спрей в конце WiFi-сессии, чтобы не спровоцировать нехватку
// // внутренней памяти при пересоздании аудио (стек голосовой задачи 27КБ).
// // Блок-жертва (sDbgHitBlock) не освобождается: его сторожит watchpoint,
// // и виновник придёт по этому адресу снова в следующей сессии.
// static void dbg_canary_release()
// {
//     if (sDbgBlockCount == 0)
//         return;
//     if (sDbgPollTimer != nullptr)
//         esp_timer_stop(sDbgPollTimer); // остановить опрос до освобождения (иначе гонка с re-arm)
//     sDbgReleasing = true;
//     for (int i = 0; i < sDbgBlockCount; ++i)
//         if (sDbgBlocks[i] != sDbgHitBlock)
//             heap_caps_free(sDbgBlocks[i]);
//     sDbgBlockCount = 0;
//     sDbgBaitLo = ~(uintptr_t)0;
//     sDbgBaitHi = 0;
//     sDbgReleasing = false;
//     ESP_LOGW(TAG, "dbg spray released%s", (sDbgHitBlock != nullptr) ? " (hit block held, wp armed)" : "");
// }
// // ===== КОНЕЦ ВРЕМЕННОЙ ОТЛАДКИ =====

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
        // Не вызываем esp_wifi_connect(), если идёт остановка
        if (!WiFiStation::Instance()->mStopping)
            esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        uint16_t ap_count = 0;
        wifi_ap_record_t *ap_list = nullptr;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count)); // Get number of APs found

        if (ap_count > 0)
        {
#ifdef CONFIG_SPIRAM
            ap_list = (wifi_ap_record_t *)heap_caps_malloc(sizeof(wifi_ap_record_t) * ap_count, MALLOC_CAP_SPIRAM);
#else
            ap_list = (wifi_ap_record_t *)heap_caps_malloc(sizeof(wifi_ap_record_t) * ap_count, MALLOC_CAP_DEFAULT);
#endif                                                                         // CONFIG_SPIRAM
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list)); // Get AP records
        }
        if (WiFiStation::Instance()->mWiFiScanCallback != nullptr)
            WiFiStation::Instance()->mWiFiScanCallback(ap_list, ap_count);
        if (ap_list != nullptr)
            heap_caps_free(ap_list);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (WiFiStation::Instance()->mSrcIP != 0)
        {
            WiFiStation::Instance()->mSrcIP = 0;
            if (WiFiStation::Instance()->mConnectCallback != nullptr)
                WiFiStation::Instance()->mConnectCallback(nullptr);
        }
        else if (WiFiStation::Instance()->mConnecting && !WiFiStation::Instance()->mStopping)
        {
            esp_wifi_connect();
            if (WiFiStation::Instance()->mEventCallback != nullptr)
                WiFiStation::Instance()->mEventCallback(event_id, "connecting was failed");
            else
                ESP_LOGW(TAG, "connecting was failed");
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

#ifdef CONFIG_WIFICHN_OTA
bool WiFiStation::startOta(onOtaProgress *otaProgressCallback, const char *file, onOtaImageDesc *otaImageDesc)
{
    if (mOTA == nullptr)
    {
        mOtaProgressCallback = otaProgressCallback;
        mOtaImageDesc = otaImageDesc;
        mOTA = new COTATask(this, file);
        return true;
    }
    return false;
}

bool WiFiStation::stopOta()
{
    if (mOTA != nullptr)
    {
        delete mOTA;
        mOTA = nullptr;
        return true;
    }
    else if (mConnecting)
    {
        mSrcIP = 1;
    }
    return false;
}
#endif

bool WiFiStation::start(onWiFiConnect *connectCallback, onWiFiEvent *eventCallback, const char *ssid, const char *password)
{
    // ВРЕМЕННАЯ ОТЛАДКА: в STA/OTA-фазе приманки не взводим (порча происходит в фазе
    // скана, а во время TLS-хендшейка каждый килобайт внутренней кучи на счету).
    mConnectCallback = connectCallback;
    mEventCallback = eventCallback;
    if (ssid != nullptr)
        strlcpy((char *)m_wifi_config.sta.ssid, ssid, sizeof(m_wifi_config.sta.ssid));
        // std::strncpy((char *)m_wifi_config.sta.ssid, ssid, sizeof(m_wifi_config.sta.ssid));
    if (password != nullptr)
        strlcpy((char *)m_wifi_config.sta.password, password, sizeof(m_wifi_config.sta.password));
        // std::strncpy((char *)m_wifi_config.sta.password, password, sizeof(m_wifi_config.sta.password));

    ESP_LOGI(TAG, "%s %s", m_wifi_config.sta.ssid, m_wifi_config.sta.password);

    // netif и default event loop создаются один раз при старте приложения (main.cpp)
    // и не удаляются: повторные вызовы вернут ESP_ERR_INVALID_STATE - это норма.
    esp_netif_init();
    esp_event_loop_create_default();
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
    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        nullptr,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        nullptr,
                                                        nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &m_wifi_config));
    mConnecting = true;
    ESP_ERROR_CHECK(esp_wifi_start());
    // ESP_LOGI(TAG, "wifi_init_sta finished.");
    return true;
}

bool WiFiStation::startScan(onWiFiScan *scanCallback)
{
    // dbg_canary_arm(); // ВРЕМЕННАЯ ОТЛАДКА: захватить область порчи до инициализации WiFi
    mWiFiScanCallback = scanCallback;

    esp_event_loop_create_default(); 
    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_SCAN_DONE,
                                                        &event_handler,
                                                        nullptr,
                                                        nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_scan_config_t scan_config = {
        .ssid = NULL,        // Scan for all SSIDs
        .bssid = NULL,       // Scan for all BSSIDs
        .channel = 0,        // Scan all channels (0)
        .show_hidden = false // Show hidden SSIDs
    };

    ESP_LOGI(TAG, "Starting Wi-Fi scan...");
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
    return true;
}

bool WiFiStation::stop()
{
    if (mConnecting)
    {
        mConnecting = false;
        mStopping = true; // Блокируем event_handler от вызова esp_wifi_connect()

        // Принудительно разрываем соединение, чтобы остановить внутренний retry ESP-IDF.
        // Повторный esp_wifi_connect() из event_handler уже заблокирован флагом mStopping,
        // поэтому обработчики можно не отключать заранее — иначе некому будет сбросить mSrcIP
        // по событию WIFI_EVENT_STA_DISCONNECTED и цикл ожидания ниже зависнет навсегда.
        esp_wifi_disconnect();

        // Сбрасываем внутреннее состояние reconnect в ESP-IDF
        esp_wifi_restore();

        // Безопасно останавливаем WiFi
        esp_wifi_stop();

        while (mSrcIP != 0)
            vTaskDelay(1);

        // Отключаем обработчики после того, как соединение гарантированно разорвано
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler);

        // default event loop и esp_netif живут весь аптайм (созданы в main.cpp):
        // их удаление здесь гонялось с отложенными esp_event_post из WiFi-драйвера
        // (ppTask/таймеры ядра 1) и оставляло висячие регистрации.

#if (CONFIG_WIFICHN_SYNC_TIME == 1)
        // Если синхронизация времени не успела завершиться (time_sync_notification_cb
        // не вызывался), esp_netif_sntp остаётся инициализированным. Без этого деинита
        // повторный WiFiStation::start() в той же сессии получит от
        // esp_netif_sntp_init() ESP_ERR_INVALID_STATE (см. esp_netif_sntp.c) - ошибку,
        // которая нигде не проверяется, поэтому новая синхронизация молча не запустится.
        if (mStartSyncTime)
        {
            esp_netif_sntp_deinit();
            mStartSyncTime = false;
        }
#endif
        esp_wifi_deinit();
        esp_netif_destroy_default_wifi(m_net_if);

        mStopping = false;
    }
    else
    {
        esp_wifi_stop();
        esp_wifi_deinit();
        esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &event_handler);
        // default event loop не удаляем (см. комментарий выше)
    }
    esp_event_loop_delete_default();
      // dbg_canary_release(); // ВРЕМЕННАЯ ОТЛАДКА: вернуть память до пересоздания аудио
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

uint16_t WiFiStation::initFromJson(json &config)
{
    uint16_t res = 0;
    if (config.contains("ssid") && config["ssid"].is_string())
    {
        std::string str = config["ssid"].template get<std::string>();
        strlcpy((char *)m_wifi_config.sta.ssid, str.c_str(), sizeof(m_wifi_config.sta.ssid));
    }
    else
    {
        res |= 0x01;
    }
    if (config.contains("password") && config["password"].is_string())
    {
        std::string str = config["password"].template get<std::string>();
        strlcpy((char *)m_wifi_config.sta.password, str.c_str(), sizeof(m_wifi_config.sta.password));
    }
    else
    {
        res |= 0x02;
    }
    return res;
}

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