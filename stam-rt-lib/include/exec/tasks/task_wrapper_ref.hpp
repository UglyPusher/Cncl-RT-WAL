#pragma once
#include "exec/tasks/task_wrapper.hpp"


namespace stam::exec::tasks {

struct TaskWrapperRef {
    void* obj = nullptr;
    void (*step_fn)(void*, stam::model::tick_t) noexcept = nullptr;
    void (*init_fn)(void*) noexcept = nullptr;
    void (*alarm_fn)(void*) noexcept = nullptr;
    void (*done_fn)(void*) noexcept = nullptr;
    bool (*is_fully_bound_fn)(const void*) noexcept = nullptr;
};

template <class Payload>
requires stam::model::Steppable<Payload>
TaskWrapperRef make_task_wrapper_ref(TaskWrapper<Payload>& wrapper) noexcept {
    TaskWrapperRef ref{};
    ref.obj = &wrapper;
    ref.step_fn = [](void* obj, stam::model::tick_t now) noexcept {
        static_cast<TaskWrapper<Payload>*>(obj)->step(now);
    };
    ref.init_fn = [](void* obj) noexcept {
        static_cast<TaskWrapper<Payload>*>(obj)->init();
    };
    ref.alarm_fn = [](void* obj) noexcept {
        static_cast<TaskWrapper<Payload>*>(obj)->alarm();
    };
    ref.done_fn = [](void* obj) noexcept {
        static_cast<TaskWrapper<Payload>*>(obj)->done();
    };
    ref.is_fully_bound_fn = [](const void* obj) noexcept {
        return static_cast<const TaskWrapper<Payload>*>(obj)->is_fully_bound();
    };

    return ref;
}

} // namespace stam::exec::tasks
