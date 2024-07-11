#pragma once

#include <atomic>
#include <stack>
#include <set>
#include <mutex>
#include <memory>

template<typename T>
class ObjectPool {
public:
    ObjectPool() = default;

    ObjectPool(size_t capacity)
    : _capacity{capacity}
    {
        for(auto i = 0; i < capacity; ++i){
            _storage.push(std::make_shared<T>(T{}));
        }
    }


    std::shared_ptr<T> acquire() {
        if(_storage.empty()) {
            return {};
        }
        auto object = _storage.top();
        _acquired.insert(object);
        _storage.pop();
        return object;
    }

    std::reference_wrapper<T> acquireReference() {
        if(_storage.empty()) {
            throw std::runtime_error{"No resources available"};
        }
        auto object = _storage.top();
        _acquired.insert(object);
        return std::reference_wrapper<T>(*object);
    }

    void release(std::shared_ptr<T> object) {
        if(_storage.size() == _capacity){
            return;
        }
        _acquired.remove(object);
        _storage.push(object);
    }

    void releaseAll() {
        for(auto object : _acquired) {
            _storage.push(object);
        }
        _acquired.clear();
    }

private:
    std::stack<std::shared_ptr<T>> _storage;
    std::set<std::shared_ptr<T>> _acquired;
    size_t _capacity{};
};