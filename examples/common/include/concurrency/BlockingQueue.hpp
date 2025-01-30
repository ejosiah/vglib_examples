#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <optional>

template<typename T>
class BlockingQueue {
public:
    struct invalid_state : std::exception {};

    BlockingQueue() = default;

    explicit BlockingQueue(size_t capacity)
    : _capacity{capacity}
    , _data(capacity)
    {}

     void push(const T& entry) {
        std::unique_lock<std::mutex> lck{ _mtx };
        if(_head == _capacity) {
            _full.wait(lck, [this]{ return _head < _capacity; });
        }
        _data[_head++] = entry;
        _empty.notify_all();
    }

    void push(T&& entry) {
        std::unique_lock<std::mutex> lck{ _mtx };
        if(_head == _capacity) {
            _full.wait(lck, [this]{ return _head < _capacity; });
        }
        if(!_isActive) return;
        _data[_head++] = std::move(entry);
        _full.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lck{ _mtx };
        if(_head < 0) {
            _empty.wait([this]{ return _head > 0; });
        }
        if(!_isActive) throw invalid_state{};
        return _data[--_head];
    }

    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lck{ _mtx, std::defer_lock };
        if(lck.try_lock() && _head > 0 && _isActive) {
            return _data[--_head];
        }else {
            return {};
        }
    }

    void shutDown() {
        std::unique_lock<std::mutex> lck{ _mtx };
        _isActive = false;
        _full.notify_one();
        _empty.notify_all();
    }

    bool empty() const {
        std::unique_lock<std::mutex> lck{ _mtx };
        return _head == 0;
    };

private:
    std::vector<T> _data;
    size_t _capacity{};
    int _head{};
    mutable std::mutex _mtx;
    std::condition_variable _full;
    std::condition_variable _empty;
    bool _isActive{true};

};