#pragma once

#include <mutex>

class Lock {

    auto lock(auto section) {
        std::lock_guard<std::mutex> lck{_mutex};
        section();
    }

    auto try_lock(auto section) {
        if(_mutex.try_lock()) {
            try {
                section();
            } catch(...) {
                _mutex.unlock();
            }
        }
    }

private:
    std::mutex _mutex;
};