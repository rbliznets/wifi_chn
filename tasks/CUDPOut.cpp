/*!
	\file
	\brief Класс задачи отправки UDP пакетов.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 11.09.2024
*/

#include "CUDPOut.h"
#include "CTrace.h"
#include <cstring>

static const char *TAG = "udpout";

CUDPOut::CUDPOut(WiFiStation *parent, uint32_t &addr, uint16_t &port) : mParent(parent)
{
	std::memset(&mDest, 0, sizeof(mDest));
	mDest.sin_family = AF_INET;
	mDest.sin_addr.s_addr = addr;
	mDest.sin_port = htons(port);

	m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (m_sock < 0)
	{
		ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
	}
	else
	{
		// Set timeout
		timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	}
}

CUDPOut::~CUDPOut()
{
	if (m_sock >= 0)
	{
		shutdown(m_sock, 0);
		close(m_sock);
	}
}

bool CUDPOut::sendData(uint8_t *data, uint16_t len)
{
	if (sendto(m_sock, data, len, 0, (struct sockaddr *)&mDest, sizeof(mDest)) < 0)
	{
		ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
		return false;
	}
	else
		return true;
}