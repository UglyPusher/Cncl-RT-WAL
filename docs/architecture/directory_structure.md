rt-wal/                          # repo name (не трогаем)
│   CMakeLists.txt
│   README.md                    # "RT-WAL: Real-Time Execution Framework"
│   LICENSE
│   design.md
│
├───docs
│   ├───architecture
│   │       layering.md
│   │       dependency_graph.md
│   │
│   ├───contracts
│   │       spsc_ring.md
│   │       double_buffer.md
│   │       task_model.md
│   │       safety_fsm.md
│   │       logging_pipeline.md
│   │
│   ├───portability
│   │       portability_model.md
│   │       user_config_example.hpp
│   │
│   └───hardware
│           reference_schematic.md
│           safety_lines.md
│
├───include
│   ├───core                     # minimal, расширяем по необходимости
│   │       types.hpp
│   │       result.hpp
│   │       concepts.hpp
│   │
│   ├───sys                      # portability layer
│   │       sys_config.hpp
│   │       sys_compiler.hpp
│   │       sys_arch.hpp
│   │       sys_align.hpp
│   │       sys_fence.hpp
│   │       sys_platform.hpp
│   │       sys_rt.hpp
│   │
│   ├───hal                      # ТОЛЬКО интерфейсы (базовый набор)
│   │       tick.hpp
│   │       gpio.hpp
│   │       adc.hpp
│   │       watchdog.hpp
│   │
│   ├───exec                     # execution model
│   │       task.hpp
│   │       exec_policy_rt.hpp
│   │       exec_policy_nonrt.hpp
│   │
│   ├───rt
│   │   ├───transport            # RT-safe data exchange
│   │   │       spsc_ring.hpp
│   │   │       double_buffer.hpp
│   │   │
│   │   ├───control              # reusable controllers
│   │   │       pid.hpp
│   │   │       bangbang.hpp
│   │   │
│   │   ├───fsm                  # deterministic FSM
│   │   │       fsm.hpp
│   │   │       safety_fsm.hpp   # generic WARN→LIMIT→SHED→PANIC
│   │   │
│   │   ├───sensor
│   │   │       validator.hpp
│   │   │       filter.hpp
│   │   │
│   │   └───logging              # RT publish (states, events, logs)
│   │           record.hpp
│   │           publisher.hpp
│   │
│   └───nonrt
│       ├───drain
│       │       ring_drain.hpp
│       │
│       ├───backend
│       │       file_backend.hpp
│       │       memory_backend.hpp
│       │
│       ├───dispatcher
│       │       dispatcher.hpp
│       │
│       └───analytics
│               metrics.hpp
│
├───src
│   ├───hal                      # platform-specific impl
│   │   ├───linux
│   │   ├───stm32
│   │   └───x86
│   │
│   ├───nonrt                    # only non-RT impl
│   │   ├───backend
│   │   └───dispatcher
│   │
│   └───exec
│
├───apps
│   ├───brewery                  # showcase
│   │   │   CMakeLists.txt
│   │   │   README.md
│   │   │
│   │   ├───hal                  # app-specific HAL (pwm, spi, etc.)
│   │   │       pwm.hpp
│   │   │       onewire.hpp
│   │   │
│   │   ├───rt_domain
│   │   │       main_rt.cpp
│   │   │       temp_control.cpp
│   │   │       safety_monitor.cpp
│   │   │
│   │   ├───nonrt_domain
│   │   │       logger_daemon.cpp
│   │   │       config_loader.cpp
│   │   │
│   │   └───sim
│   │           virtual_sensors.cpp
│   │
│   └───minimal
│           main.cpp
│
└───tests
    ├───contracts
    ├───unit
    └───integration