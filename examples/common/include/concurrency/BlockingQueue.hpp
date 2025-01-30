#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <span>

template<typename T>
class BlockingQueue {
public:
    struct invalid_state : std::exception {};

    BlockingQueue() = default;

    explicit BlockingQueue(size_t capacity)
    : _capacity{capacity}
    {}

     void push(const T& entry) {
        std::unique_lock<std::mutex> lck{ _mtx };
        if(_data.size() == _capacity) {
            _full.wait(lck, [this]{ return _data.size() < _capacity; });
        }
        if(!_isActive) return;
        _data.push(entry);
         lck.unlock();

        _empty.notify_one();
    }

    void push(T&& entry) {
        std::unique_lock<std::mutex> lck{ _mtx };
        if(_data.size() == _capacity) {
            _full.wait(lck, [this]{ return _data.size() < _capacity; });
        }
        if(!_isActive) return;
        _data.push(std::move(entry));
        lck.unlock();

        _empty.notify_one();
    }

    void push(std::span<T> entries) {
        std::unique_lock<std::mutex> lck{ _mtx };
        auto count = std::min(entries.size(), availableSlots());
        auto itr = entries.begin();
        do {
            for(auto i = 0; i < count; ++i) {
                _data.push(*itr);
                ++itr;
            }
            if(itr != entries.end()) {
                // FIXME we are going to get stuck here if entries.size() > _capacity
                _full.wait(lck, [&]{ return availableSlots() <= std::distance(itr, entries.end()); });
            }
        } while (itr != entries.end());
        lck.unlock();

        _empty.notify_one();

    }

    T pop() {
        std::unique_lock<std::mutex> lck{ _mtx };
        if(_data.empty()) {
            _empty.wait([this]{ return !_data.empty(); });
        }
        if(!_isActive) throw invalid_state{};
        auto result =  std::move(_data.front());
        _data.pop();
        lck.unlock();

        _full.notify_one();
        return result;
    }

    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lck{ _mtx, std::defer_lock };
        if(lck.try_lock() && !_data.empty() && _isActive) {
            auto result =  std::move(_data.front());
            _data.pop();
            lck.unlock();

            _full.notify_one();
            return result;
        }else {
            return {};
        }
    }

    auto availableSlots() const {
        return _capacity - _data.size();
    }

    void shutDown() {
        std::unique_lock<std::mutex> lck{ _mtx };
        _isActive = false;
        _full.notify_one();
        _empty.notify_all();
    }

    bool empty() const {
        std::unique_lock<std::mutex> lck{ _mtx };
        return _data.empty();
    };

private:
    std::queue<T> _data;
    size_t _capacity{};
    mutable std::mutex _mtx;
    std::condition_variable _full;
    std::condition_variable _empty;
    bool _isActive{true};

};