# Перечень задач (STM32)

## 1. sensor_task
- Тип: NON-RT  
- Читает каналы: —  
- Пишет в каналы:  
  - temperature_raw / out_temp  
- Зависит от железа (читает):  
  - DS18B20 (1-Wire)  
- Наблюдает железо (читает):  
  - CRC датчика  
- Управляет железом (пишет):  
  - 1-Wire (запуск конверсии)

---

## 2. level_input_task
- Тип: ISR (EXTI) + atomic state (без буфера)  
- Читает каналы: —  
- Пишет в каналы:  
  - level_raw / out_level  
- Зависит от железа (читает):  
  - GPIO (датчик уровня через оптопару)  
- Наблюдает железо (читает):  
  - уровень сигнала (OK/NOT_OK)  
- Управляет железом (пишет): —  

---

## 3. state_aggregator
- Тип: RT  
- Читает каналы:  
  - temperature_raw / in_temp  
  - level_raw / in_level  
- Пишет в каналы:  
  - temperature_valid / out_temp_valid  
  - level_state / out_level_state  
- Зависит от железа (читает): —  
- Наблюдает железо (читает): —  
- Управляет железом (пишет): —  

---

## 4. fsm_task
- Тип: NON-RT  
- Читает каналы:  
  - temperature_valid / in_temp  
  - level_state / in_level  
  - ui_input / in_ui  
- Пишет в каналы:  
  - target_temp / out_target  
  - pump_command / out_pump  
  - mode_state / out_mode  
- Зависит от железа (читает): —  
- Наблюдает железо (читает): —  
- Управляет железом (пишет): —  

---

## 5. pid_task
- Тип: RT  
- Читает каналы:  
  - temperature_valid / in_temp  
  - target_temp / in_target  
- Пишет в каналы:  
  - heater_power / out_power  
- Зависит от железа (читает): —  
- Наблюдает железо (читает): —  
- Управляет железом (пишет): —  

---

## 6. safety_task
- Тип: RT (HIGH PRIORITY)  
- Читает каналы:  
  - temperature_valid / in_temp  
  - level_state / in_level  
  - heater_power / in_power  
  - mode_state / in_mode  
- Пишет в каналы:  
  - safety_trip / out_trip  
  - safety_reason / out_reason  
- Зависит от железа (читает): —  
- Наблюдает железо (читает): —  
- Управляет железом (пишет): —  

---

## 7. actuator_task
- Тип: RT  
- Читает каналы:  
  - heater_power / in_power  
  - pump_command / in_pump  
  - safety_trip / in_trip  
- Пишет в каналы: —  
- Зависит от железа (читает): —  
- Наблюдает железо (читает): —  
- Управляет железом (пишет):  
  - SSR (ТЭН)  
  - Relay1 (помпа 1)  
  - Relay2 (помпа 2)  

---

## 8. stm8_link_task
- Тип: NON-RT  
- Читает каналы:  
  - temperature_valid / in_temp  
  - safety_trip / in_trip  
- Пишет в каналы: —  
- Зависит от железа (читает): —  
- Наблюдает железо (читает):  
  - GPIO heartbeat от STM8 (опционально)  
- Управляет железом (пишет):  
  - USART TX (кадр в STM8)

---

## 9. ui_task
- Тип: NON-RT  
- Читает каналы:  
  - mode_state / in_mode  
  - temperature_valid / in_temp  
  - safety_trip / in_trip  
  - safety_reason / in_reason  
- Пишет в каналы:  
  - ui_input / out_ui  
- Зависит от железа (читает):  
  - GPIO (кнопки)  
- Наблюдает железо (читает): —  
- Управляет железом (пишет):  
  - I2C (LCD2004)

---

## 10. logger_task
- Тип: NON-RT  
- Читает каналы:  
  - temperature_valid / in_temp  
  - mode_state / in_mode  
  - safety_trip / in_trip  
  - safety_reason / in_reason  
- Пишет в каналы:  
  - log_stream / out_log  
