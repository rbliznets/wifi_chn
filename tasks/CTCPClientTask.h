/*!
	\file
	\brief Класс задачи для TCP клиента.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 16.09.2024
*/

#pragma once

#include "sdkconfig.h"
#include "WiFiStation.h"

#include "CBaseTask.h"
#include "task_settings.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

/// Класс задачи для TCP клиента.
class CTCPClientTask : public CBaseTask
{
private:
protected:
	WiFiStation *mParent;	 ///< Родительский объект
	sockaddr_in src_addr;	 ///< Адрес сервера
	int m_sock;				 ///< Сокет
	uint8_t rx_buffer[2048]; ///< Буфер принимаемых данных
	int32_t mConnected = 0;	 ///< Флаг установки соединения

	/// Функция задачи.
	virtual void run() override;

public:
	/// Конструктор.
	/*
	 * \param[in] parent - Указатель на родительский объект.
	 * \param[in] addr - IP адрес сервера.
	 * \param[in] port - Порт сервера.
	 */
	CTCPClientTask(WiFiStation *parent, uint32_t &addr, uint16_t &port);
	/// Деструктор.
	virtual ~CTCPClientTask();

	/// Посылка данных
	/*
	 * \param[in] data - Указатель на данные.
	 * \param[in] len - Длина данных.
	 * \return true - если успешно.
	 */
	bool sendData(uint8_t *data, uint16_t len);
};
