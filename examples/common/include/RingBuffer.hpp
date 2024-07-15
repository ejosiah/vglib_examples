#pragma once

#include <utility>
#include <cassert>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <span>

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
        const auto writeIndex = _writeIndex.load();
        const auto readIndex = _readIndex.load();

        return writeIndex < readIndex;
    }

    [[nodiscard]]
    bool full() const {
        const auto writeIndex = _writeIndex.load();
        const auto readIndex = _readIndex.load();

        auto size = (writeIndex - readIndex) + 1;
        return size == _capacity;
    }

    size_t size() const {
        const auto writeIndex = _writeIndex.load();
        const auto readIndex = _readIndex.load();
        return (writeIndex - readIndex) + 1;
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


template<typename T>
struct SingleWriterManyReadersQueue {
public:
    using Queue = std::shared_ptr<RingBuffer<T>>;
    SingleWriterManyReadersQueue() = default;

    SingleWriterManyReadersQueue(size_t readerCount, size_t capacity)
    {

        for(auto i = 0; i < readerCount; ++i) {
            auto queue = std::make_shared<RingBuffer<T>>();
            queue->reset(capacity);
            _readBuffers.push_back(std::move(queue));
        }
    }

    void push(T entry) {
        _readBuffers[_writeIndex]->push(entry);
        _writeIndex = (_writeIndex + 1) % _readBuffers.size();
    }

    void broadcast(T entry) {
        for(auto& buffer : _readBuffers) {
            buffer->push(entry);
        }
    }

    auto buffer(int index) {
        assert(index < _readBuffers.size());
        return _readBuffers[index];
    }

private:
    std::vector<Queue> _readBuffers;
    uint32_t _writeIndex{};
};

template<typename T>
struct ManyWritersSingleReaderQueue {
public:
    ManyWritersSingleReaderQueue() = default;

    ManyWritersSingleReaderQueue(size_t capacity)
    : _buffer(capacity)
    {}

    void push(T&& entry) {
        std::lock_guard<std::mutex> lk{ _mutex };
        _buffer.push(entry);
    }

    void push(std::span<T> entries) {
        std::lock_guard<std::mutex> lk{ _mutex };
        for(auto& entry : entries) {
            _buffer.push(entry);
        }
    }

    [[nodiscard]]
    bool empty() const {
        return _buffer.empty();
    }

    T pop() {
        return _buffer.pop();
    }

private:
    RingBuffer<T> _buffer;
    std::mutex _mutex;
};