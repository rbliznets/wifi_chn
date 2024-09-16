/*!
	\file
	\brief Класс задачи для TCP клиента.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 16.09.2024
*/

#include "CTCPClientTask.h"
#include "CTrace.h"
#include <cstring>

static const char *TAG = "tcp";

CTCPClientTask::CTCPClientTask(WiFiStation *parent, uint32_t &addr, uint16_t &port) : CBaseTask(), mParent(parent)
{
	std::memset(&src_addr, 0, sizeof(src_addr));
	src_addr.sin_family = AF_INET;
	src_addr.sin_port = htons(port);
	src_addr.sin_addr.s_addr = addr;

	CBaseTask::init(TCPCLIENTTASK_NAME, TCPCLIENTTASK_STACKSIZE, TCPCLIENTTASK_PRIOR, TCPCLIENTTASK_LENGTH, TCPCLIENTTASK_CPU);
}

CTCPClientTask::~CTCPClientTask()
{
	if (m_sock >= 0)
	{
		mConnected = -1;
		shutdown(m_sock, 0);
		close(m_sock);
		do
		{
			vTaskDelay(1);
		} while (mTaskQueue != nullptr);
		// ESP_LOGI(TAG, "Socket delete");
		vTaskDelay(1);
	}
}

void CTCPClientTask::run()
{
#ifndef CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE
	UBaseType_t m1 = uxTaskGetStackHighWaterMark2(nullptr);
#endif
	for (;;)
	{
		m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
		if (m_sock < 0)
		{
			ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
			return;
		}
		int err = connect(m_sock, (struct sockaddr *)&src_addr, sizeof(src_addr));
		if (err != 0)
		{
			ESP_LOGW(TAG, "Socket unable to connect: errno %d", errno);
			if (mConnected == -1)
				break;
			else
			{
				close(m_sock);
				continue;
			}
		}

		mConnected = 1;
		for (;;)
		{
			int len = recv(m_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
			if (len < 0)
			{
				// TDEC("errno",errno);
				if (errno == ENOTCONN)
				{
					if (mConnected == 1)
						mConnected = 0;
					break;
				}
			}
			else
			{
				if (mParent->mClientDataRxCallback != nullptr)
					mParent->mClientDataRxCallback(src_addr.sin_addr.s_addr, src_addr.sin_port, rx_buffer, len);
				else
					TRACEDATA("rx", rx_buffer, len);
			}
#ifndef CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE
			UBaseType_t m2 = uxTaskGetStackHighWaterMark2(nullptr);
			if (m2 != m1)
			{
				m1 = m2;
				TDEC("free tcp stack", m2);
			}
#endif
		}
		if (mConnected == -1)
			break;
	}
}

bool CTCPClientTask::sendData(uint8_t *data, uint16_t len)
{
	if ((m_sock < 0) || (mConnected != 1))
		return false;

	if (send(m_sock, data, len, 0) < 0)
	{
		ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
		return false;
	}
	else
		return true;
}