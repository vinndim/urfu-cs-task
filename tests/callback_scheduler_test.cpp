#include "callback_scheduler.h"

#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>

using namespace std::chrono_literals;

TEST(CallbackSchedulerTest, Simple)
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    auto callback = [&] {
        std::unique_lock<std::mutex> lock(m);
        done = true;
        cv.notify_one();
    };

    CallbackScheduler scheduler;
    auto when = std::chrono::system_clock::now() + 1s;
    scheduler.Schedule(std::move(callback), when);

    std::unique_lock<std::mutex> lock(m);
    const bool signaled = cv.wait_for(lock, 3s, [&] { return done; });
    EXPECT_TRUE(signaled) << "Callback was not called in time";
}

TEST(CallbackSchedulerTest, NotEarlierThanRequested)
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    auto fired_at = std::chrono::system_clock::time_point{};

    auto when = std::chrono::system_clock::now() + 300ms;

    auto callback = [&] {
        std::unique_lock<std::mutex> lock(m);
        fired_at = std::chrono::system_clock::now();
        done = true;
        cv.notify_one();
    };

    CallbackScheduler scheduler;
    scheduler.Schedule(std::move(callback), when);

    std::unique_lock<std::mutex> lock(m);
    const bool signaled = cv.wait_for(lock, 2s, [&] { return done; });
    ASSERT_TRUE(signaled) << "Callback was not called in time";
    EXPECT_GE(fired_at, when);
}

TEST(CallbackSchedulerTest, EarlierTaskInsertedLaterExecutesFirst)
{
    using namespace std::chrono_literals;

    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    std::vector<int> order;

    CallbackScheduler scheduler;

    auto now = std::chrono::system_clock::now();

    scheduler.Schedule([&]
    {
        std::lock_guard lock(m);
        order.push_back(2);
    }, now + 800ms);

    scheduler.Schedule([&]
    {
        std::lock_guard lock(m);
        order.push_back(1);
    }, now + 200ms);

    std::unique_lock lock(m);
    cv.wait_for(lock, 2s, [&]
    {
        return order.size() == 2;
    });

    ASSERT_EQ(order.size(), 2);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}