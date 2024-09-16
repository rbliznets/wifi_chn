/*!
	\file
	\brief Класс задачи отправки UDP пакетов.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 11.09.2024
*/

#pragma once

#include "sdkconfig.h"
#include "WiFiStation.h"

#include "task_settings.h"

#include "lwip/sockets.h"

/// Класс задачи отправки UDP пакетов.
class CUDPOut
{
private:
protected:
	WiFiStation *mParent; ///< Родительский объект
	sockaddr_in mDest;	  ///< Адрес сервера
	int m_sock;			  ///< Сокет на отправку данных
public:
	/// Конструктор.
	/*
	* \param[in] parent - Указатель на родительский объект.
	* \param[in] addr - IP адрес сервера.
	* \param[in] port - Порт сервера.
	*/
	CUDPOut(WiFiStation *parent, uint32_t &addr, uint16_t &port);
	/// Деструктор.
	virtual ~CUDPOut();

	/// Посылка данных
	/*
	 * \param[in] data - Указатель на данные.
	 * \param[in] len - Длина данных.
	 * \return true - если успешно.
	 */
	bool sendData(uint8_t *data, uint16_t len);
};
