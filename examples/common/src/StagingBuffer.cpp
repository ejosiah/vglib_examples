#include "gltf/StagingBuffer.hpp"

StagingBuffer::StagingBuffer(VulkanDevice *device, VkDeviceSize size)
: _buffer{ device->createStagingBuffer(size) }
, _offset{0}
{}

BufferRegion StagingBuffer::allocate(VkDeviceSize size, VkDeviceSize alignment) {
    if(size + _offset >= _buffer.size) {
        _offset = 0;
    }
    _offset += _offset % alignment;
    const auto start = _offset;
    const auto end = start + size;
    _offset += size;
    return _buffer.region(start, end);
}
