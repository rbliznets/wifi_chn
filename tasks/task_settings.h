/*!
	\file
	\brief Настройки параметров.
	\authors Близнец Р.А. (r.bliznets@gmail.com)
	\version 0.0.0.1
	\date 26.11.2023

	Опции условной компиляции, настройки статических буферов и привязки классов устройств к железу.
*/

#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_WIFICHN_TASK0
#define CPU_CORE (0)
#else
#define CPU_CORE (1)
#endif

/// Макрос используется для вычисления количества элементов в массиве. Он принимает массив «x» в качестве входных данных и делит общий размер массива на размер его первого элемента. Это дает количество элементов в массиве.
#define countof(x) (sizeof(x) / sizeof(x[0]))

#ifdef CONFIG_ESP_TASK_WDT
#define TASK_MAX_BLOCK_TIME pdMS_TO_TICKS((CONFIG_ESP_TASK_WDT_TIMEOUT_S - 1) * 1000 + 500)
#else
#define TASK_MAX_BLOCK_TIME portMAX_DELAY
#endif

/*
 * Параметры задач
 */
#define OTATASK_NAME "ota"				   ///< Имя задачи для отладки.
#define OTATASK_STACKSIZE (5 * 1024 - 256) ///< Размер стека задачи.
#define OTATASK_PRIOR (1)				   ///< Приоритет задачи.
#define OTATASK_LENGTH (1)				   ///< Длина приемной очереди задачи.
#define OTATASK_CPU CPU_CORE					   ///< Номер ядра процессора.
#define OTATASK_PSRAM false

#define OTATASK_BEGIN_RETRIES (1)			   ///< Число попыток esp_https_ota_begin() при обрыве связи.
#define OTATASK_RETRY_DELAY_MS (1000)		   ///< Пауза между попытками esp_https_ota_begin().
