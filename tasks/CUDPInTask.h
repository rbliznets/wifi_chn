/*!
	\file
	\brief Класс задачи для приема UDP пакетов.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 11.09.2024
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

/// Класс задачи для приема UDP пакетов.
class CUDPInTask : public CBaseTask
{
private:
protected:
	WiFiStation *mParent;	 ///< Родительский объект
	sockaddr_in src_addr;	 ///< Адрес устройства
	int m_sock;				 ///< Сокет на прием данных
	uint8_t rx_buffer[2048]; ///< Буфер принимаемых данных

	/// Функция задачи.
	virtual void run() override;

public:
	/// Конструктор.
	/*
	 * \param parent - родительский объект
	 * \param port - порт
	 */
	CUDPInTask(WiFiStation *parent, uint16_t &port);
	/// Деструктор.
	virtual ~CUDPInTask();
};
