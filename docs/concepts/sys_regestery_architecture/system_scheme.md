┌───────────────────────────────────────────────┐
│                USER PAYLOAD                   │
│  (алгоритмы задач, бизнес-логика)            │
│                                               │
│  step()                                      │
│  init() alarm() done()                       │
│                                               │
│  InPort<T> / OutPort<T>                       │
└───────────────────────────────────────────────┘
                     │
                     ▼
┌───────────────────────────────────────────────┐
│                TASK WRAPPER                   │
│                                               │
│  TaskWrapper<Payload>                         │
│                                               │
│  wrapper.step()                               │
│      → payload.step()                         │
│      → heartbeat.store()                      │
│                                               │
│  attach_hb()                                  │
│  init()/alarm()/done()                        │
└───────────────────────────────────────────────┘
                     │
                     ▼
┌───────────────────────────────────────────────┐
│                PORT LAYER                     │
│                                               │
│  InPort<T>                                    │
│  OutPort<T>                                   │
│                                               │
│  port.publish()                               │
│  port.try_read()                              │
│  port.push()                                  │
│  port.pop()                                   │
│                                               │
│  лёгкий handle на примитив канала             │
└───────────────────────────────────────────────┘
                     │
                     ▼
┌───────────────────────────────────────────────┐
│             CHANNEL PRIMITIVES                │
│                                               │
│  EventChannel<T>                              │
│      → SPSCRing<T>                            │
│                                               │
│  StateChannel<T,N>                            │
│      → SPMCSnapshot<T,N>                      │
│                                               │
│  wait-free RT операции                        │
│  push/pop/publish/try_read                    │
└───────────────────────────────────────────────┘
                     │
                     ▼
┌───────────────────────────────────────────────┐
│            SYSTEM REGISTRY (bootstrap)        │
│                                               │
│  TaskDescriptor                               │
│  ChannelDescriptor                            │
│                                               │
│  registry.add(task, wrapper)                  │
│  registry.assign_port(...)                    │
│  registry.add(channel)                        │
│                                               │
│  seal()                                       │
│      → validate topology                      │
│      → assign task_index                      │
│      → build signal_mask                      │
│                                               │
│  после seal() → read-only                     │
└───────────────────────────────────────────────┘
                     │
                     ▼
┌───────────────────────────────────────────────┐
│               RUNTIME SYSTEM                  │
│                                               │
│  scheduler                                    │
│  safety monitor                               │
│  interrupt system                             │
│                                               │
│  runtime читает registry как таблицу          │
│                                               │
│  scheduler → wrapper.step()                   │
└───────────────────────────────────────────────┘
                     │
                     ▼
┌───────────────────────────────────────────────┐
│                 HARDWARE                      │
│                                               │
│  timers                                       │
│  interrupts                                   │
│  cores                                        │
│                                               │
└───────────────────────────────────────────────┘