- Зависит от железа (читает): —  
- Наблюдает железо (читает): —  
- Управляет железом (пишет):  
  - UART / storage (опционально)

---

# Перечень каналов

## 1. temperature_raw
- Пишущие задачи:  
  - sensor_task / out_temp  
- Читающие задачи:  
  - state_aggregator / in_temp  
- Суть данных: сырое значение температуры  
- Тип канала (STAM): `Mailbox2Slot` (snapshot publish, last value)

---

## 2. level_raw
- Пишущие задачи:  
  - level_input_task / out_level  
- Читающие задачи:  
  - state_aggregator / in_level  
- Суть данных: сырое состояние уровня (GPIO)  
- Тип канала (STAM): `Mailbox2Slot` (snapshot publish, last value)

---

## 3. temperature_valid
- Пишущие задачи:  
  - state_aggregator / out_temp_valid  
- Читающие задачи:  
  - fsm_task / in_temp  
  - pid_task / in_temp  
  - safety_task / in_temp  
  - ui_task / in_temp  
  - logger_task / in_temp  
  - stm8_link_task / in_temp  
- Суть данных: валидированная температура или INVALID  
- Тип канала (STAM): `SPMCSnapshot` (single producer, multi consumer)

---

## 4. level_state
- Пишущие задачи:  
  - state_aggregator / out_level_state  
- Читающие задачи:  
  - fsm_task / in_level  
  - safety_task / in_level  
- Суть данных: OK / NOT_OK  
- Тип канала (STAM): `SPMCSnapshot` (single producer, multi consumer)

---

## 5. target_temp
- Пишущие задачи:  
  - fsm_task / out_target  
- Читающие задачи:  
  - pid_task / in_target  
- Суть данных: целевая температура  
- Тип канала (STAM): `Mailbox2Slot` (snapshot publish, last value)

---

## 6. pump_command
- Пишущие задачи:  
  - fsm_task / out_pump  
- Читающие задачи:  
  - actuator_task / in_pump  
- Суть данных: команда управления помпами  
- Тип канала (STAM): `Mailbox2Slot` (snapshot publish, last value)

---

## 7. heater_power
- Пишущие задачи:  
  - pid_task / out_power  
- Читающие задачи:  
  - actuator_task / in_power  
  - safety_task / in_power  
- Суть данных: мощность ТЭНа (0..1)  
- Тип канала (STAM): `SPMCSnapshot` (single producer, multi consumer)

---

## 8. safety_trip
- Пишущие задачи:  
  - safety_task / out_trip  
- Читающие задачи:  
  - actuator_task / in_trip  
  - stm8_link_task / in_trip  
  - ui_task / in_trip  
  - logger_task / in_trip  
- Суть данных: аварийный флаг (latch)  
- Тип канала (STAM): `SPMCSnapshot` (single producer, multi consumer; high priority)

---

## 9. safety_reason
- Пишущие задачи:  
  - safety_task / out_reason  
- Читающие задачи:  
  - ui_task / in_reason  
  - logger_task / in_reason  
- Суть данных: код причины аварии  
- Тип канала (STAM): `SPMCSnapshot` (single producer, multi consumer)

---

## 10. mode_state
- Пишущие задачи:  
  - fsm_task / out_mode  
- Читающие задачи:  
  - safety_task / in_mode  
  - ui_task / in_mode  
  - logger_task / in_mode  
- Суть данных: INIT / MANUAL / AUTO / PAUSE  
- Тип канала (STAM): `SPMCSnapshot` (single producer, multi consumer)

---

## 11. ui_input
- Пишущие задачи:  
  - ui_task / out_ui  
- Читающие задачи:  
  - fsm_task / in_ui  
- Суть данных: события от кнопок  
- Тип канала (STAM): `SPSCRing` (event stream)

---

## 12. log_stream
- Пишущие задачи:  
  - logger_task / out_log  
- Читающие задачи:  
  - внешний потребитель / storage  
- Суть данных: события/логи  
- Тип канала (STAM): `SPSCRing` (event stream)
