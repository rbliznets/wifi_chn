/*!
	\file
	\brief Класс задачи для приема UDP пакетов.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 11.09.2024
*/

#include "CUDPInTask.h"
#include "CTrace.h"
#include <cstring>

static const char *TAG = "udpin";

CUDPInTask::CUDPInTask(WiFiStation *parent, uint16_t &port) : CBaseTask(), mParent(parent)
{
	std::memset(&src_addr, 0, sizeof(src_addr));
	src_addr.sin_family = AF_INET;
	src_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	src_addr.sin_port = htons(port);

	m_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (m_sock < 0)
	{
		ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
		return;
	}
	//ESP_LOGI(TAG, "Socket created");

	CBaseTask::init(UDPINTASK_NAME, UDPINTASK_STACKSIZE, UDPINTASK_PRIOR, UDPINTASK_LENGTH, UDPINTASK_CPU);
}

CUDPInTask::~CUDPInTask()
{
	if (m_sock >= 0)
	{
		// shutdown(m_sock, 0);
		close(m_sock);
		do
		{
			vTaskDelay(1);
		} while (mTaskQueue != nullptr);
		//ESP_LOGI(TAG, "Socket delete");
	}
}

void CUDPInTask::run()
{
#ifndef CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE
	UBaseType_t m1 = uxTaskGetStackHighWaterMark2(nullptr);
#endif
	if (m_sock >= 0)
	{
		// Set timeout
		struct timeval timeout;
		timeout.tv_sec = 10;
		timeout.tv_usec = 0;
		setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

		int err = bind(m_sock, (struct sockaddr *)&src_addr, sizeof(src_addr));
		if (err < 0)
		{
			ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
		}
		else
		{
			sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
			socklen_t socklen = sizeof(source_addr);
			ESP_LOGI(TAG, "start");
			for (;;)
			{
				int len = recvfrom(m_sock, rx_buffer, sizeof(rx_buffer)-1, 0, (struct sockaddr *)&source_addr, &socklen);
				if (len < 0)
				{
					// TDEC("errno",errno);
					if(errno == ENOTCONN)
					{
						break;
					}
				}
				else
				{
					if (mParent->mClientDataRxCallback != nullptr)
						mParent->mClientDataRxCallback(((sockaddr_in *)&source_addr)->sin_addr.s_addr, src_addr.sin_port, rx_buffer, len);
					else
						TRACEDATA("rx", rx_buffer, len);
				}
#ifndef CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE
				UBaseType_t m2 = uxTaskGetStackHighWaterMark2(nullptr);
				if (m2 != m1)
				{
					m1 = m2;
					TDEC("free UDPIn stack", m2);
				}
#endif
			}
		}

		// TDEC("m_sock",m_sock);
	}
}
