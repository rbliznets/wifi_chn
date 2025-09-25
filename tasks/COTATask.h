/*!
	\file
	\brief Класс задачи для HTTPS OTA.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 24.09.2025
*/

#pragma once

#include "sdkconfig.h"
#include "WiFiStation.h"

#include "CBaseTask.h"
#include "task_settings.h"
#include <string>

class COTATask : public CBaseTask
{
private:
	static void event_ota_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

protected:
	WiFiStation *mParent; ///< Родительский объект
	const std::string mPath;
	int mImageSize = 0;
	uint16_t mProgress = 0xffff;

	/// Функция задачи.
	virtual void run() override;

public:
	COTATask(WiFiStation *parent, const char *file);
	/// Деструктор.
	virtual ~COTATask();

	bool mCancel = false;
};
