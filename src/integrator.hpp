#pragma once

#include <vulkan/vulkan_core.h>

class Integrator {
    struct SliceStagingBuffer {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void* data;
    };

    struct SliceImage {
        VkImage image;
        VkDeviceMemory memory;
    };
};
