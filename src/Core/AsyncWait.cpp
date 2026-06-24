#include "AsyncWait.h"

#include <condition_variable>
#include <mutex>

bool WaitForDelay(std::stop_token stopToken, std::chrono::milliseconds delay) {
    if (stopToken.stop_requested()) {
        return false;
    }

    std::condition_variable condition;
    std::mutex mutex;
    std::unique_lock lock(mutex);
    std::stop_callback stopCallback(stopToken, [&condition]() {
        condition.notify_all();
    });
    condition.wait_for(lock, delay, [&stopToken]() {
        return stopToken.stop_requested();
    });
    return !stopToken.stop_requested();
}
