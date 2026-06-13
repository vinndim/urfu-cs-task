#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>

using TimePoint = std::chrono::time_point<std::chrono::system_clock>;
class CallbackScheduler
{
public:
    using TaskId = std::uint64_t;

    CallbackScheduler();
    ~CallbackScheduler();

    CallbackScheduler(const CallbackScheduler&) = delete;
    CallbackScheduler& operator=(const CallbackScheduler&) = delete;

    CallbackScheduler(CallbackScheduler&&) = delete;
    CallbackScheduler& operator=(CallbackScheduler&&) = delete;

    TaskId Schedule(std::function<void()> callback, TimePoint when);
    bool Cancel(TaskId id);
    void WorkerLoop();
private:
    struct ScheduledTask;
    using TaskPtr = std::shared_ptr<ScheduledTask>;
    struct Compare
    {
        bool operator()(const TaskPtr& lhs, const TaskPtr& rhs) const;
    };

    std::priority_queue<TaskPtr, std::vector<TaskPtr>, Compare> m_tasks;
    std::unordered_map<TaskId, TaskPtr> m_activeTasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_stop = false;
    TaskId m_nextId = 1;
};
