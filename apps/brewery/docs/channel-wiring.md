# Wiring задач ↔ каналов (STAM)

Первая версия.
Таблица “кто пишет → в какой канал → кто читает” для v1.

| Пишет (задача/endpoint)               | Канал                 | Читает (задача/endpoint)      |
| `temp_sensor_task/out_temp`           | `temperature_raw`     | `state_aggregator/in_temp`    |
| `level_input_task/out_level`          | `level_raw`           | `state_aggregator/in_level`   |
| `state_aggregator/out_temp_valid`     | `temperature_valid`   | `fsm_task/in_temp`            |
|                                       |                       | `pid_task/in_temp`            |
|                                       |                       | `safety_task/in_temp`         |
|                                       |                       | `ui_task/in_temp`             |
|                                       |                       | `logger_task/in_temp`         |
|                                       |                       | `stm8_link_task/in_temp`      |
| `state_aggregator/out_level_state`    | `level_state`         | `fsm_task/in_level`           |  
|                                       |                       | `safety_task/in_level`        |
| `fsm_task/out_target`                 | `target_temp`         | `pid_task/in_target`          |
| `fsm_task/out_pump`                   | `pump_command`        | `actuator_task/in_pump`       |
| `pid_task/out_power`                  | `heater_power`        | `actuator_task/in_power`      | 
|                                       |                       | `safety_task/in_power`        |
| `safety_task/out_trip`                | `safety_trip`         | `actuator_task/in_trip`       | 
|                                       |                       | `stm8_link_task/in_trip`      | 
|                                       |                       | `ui_task/in_trip`             | 
|                                       |                       | `logger_task/in_trip`         |
| `safety_task/out_reason`              | `safety_reason`       | `ui_task/in_reason`           | 
|                                       |                       | `logger_task/in_reason`       |
| `fsm_task/out_mode`                   | `mode_state`          | `safety_task/in_mode`         | 
|                                       |                       | `ui_task/in_mode`             | 
|                                       |                       | `logger_task/in_mode`         |
| `ui_task/out_ui`                      | `ui_input`            | `fsm_task/in_ui`              |
| `logger_task/out_log`                 | `log_stream`          | `внешний потребитель/storage` |



Вторая версия оля одноконтроллерной системы без параллельного сейфти-контроллера.

RT/HAL-dep — задача RT по архитектуре. 
             Гарантия RT обеспечивается портом HAL.
             Нарушение HAL-контракта переводит задачу в NON_RT.

|Reading chnl           |In_channel  |Task name     |Task domain    |Out_Channel        |Writing        |Data
________________________________________________________________________________________________________________________________________________________________
|RT_TIME buffer         |IN_RTC      |RECEIP_TSK    |RT             |OUT_RCP_DATA       |RCP_DATA SPMC  |TEMP_GOAL, PUMP_FLAG, HEAT_FLAG, BREWING_STAGE, TICK
|EVT_BTNS ring          |IN_BTNS     |-//-//-//     |               |                   |RCP_ALERTS RING|REMOVE_GRAIN / ADD_HOP / BREW_DONE
RECEIP_TSK - управляет стадиями варки в автомате и командами в ручном режиме; выдаёт уставку температуры и сигналы тревог
Задача управления варкой в автоматическом/ручном режиме.  
В автоматическом режиме : 
	Отвечает за стадии, температуры, паузы.
    Выдает в систему нужную температуру. 
	Выдает сигналы "Убрать дробину", "Добавить хмель", "Варка завершена".
	Читает текущее время из буфера задачи РТЦ для выдержки времени пауз.
	Читает сигналы "Пауза", "Стоп".
	Выдает данные для отображения на дисплее (по крайней мере часть данных)
	По команде "Стоп" вызывается SYSTEM_DOWN(USER_STOP).
	По команде "Пауза" температура воды продолжает контролироваться, помпы отключаются, время варки останавлевается. При паузе сбрасывает PUMP_FLAG в 0 Повторное нажатие PAUSE возобновляет работу: восстанавливает PUMP_FLAG, продолжает отсчёт времени варки
Ручном режиме:
	Читает сигналы "Стоп", "Выше температура", "Ниже температура", "Включить помпу/Выключить помпу".
    Выдает в систему нужную температуру
    Выдает в систему включен/выключен нагрев (целевую температуту контролирует задача нагрева). 
    Выдает в систему включена/выключена помпа (режим работы помпы контролирует задача помпы). 
	Выдает данные для отображения на дисплее (по крайней мере часть данных)
	По команде "Стоп" вызывается SYSTEM_DOWN(USER_STOP).

IN_RTC      → C_TIME (current time, для выдержки пауз)
IN_BTNS     → PAUSE, STOP, TEMP+, TEMP-, PUMP_ON/OFF

