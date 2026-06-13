#include "iostream"
#include "thread"

#include "callback_scheduler.h"

struct CallbackScheduler::ScheduledTask
{
    TaskId id;
    TimePoint executeTime;
    std::function<void()> callback;
    bool cancelled = false;
};

bool CallbackScheduler::Compare::operator()(const TaskPtr& lhs, const TaskPtr& rhs) const
{
    return lhs->executeTime > rhs->executeTime;
}

CallbackScheduler::CallbackScheduler() : m_stop(false)
{
    m_worker = std::thread([this]
    {
        WorkerLoop();
    });
}

CallbackScheduler::~CallbackScheduler()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }

    m_cv.notify_one();

    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

CallbackScheduler::TaskId CallbackScheduler::Schedule(std::function<void()> callback, TimePoint when)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const TaskId id = m_nextId++;

    auto task = std::make_shared<ScheduledTask>();
    task->id = id;
    task->executeTime = when;
    task->callback = std::move(callback);
    task->cancelled = false;

    m_tasks.push(task);
    m_activeTasks.emplace(id, task);

    // разбудить worker (возможно это новая более ранняя задача)
    m_cv.notify_one();

    return id;
}

bool CallbackScheduler::Cancel(TaskId id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_activeTasks.find(id);
    if (it == m_activeTasks.end())
    {
        return false;
    }

    it->second->cancelled = true;

    // разбудим worker, чтобы он не выполнил уже отменённую задачу
    m_cv.notify_one();

    return true;
}

void CallbackScheduler::WorkerLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    while (true)
    {
        // Ждём, пока появится работа или остановка
        m_cv.wait(lock, [this]
        {
            return m_stop || !m_tasks.empty();
        });

        if (m_stop)
        {
            return;
        }

        // Берём ближайшую задачу (она всегда top heap)
        auto task = m_tasks.top();

        const auto now = std::chrono::system_clock::now();

        // Если задача ещё не готова - спим до её времени
        if (task->executeTime > now)
        {
            m_cv.wait_until(lock, task->executeTime);
            continue; // состояние могло измениться - пересчитываем заново
        }

        // Забираем задачу из структур планировщика
        m_tasks.pop();
        m_activeTasks.erase(task->id);

        if (task->cancelled)
        {
            continue;
        }

        // Извлекаем callback до выхода из критической секции
        std::function<void()> callback = std::move(task->callback);

        lock.unlock();

        try
        {
            callback();
        }
        catch (const std::exception& e)
        {
            std::cerr << "[CallbackScheduler] std::exception in callback: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "[CallbackScheduler] unknown exception in callback" << std::endl;
        }

        // Возвращаем mutex для следующей итерации цикла
        lock.lock();
    }
}
