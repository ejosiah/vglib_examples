#pragma once

#include "common.h"
#include <VulkanBuffer.h>
#include <VulkanImage.h>
#include <VulkanDevice.h>
#include "optix_common.hpp"

namespace cuda {


    struct Buffer {
        VulkanBuffer buf;
        void* cudaPtr{};
#ifdef WIN32
        HANDLE handle{};
#else
        int handle{-1};
#endif

        Buffer() = default;

        Buffer(const VulkanDevice& device, VulkanBuffer buffer): buf{buffer}{
            const auto allocation = buf.allocationInfo();
            handle = buf.getHandle(device);
            cudaExternalMemoryHandleDesc externalMemoryHandleDesc{};
            externalMemoryHandleDesc.size = allocation.size + allocation.offset;
#ifdef WIN32
            externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
            externalMemoryHandleDesc.handle.win32.handle = handle;
#else
            externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueFd;
            cudaExternalMemoryHandleTypeOpaqueFd.handle.fd = handle;
#endif
            cudaExternalMemory_t externalMemoryBuffer{};
            CUDA_CHECK(cudaImportExternalMemory(&externalMemoryBuffer, &externalMemoryHandleDesc));

#ifndef WIN32
            // fd got consumed
            externalMemoryHandleDesc.handle.fd = -1;
#endif
            cudaExternalMemoryBufferDesc cudaExternalMemoryBufferDesc{};
            cudaExternalMemoryBufferDesc.offset = allocation.offset;
            cudaExternalMemoryBufferDesc.size = allocation.size;
            cudaExternalMemoryBufferDesc.flags = 0;
            CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(&cudaPtr, externalMemoryBuffer, &cudaExternalMemoryBufferDesc));
        }

        OptixImage2D toOptixImage2D(uint32_t width, uint32_t height) const {
            OptixImage2D oi{};
            oi.data = (CUdeviceptr)cudaPtr;
            oi.width              = width;
            oi.height             = height;
            oi.rowStrideInBytes   = width*sizeof(float4);
            oi.pixelStrideInBytes = 0;
            oi.format             = OPTIX_PIXEL_FORMAT_FLOAT4;
            return oi;
        }

        operator bool() const {
            return static_cast<bool>(buf);
        }
    };

    struct Semaphore{
        VulkanSemaphore vk;
        cudaExternalSemaphore_t cu{};
#ifdef WIN32
        HANDLE handle{};
#else
        int handle{-1};
#endif
        Semaphore() = default;

        Semaphore(const VulkanDevice& device)
        {
            vk = device.createTimelineSemaphore();
            device.setName<VK_OBJECT_TYPE_SEMAPHORE>("denoiser", vk.semaphore);
            handle = vk.getHandle();
            cudaExternalSemaphoreHandleDesc externalSemaphoreHandleDesc{};
            externalSemaphoreHandleDesc.flags = 0;
#ifdef WIN32
            externalSemaphoreHandleDesc.type  = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
            externalSemaphoreHandleDesc.handle.win32.handle = (void*)handle;
#else
            externalSemaphoreHandleDesc.type  = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
  externalSemaphoreHandleDesc.handle.fd = m_semaphore.handle;
#endif

            CUDA_CHECK(cudaImportExternalSemaphore(&cu, &externalSemaphoreHandleDesc));
        }

        DISABLE_COPY(Semaphore)

        Semaphore& operator=(Semaphore&& source) noexcept {
            if(this == &source) return *this;

            if(vk){
                this->~Semaphore();
            }

            this->vk = std::move(source.vk);
            this->cu = std::exchange(source.cu, nullptr);

#if WIN32
            this->handle = std::exchange(source.handle, nullptr);
#else
            this->handle = std::exchange(source.handle, 0);
#endif
            return *this;
        }

        ~Semaphore(){
#ifdef WIN32
            CloseHandle(handle);
#else
            if(handle != -1){
                close(handle);
                handle = -1;
            }
#endif
//            dispose(*vk);
        }
    };
}