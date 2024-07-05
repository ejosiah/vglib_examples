#pragma once

#include <utility>
#include <cassert>
#include <atomic>
#include <vector>

template<typename T>
class RingBuffer {
public:
    RingBuffer() = default;

    explicit RingBuffer(size_t capacity)
            : _data(capacity)
            , _capacity{capacity}
    {}

    constexpr void push(const T& entry) {
        ensureCapacity();
        _data[++_writeIndex % _capacity] = entry;
    }

    constexpr void push(T&& entry) {
        ensureCapacity();
        _data[++_writeIndex % _capacity] = std::move(entry);
    }

    T pop() {
        ensureCapacity();
        return std::move(_data[_readIndex++ % _capacity]);
    }

    [[nodiscard]]
    bool empty() const {
        return _writeIndex < _readIndex;
    }

    [[nodiscard]]
    bool full() const {
        auto size = (_writeIndex - _readIndex) + 1;
        return size == _capacity;
    }

    size_t size() const {
        return (_writeIndex - _readIndex) + 1;
    }

    void ensureCapacity() {
        assert(_capacity > 0);
    }

    void reset(size_t newCapacity) {
        _data.clear();
        _data.resize(newCapacity);
        _capacity = newCapacity;
        _writeIndex = -1;
        _readIndex = 0;
    }

private:
    std::vector<T> _data;
    size_t _capacity{0};
    std::atomic_int64_t _writeIndex{-1};
    std::atomic_int64_t _readIndex{0};
};