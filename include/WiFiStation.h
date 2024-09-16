/*!
	\file
	\brief Класс для подключения к WiFi роутеру.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 10.09.2024
*/

#pragma once

#include "stdint.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include <cstring>
#include "CJsonParser.h"

/// Обработчик события подключения к WiFi.
/*
 *  \param[in] ip_addr - Указатель на IP адрес после подключения, если nullptr - подключение завершено.
 */
typedef void onWiFiConnect(uint32_t *ip_addr);
/// Функция события приема данных.
/*!
 * \param[in] addr IP адрес.
 * \param[in] port Порт.
 * \param[in] data данные.
 * \param[in] size размер данных.
 */
typedef void onClientDataRx(uint32_t addr, uint16_t port, uint8_t *data, uint16_t size);

/// @brief Тип клиента
enum class CLIENT_TYPE
{
	UDP, ///< UDP клиент.
	TCP	 ///< TCP клиент.
};

class CUDPOut;
class CUDPInTask;
class CTCPClientTask;

class WiFiStation
{
	friend class CUDPInTask;
	friend class CTCPClientTask;

protected:
	static WiFiStation *theSingleInstance; ///< Указатель на единственный экземпляр
#if (CONFIG_WIFICHN_SYNC_TIME == 1)
	bool mStartSyncTime = false; ///< Флаг запуска синхронизации времени

	/// Callback функция для синхронизации времени.
	/*
	 *  \param[in] tv - Указатель на структуру timeval (текущее время).
	 */
	static void time_sync_notification_cb(struct timeval *tv);
	/// Запуск синхронизации времени не из DHCP
	void syncTime();
#endif

	wifi_config_t m_wifi_config; ///< Структура WiFi конфигурации.

	/// Callback функция для обработки событий WiFi.
	static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
	onWiFiConnect *mConnectCallback = nullptr; ///< Событие на подсоединение/отсоединения к WiFi.
	esp_netif_t *m_net_if;					   ///< esp_netif_object server

	CLIENT_TYPE mClient = CLIENT_TYPE::UDP; ///< Тип клиента
	uint32_t mDestIP = 0xffffffffL;			///< IP адрес сервера.
	uint32_t mSrcIP = 0;					///< IP адрес устройства.
	uint16_t mPort = 2013;					///< Порт для подключения к серверу
	onClientDataRx *mClientDataRxCallback = nullptr; ///< Событие на прием данных от сервера.

	CUDPOut *mUdpOut = nullptr;			  ///< Указатель на UDP клиент.
	CUDPInTask *mUdpIn = nullptr;		  ///< Указатель на UDP сервер.
	CTCPClientTask *mTcpClient = nullptr; ///< Указатель на TCP клиент.

	/// Конструктор.
	WiFiStation();
	/// Деструктор.
	~WiFiStation();

public:
	/// Единственный экземпляр класса.
	/*!
	  \return Указатель на WiFiStation
	*/
	static WiFiStation *Instance()
	{
		if (theSingleInstance == nullptr)
			theSingleInstance = new WiFiStation();
		return theSingleInstance;
	};
	/// Освобождение ресурсов.
	static void free()
	{
		if (theSingleInstance != nullptr)
		{
			delete theSingleInstance;
			theSingleInstance = nullptr;
		}
	};

	/// Подключение к WiFi.
	/*
	* \param[in] connectCallback - Указатель на функцию обработки события подключения к WiFi.
	* \return true - если подключение успешно, false - если не удалось подключиться.
	*/
	bool start(onWiFiConnect *connectCallback);
	/// Отключение от WiFi.
	/*
	* \return true - если отключение успешно, false - если не удалось отключиться.
	*/
	bool stop();

	/// Настройки WiFi из файла.
	/*
	* \param[in] fileName - имя файла.
	* \param[in] parser - указатель на парсер.
	*/
	void initFromFile(const char *fileName, CJsonParser *parser);
	/// Настройки WiFi из json.
	/*
	* \param[in] index - индекс token
	* \param[in] parser - указатель на парсер.
	*/
	void initFromJson(int index, CJsonParser *parser);

	/// Запуск клиента
	/*
	* \param[in] clientDataRxCallback - Указатель на функцию обработки события приема данных от сервера.
	* \return true - если подключение успешно, false - если не удалось подключиться.
	*/
	bool startClient(onClientDataRx *clientDataRxCallback);
	/// Посылка данных
	/*
	* \param[in] data - Указатель на данные.
	* \param[in] len - Длина данных.
	* \return true - если успешно.
	*/
	void sendData(uint8_t *data, uint16_t len);
	/// Остановка клиента
	void stopClient();
};