OUT RCP_DATA SPMC:
  TEMP_GOAL       — целевая температура
  PUMP_FLAG       — вкл/выкл помпа
  HEAT_FLAG       — вкл/выкл нагрев
  BREWING_STAGE   — текущая стадия
  TICK            — прокид для сейфти (наблюдаемость)

RCP_ALERTS RING  →  OUT_TSK   (вывод: дисплей, зуммер, LED — внутреннее дело задачи)
RCP_DATA   SPMC  →  OUT_TSK   (текущее состояние для отображения)
                 →  HEAT_TSK
                 →  PUMP_TSK

________________________________________________________________________________________________________________________________________________________________
|RT_TIME  buffer 		|IN_RTC      |TEMP_SNSR_TSK |NON_RT         |OUT_TEMP           |CRRNT_TEMP SPMC   | current temperature, tick 
TEMP_SNSR_TSK — задача чтения датчика температуры.

Не привязана к реальному времени (NON_RT).
Напрямую обращается к HAL::GetTemp() — реализация скрыта за HAL.

Внутренняя FSM:
    IDLE → запускает конверсию DS18B20
    CONVERTING → выдерживает ~750ms по счётчику тиков
    READING → читает результат через HAL::GetTemp()
    → IDLE → пишет в OUT_TEMP, обновляет tick

Выдаёт:
    Текущую температуру и tick в OUT_TEMP (CRRNT_TEMP SPMC).

Консьюмеры OUT_TEMP:
    HEAT_TSK   — регулирование
    SAFETY_TSK — контроль перегрева
    OUT_TSK    — отображение

________________________________________________________________________________________________________________________________________________________________
|RCP_DATA SPMC          |IN_RCP_DATA |HEAT_ACT_TSK  |RT/HAL-dep     |OUT_HTR_STATE      |HTR_STATE SPMC | Heater on/off, tick
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP|  -//-//-     |               |                   |               |
HEAT_ACT_TSK — включает/выключает нагреватель по гистерезису, читая уставку и текущую температуру.

RT/HAL-dep. Период вызова критичен для своевременного переключения нагрева.

Читает:
    TEMP_GOAL, HEAT_FLAG из IN_RCP_DATA (RCP_DATA SPMC)
    current_temp из IN_CRNT_TEMP (CRRNT_TEMP SPMC)

Алгоритм: гистерезисный регулятор.
    HEAT_FLAG == 0 → нагрев выключен независимо от температуры.
    HEAT_FLAG == 1 → temp < TEMP_GOAL - δ → HAL::SetHeater(ON)
                     temp > TEMP_GOAL + δ → HAL::SetHeater(OFF)

Выдаёт:
    Состояние нагревателя и tick в OUT_HTR_STATE (HTR_STATE SPMC).

Консьюмеры OUT_HTR_STATE:
    OUT_TSK    — отображение
    SAFETY_TSK — контроль состояния нагревателя

Требование к HAL:
    HAL::SetHeater() обязан обеспечивать bounded execution — 
    никаких блокирующих вызовов, очередей, ожидания шины.
    Нарушение контракта переводит задачу из RT в NON_RT.
    Ответственность за выполнение контракта лежит на порте HAL,
    не на задаче.


________________________________________________________________________________________________________________________________________________________________
|RCP_DATA SPMC          |IN_RCP_DATA |PUMP_ACT_TSK  |RT/HAL-dep     |OUT_PMP_STATE      |PMP_STATE SPMC | Pump state (on/off/rest), tick
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP|  -//-//-     |               |                   |               |
PUMP_ACT_TSK — задача управления помпой.

RT. Период вызова критичен для своевременного переключения помпы.

Читает:
    PUMP_FLAG из IN_RCP_DATA (RCP_DATA SPMC)
    current_temp из IN_CRNT_TEMP (CRRNT_TEMP SPMC)

Внутренняя FSM:
    PUMP_FLAG == 0 → OFF (немедленно, из любого состояния)
    PUMP_FLAG == 1 → ON  → (отработал N тиков) → REST
                  → REST → (отдохнул M тиков)  → ON
    N, M — константы времени компиляции.

Выдаёт:
    Состояние помпы (ON/OFF/REST) и tick в OUT_PMP_STATE (PMP_STATE SPMC).

Консьюмеры OUT_PMP_STATE:
    OUT_TSK    — отображение
    SAFETY_TSK — контроль состояния помпы

Требование к HAL:
    HAL::SetPump() обязан обеспечивать bounded execution —
    никаких блокирующих вызовов, очередей, ожидания шины.
    Нарушение контракта переводит задачу из RT в NON_RT.
    Ответственность за выполнение контракта лежит на порте HAL,
    не на задаче.
