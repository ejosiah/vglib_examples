#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>

struct Condition {

    using Predicate = std::function<bool()>;

    void wait() {
        std::unique_lock<std::mutex> lck{ _m };
        _cv.wait(lck);
    }

    void wait(Predicate&& predicate) {
        std::unique_lock<std::mutex> lck{ _m };
        _cv.wait(lck, predicate);
    }

    void signal() {
        _cv.notify_one();
    }

    void signalAll() {
        _cv.notify_all();
    }
private:
    std::condition_variable _cv;
    std::mutex _m;
};