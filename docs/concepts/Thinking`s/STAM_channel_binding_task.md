# THINKING NOTE

This file contains architecture reasoning/discussion history.
It is not the canonical implementation contract.

Use source-of-truth docs:
- stam-rt-lib/docs/*
- primitives/docs/*
- docs/architecture/*

---

Каталоги документации 
 - docs\concepts\fixed
 - primitives\docs

Каталог обсуждения docs\concepts\sys_regestery_architecture
Каталог кода stam-rt-lib\include\exec\tasks

Файлы постановки задачи - docs\concepts\sys_regestery_architecture\task.md

Файлы для модификации:
 - stam-rt-lib\include\exec\tasks\taskwrapper.hpp
 - stam-rt-lib\include\model\tags.hpp

Проблема - привязка каналов передачи информации к задаче. Сейчас механизм привязки канала связи к задаче не прописан и не реализован.

Фаза первая - обсуждаем модификацию враппера и тагов для привязки каналов связи к врапперу. Правим документацию при необходимости. Приходим к конценсусу.

Фаза вторая - вносим изменения в код.

Формат обсуждения - файл docs\concepts\sys_regestery_architecture\STAM_channel_binding_conversation.md
[Сессия НННН][Имя (Клод/Кодекс/Оператор)]   Существо замечания/предложения