________________________________________________________________________________________________________________________________________________________________
|RT_TIME buffer         |IN_RTC      |FLOW_SNSR_TSK |NON_RT         |OUT_FLOW           |CRRNT_FLOW     | current flowmetr, tick 
FLOW_SNSR_TSK — задача чтения датчика потока.

NON_RT. 
Напрямую обращается к HAL::GetFlow() — реализация скрыта за HAL.
Тип датчика (импульсный/аналоговый) определяется BOM конкретной платформы.

Текущие применения:
    Защита от сухого хода — передаётся в SAFETY_TSK.
    Возможно в будущем: регулировка мощности помпы.

Выдаёт:
    Текущий поток и tick в OUT_FLOW (CRRNT_FLOW SPMC).

Консьюмеры OUT_FLOW:
    SAFETY_TSK — защита от сухого хода
    OUT_TSK    — отображение (опционально)


|Reading chnl           |In_channel     |Task name     |Task domain    |Out_Channel        |Writing        |Data
________________________________________________________________________________________________________________________________________________________________
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP   |SAFETY_TSK    |RT             |—                  |—              |—
|HTR_STATE SPMC         |IN_HTR_STATE   |-//-//-//     |               |                   |               |
|PMP_STATE SPMC         |IN_PMP_STATE   |-//-//-//     |               |                   |               |
|CRRNT_FLOW SPMC        |IN_FLOW        |-//-//-//     |               |                   |               |
|RCP_DATA SPMC        	|IN_RCP_DATA    |-//-//-//     |               |                   |               |
SAFETY_TSK — задача защиты системы.

RT, высокий приоритет.

Читает:
    IN_CRNT_TEMP   — CRRNT_TEMP SPMC   (перегрев)
    IN_HTR_STATE   — HTR_STATE SPMC    (состояние нагревателя)
    IN_PMP_STATE   — PMP_STATE SPMC    (состояние помпы)
    IN_FLOW        — CRRNT_FLOW SPMC   (сухой ход)
	IN_RCP_DATA    — RCP_DATA SPMC 		(наблюдаемость)

При срабатывании любого условия защиты:
    Вызывает SYSTEM_DOWN(SAFETY_FAULT).

SYSTEM_DOWN(reason):
    Штатный механизм останова планировщика.
    Причины: BREW_COMPLETE, SAFETY_FAULT, USER_STOP, WDT_FAULT, ...
    После останова — поставарийный обработчик вне планировщика:
    вывод на дисплей, зуммер, ожидание реакции пользователя.

Отдельного выходного канала нет.
    Воздействие на систему — через SYSTEM_DOWN(), не через SPMC/RING.	
 
 |Reading chnl  |In_channel  |Task name  |Task domain  |Out_Channel   |Writing         |Data
|—             |—           |RTC_TSK    |NON_RT       |OUT_RTC       |RT_TIME buffer  |C_TIME, tick
RTC_TSK — задача чтения реального времени.

NON_RT.
Напрямую обращается к HAL::GetTime() — скрывает внутренний
(STM32F1/F4/F7, RP2040) или внешний RTC.

Выдаёт:
    C_TIME и tick в OUT_RTC (RT_TIME buffer).
    tick обязателен — наблюдаемость задачи.

Консьюмеры OUT_RTC:
    RECEIP_TSK — выдержка времени пауз
	TEMP_SNSR_TSK - опрос DS18B20

Требование к HAL:
    HAL::GetTime() — no blocking calls.

|Reading chnl  |In_channel  |Task name  |Task domain  |Out_Channel   |Writing        |Data
|—             |—           |BTN_TSK    |NON_RT       |OUT_BTNS      |EVT_BTNS RING  |PAUSE, STOP, TEMP+, TEMP-, PUMP_ON/OFF
BTN_TSK — задача чтения кнопок.

NON_RT.
Напрямую обращается к HAL::GetButtons() — реализация скрыта за HAL.

Внутренняя FSM:
    Программный дебаунс — внутреннее дело задачи.
    При подтверждённом нажатии пишет событие в EVT_BTNS RING.

Выдаёт:
    События кнопок в OUT_BTNS (EVT_BTNS RING):
    PAUSE, STOP, TEMP+, TEMP-, PUMP_ON/OFF

Консьюмеры OUT_BTNS:
    RECEIP_TSK — управление рецептом и ручной режим

Примечание:
    Энкодер — вне текущего скопа, потенциальное расширение.
    При добавлении энкодера — либо расширение BTN_TSK,
    либо отдельная ENC_TSK, решение при необходимости.


|Reading chnl           |In_channel     |Task name  |Task domain  |Out_Channel  |Writing  |Data
|RCP_DATA SPMC          |IN_RCP_DATA    |OUT_TSK    |NON_RT       |—            |—        |—
|RCP_ALERTS RING        |IN_RCP_ALERTS  |-//-//-//  |             |             |         |
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP   |-//-//-//  |             |             |         |
|HTR_STATE SPMC         |IN_HTR_STATE   |-//-//-//  |             |             |         |
|PMP_STATE SPMC         |IN_PMP_STATE   |-//-//-//  |             |             |         |

