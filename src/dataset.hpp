#pragma once

#include "data_source.hpp"
#include <liblava/base/base.hpp>
#include <liblava/base/device.hpp>
#include <liblava/core/time.hpp>
#include <memory>
#include <optional>
#include <sync/rwlock.hpp>
#include <thread>
#include <vulkan/vulkan_core.h>

struct Dataset {
    using Ptr = std::shared_ptr<Dataset>;

    Dataset(DataSource::Ptr data) : data(std::move(data)), loading_state(LoadingState()) {}
    ~Dataset() { destroy(); }

    static Ptr make(lava::device_p device, DataSource::Ptr data) {
        auto dataset = std::make_shared<Dataset>(data);

        if (!dataset->create(device)) {
            return nullptr;
        }
        return dataset;
    }

    bool create(lava::device_p device);
    void destroy();

    DataSource::Ptr data;
    struct Image {
        Image() = default;
        Image(const Image&) = delete;
        Image(Image&& other) : device(other.device), image(other.image), view(other.view), allocation(other.allocation), image_info(other.image_info) {
            other.device = nullptr;
            other.image = VK_NULL_HANDLE;
            other.view = VK_NULL_HANDLE;
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
    Image& get_image(unsigned channel, unsigned t) {
        return this->images[channel * this->data->dimensions.w + t];
    }

    lava::device_p device = nullptr;
    VkSampler sampler = VK_NULL_HANDLE;

    struct LoadingState {
        enum class Step {
            STARTING,
            LOAD_SLICE,
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
    std::thread loading_thread;
    std::atomic<lava::ms> loading_time;
    void load(std::size_t staging_buffer_count);

    bool transitioned = false;
    void transition_images(VkCommandBuffer command_buffer);

    bool is_loading();
    bool loaded();
    void imgui();
};
