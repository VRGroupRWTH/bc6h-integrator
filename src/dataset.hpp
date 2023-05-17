#pragma once

#include "data_source.hpp"
#include <liblava/base/device.hpp>
#include <memory>
#include <optional>
#include <thread>
#include <vulkan/vulkan_core.h>
#include <sync/rwlock.hpp>

struct Dataset {
    using Ptr = std::shared_ptr<Dataset>;

    Dataset(DataSource::Ptr data) : data(data), loading_state(LoadingState()) {}
    ~Dataset() { destroy(); }

    static Ptr create(lava::device_p device, DataSource::Ptr data);
    void destroy();

    DataSource::Ptr data;
    struct Image {
        Image() = default;
        Image(const Image&) = delete;
        Image(Image&& other) : device(other.device), image(other.image), allocation(other.allocation) {
            other.device = nullptr;
            other.image = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
        }
        Image& operator=(const Image&) = delete;
        ~Image();

        bool create(lava::device_p device, const DataSource::Ptr& data, VkSampler sampler);

        lava::device_p device = nullptr;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkDescriptorImageInfo image_info;
    };
    std::vector<Image> images;
    std::thread loading_thread;
    Image& get_image(unsigned channel, unsigned t) {
        return this->images[channel * this->data->dimensions.w + t];
    }

    lava::device_p device = nullptr;
    VkSampler sampler = VK_NULL_HANDLE;

    struct StagingBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocation_info;
        VmaAllocator allocator;

        ~StagingBuffer() {
            if (buffer && allocation && allocator) {
                vmaDestroyBuffer(this->allocator, this->buffer, this->allocation);
            }
        }
    };
    std::optional<StagingBuffer> staging;

    struct LoadingState {
        enum class Step {
            STARTING,
            STAGING_BUFFER_ALLOCATION,
            READ_DATA,
            TEXTURE_ALLOCATION,
            TRANSFER,
            FINISHED,
            ERROR,
        };
        Step step = Step::STARTING;
        int current_substep = 0;
        int substep_count = 1;

        void set_step(Step step, int substep_count = 1) {
            this->step = step;
            this->substep_count = substep_count;
            this->current_substep = 0;
        }
        void advance_substep() {
            this->current_substep += 1;
        }
    };
    cppsync::read_write_lock<LoadingState> loading_state;

    bool is_loading();
    bool loaded();
    void imgui();
    bool transfer_if_necessary(VkCommandBuffer command_buffer);

    static void load(lava::device_p device, Ptr dataset);
};