OUT_TSK — задача интерфейсного вывода.

NON_RT. Период вызова 0.1 сек.
Платформозависимая — что и куда выводить внутреннее дело задачи
(дисплей, зуммер, LED и т.д.).

Читает на каждом вызове:
    IN_RCP_DATA    — RCP_DATA SPMC   (стадия, TEMP_GOAL, флаги)
    IN_RCP_ALERTS  — RCP_ALERTS RING (REMOVE_GRAIN, ADD_HOP, BREW_DONE)
    IN_CRNT_TEMP   — CRRNT_TEMP SPMC (текущая температура)
    IN_HTR_STATE   — HTR_STATE SPMC  (состояние нагревателя)
    IN_PMP_STATE   — PMP_STATE SPMC  (состояние помпы)

Выходного канала нет.
    Воздействие на систему — через HAL (дисплей, зуммер, LED).

Примечание:
    CRRNT_FLOW — не читается, вне скопа отображения.
    RCP_ALERTS RING — события, обрабатываются по мере появления
    в кольцевом буфере на каждом вызове задачи.

Требование к HAL:
    HAL::Display() — no blocking calls longer than period (0.1 сек).


Итого:

|Reading chnl           |In_channel     |Task name     |Task domain    |Out_Channel        |Writing         |Data
________________________________________________________________________________________________________________________________________________________________
|RT_TIME buffer         |IN_RTC         |RECEIP_TSK    |RT             |OUT_RCP_DATA       |RCP_DATA SPMC   |TEMP_GOAL, PUMP_FLAG, HEAT_FLAG, BREWING_STAGE, TICK
|EVT_BTNS ring          |IN_BTNS        |-//-//-//     |               |OUT_RCP_ALERTS     |RCP_ALERTS RING |REMOVE_GRAIN, ADD_HOP, BREW_DONE
________________________________________________________________________________________________________________________________________________________________
|RT_TIME buffer         |IN_RTC      	|TEMP_SNSR_TSK |NON_RT         |OUT_TEMP           |CRRNT_TEMP SPMC |current_temp, tick
________________________________________________________________________________________________________________________________________________________________
|RT_TIME buffer         |IN_RTC      	|FLOW_SNSR_TSK |NON_RT         |OUT_FLOW           |CRRNT_FLOW SPMC |current_flow, tick
________________________________________________________________________________________________________________________________________________________________
|RCP_DATA SPMC          |IN_RCP_DATA    |HEAT_ACT_TSK  |RT/HAL-dep     |OUT_HTR_STATE      |HTR_STATE SPMC  |heater on/off, tick
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP   |-//-//-//     |               |                   |                |
________________________________________________________________________________________________________________________________________________________________
|RCP_DATA SPMC          |IN_RCP_DATA    |PUMP_ACT_TSK  |RT/HAL-dep     |OUT_PMP_STATE      |PMP_STATE SPMC  |pump on/off/rest, tick
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP   |-//-//-//     |               |                   |                |
________________________________________________________________________________________________________________________________________________________________
|—                      |—              |RTC_TSK       |NON_RT         |OUT_RTC            |RT_TIME buffer  |C_TIME, tick
________________________________________________________________________________________________________________________________________________________________
|—                      |—              |BTN_TSK       |NON_RT         |OUT_BTNS           |EVT_BTNS RING   |PAUSE, STOP, TEMP+, TEMP-, PUMP_ON/OFF
________________________________________________________________________________________________________________________________________________________________
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP   |SAFETY_TSK    |RT             |—                  |—               |—
|HTR_STATE SPMC         |IN_HTR_STATE   |-//-//-//     |               |                   |                |
|PMP_STATE SPMC         |IN_PMP_STATE   |-//-//-//     |               |                   |                |
|CRRNT_FLOW SPMC        |IN_FLOW        |-//-//-//     |               |                   |                |
|RCP_DATA SPMC        	|IN_RCP_DATA    |-//-//-//     |               |                   |                |
________________________________________________________________________________________________________________________________________________________________
|RCP_DATA SPMC          |IN_RCP_DATA    |OUT_TSK       |NON_RT         |—                  |—               |—
|RCP_ALERTS RING        |IN_RCP_ALERTS  |-//-//-//     |               |                   |                |
|CRRNT_TEMP SPMC        |IN_CRNT_TEMP   |-//-//-//     |               |                   |                |
|HTR_STATE SPMC         |IN_HTR_STATE   |-//-//-//     |               |                   |                |
|PMP_STATE SPMC         |IN_PMP_STATE   |-//-//-//     |               |                   |                |
________________________________________________________________________________________________________________________________________________________________