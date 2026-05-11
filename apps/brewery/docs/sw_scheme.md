---
doc_id: SW_SCHEME_LEGACY
title: Legacy Brewery Software State Sketch
status: reference
version: 0.1
depends_on: []
review_scope:
  - legacy_state_model
---

Базовые состояния контроллера:

INIT
CONFIG
AUTO_BREW
MAN_BREW
ERROR

Переходы:
INIT - CONFIG
INIT - AUTO_BREW
INIT - MAN_BREW

CONFIG - INIT
AUTO_BREW - INIT
MAN_BREW - INIT

CONFIG - ERROR
AUTO_BREW - ERROR
MAN_BREW - ERROR

ERROR - INIT (ручной сброс)

Субсостояния CONFIG:
SYS_SET
RECEIP_SET
