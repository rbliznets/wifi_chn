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

/// Макрос используется для вычисления количества элементов в массиве. Он принимает массив «x» в качестве входных данных и делит общий размер массива на размер его первого элемента. Это дает количество элементов в массиве.
#define countof(x) (sizeof(x) / sizeof(x[0]))

#define MSG_END_TASK (0) ///< Код команды завершения задачи.
#define MSG_CONNECT (10)
#define MSG_DISCONNECT (11)
#define MSG_SEND_DATA (12)

#ifdef CONFIG_ESP_TASK_WDT
#define TASK_MAX_BLOCK_TIME pdMS_TO_TICKS((CONFIG_ESP_TASK_WDT_TIMEOUT_S - 1) * 1000 + 500)
#else
#define TASK_MAX_BLOCK_TIME portMAX_DELAY
#endif

/*
 * Параметры задач
 */
#define UDPINTASK_NAME "udpi"		   ///< Имя задачи для отладки.
#define UDPINTASK_STACKSIZE (2 * 1024) ///< Размер стека задачи.
#define UDPINTASK_PRIOR (2)			   ///< Приоритет задачи.
#define UDPINTASK_LENGTH (1)		   ///< Длина приемной очереди задачи.
#define UDPINTASK_CPU (0)			   ///< Номер ядра процессора.

#define TCPCLIENTTASK_NAME "tcp"		   ///< Имя задачи для отладки.
#define TCPCLIENTTASK_STACKSIZE (2 * 1024) ///< Размер стека задачи.
#define TCPCLIENTTASK_PRIOR (2)			   ///< Приоритет задачи.
#define TCPCLIENTTASK_LENGTH (1)		   ///< Длина приемной очереди задачи.
#define TCPCLIENTTASK_CPU (0)			   ///< Номер ядра процессора.
