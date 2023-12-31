#include "dataset.hpp"
#include "queues.hpp"
#include <imgui.h>
#include <liblava/base/physical_device.hpp>
#include <liblava/core/time.hpp>
#include <liblava/resource/buffer.hpp>
#include <liblava/util/log.hpp>
#include <spdlog/fmt/ostr.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

VkFormat get_vulkan_format(DataSource::Format format) {
    switch (format) {
        case DataSource::Format::Float16:
            return VK_FORMAT_R16_SFLOAT;
        case DataSource::Format::Float32:
            return VK_FORMAT_R32_SFLOAT;
        case DataSource::Format::BC6H:
            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    }
    assert(false && "Invalid format");
}

bool Dataset::Image::create(lava::device_p device, const DataSource::Ptr& data, VkSampler sampler) {
    assert(this->image == VK_NULL_HANDLE);
    assert(this->view == VK_NULL_HANDLE);
    assert(this->allocation == VK_NULL_HANDLE);
    assert(this->device == nullptr);

    //     VkImageFormatProperties props;
    //     if (vkGetPhysicalDeviceImageFormatProperties(
    //             device->get_physical_device()->get(),
    //             VK_FORMAT_R32_SFLOAT,
    //             VK_IMAGE_TYPE_3D,
    //             VK_IMAGE_TILING_OPTIMAL,
    //             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    //             0,
    //             &props
    //         ) != VK_SUCCESS) {
    //         return false;
    //     }

    this->device = device;
    const VkFormat format = get_vulkan_format(data->format);

    const auto& queues = this->device->get_queues();
    std::vector<std::uint32_t> family_indices = {
        queues[queue_indices::GRAPHICS].family,
        queues[queue_indices::COMPUTE].family,
        queues[queue_indices::TRANSFER].family,
    };
    std::sort(family_indices.begin(), family_indices.end());
    family_indices.erase(std::unique(family_indices.begin(), family_indices.end()), family_indices.end());

    Image time_slice;
    const VkImageCreateInfo image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = 0,
        .imageType = data->format == DataSource::Format::BC6H ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D,
        .format = format,
        .extent = VkExtent3D{
            .width = data->dimensions.x,
            .height = data->dimensions.y,
            .depth = data->format == DataSource::Format::BC6H ? 1 : data->dimensions.z,
        },
        .mipLevels = 1,
        .arrayLayers = data->format == DataSource::Format::BC6H ? data->dimensions.z : 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = static_cast<uint32_t>(family_indices.size()),
        .pQueueFamilyIndices = family_indices.data(),
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocation_create_info{
        .flags = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VmaAllocationInfo allocation_info;

    if (vmaCreateImage(device->get_allocator()->get(), &image_create_info, &allocation_create_info, &this->image, &this->allocation, &allocation_info) != VK_SUCCESS) {
        lava::log()->error("failed to create image for dataset");
        return false;
    }
    lava::log()->debug("slice memory type: {}", allocation_info.memoryType);
    // lava::log()->info("allocated memory for slice t={}: {} bytes", t, allocation_info.size);
    // this->allocator = device->get_allocator()->get();

    const VkImageViewCreateInfo image_view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = this->image,
        .viewType = data->format == DataSource::Format::BC6H ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_3D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = data->format == DataSource::Format::BC6H ? data->dimensions.z : 1,
        },
    };

    if (device->vkCreateImageView(&image_view_info, &this->view).value != VK_SUCCESS) {
        lava::log()->error("failed to create image view for dataset");
        return false;
    }

    this->image_info.sampler = sampler;
    this->image_info.imageView = this->view;
    this->image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return true;
}

Dataset::Image::~Image() {
    if (this->device && this->image && this->allocation && this->view) {
        this->device->vkDestroyImageView(this->view);
        vmaDestroyImage(device->get_allocator()->get(), this->image, this->allocation);
    }
}

bool Dataset::create(lava::device_p device) {
    this->device = device;
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (device->vkCreateSampler(&sampler_info, &this->sampler).value != VK_SUCCESS) {
        lava::log()->error("failed to create sampler");
        this->loading_state.write()->set_step(LoadingState::Step::ERROR);
        return false;
    }

    // this->staging.resize(2);
    // for (auto& staging_buffer : this->staging) {
    //     if (!staging_buffer.create(device, data)) {
    //         return false;
    //     }
    // }
    this->images.reserve(data->dimensions.w * data->channel_count);
    this->loading_thread = std::thread(&Dataset::load, this, 1);
    // this->staging.emplace(StagingBuffer{.allocator = device->get_allocator()->get()});

    // dataset->loading_state.write()->set_step(LoadingState::Step::READ_DATA);
    // sw.reset();
    // lava::log()->info("read data to staging buffer ({} ms)", sw.elapsed().count());

    // dataset->loading_state.write()->set_step(LoadingState::Step::TEXTURE_ALLOCATION, data->dimensions.w);
    // sw.reset();
    // std::vector<Image> images(data->dimensions.w * data->channel_count);
    // for (auto& image : images) {
    //     dataset->loading_state.write()->advance_substep();
    // }
    // lava::log()->info("allocated images ({} ms)", sw.elapsed().count());
    // dataset->images = std::move(images);

    // dataset->loading_state.write()->set_step(LoadingState::Step::TRANSFER);

    return true;
}

void Dataset::destroy() {
    this->loading_thread.join();
    if (this->sampler != VK_NULL_HANDLE) {
        assert(this->device);
        this->device->vkDestroySampler(this->sampler);
        this->sampler = VK_NULL_HANDLE;
    }
}

bool Dataset::is_loading() {
    switch (this->loading_state.read()->step) {
        case LoadingState::Step::FINISHED:
        case LoadingState::Step::ERROR:
            return false;

        default:
            return true;
    }
}

bool Dataset::loaded() {
    return this->loading_state.read()->step == LoadingState::Step::FINISHED;
}

void Dataset::imgui() {
    this->data->imgui();
    auto loading_state = this->loading_state.read();
    if (loading_state->step == LoadingState::Step::FINISHED) {
    } else if (loading_state->step == LoadingState::Step::ERROR) {
        ImGui::Text("failed to load dataset, see log for details");
    } else {
        ImGui::Text("loading dataset");
        const float substep_progress = static_cast<float>(loading_state->current_substep) / loading_state->substep_count;
        const float overall_progress = (static_cast<float>(loading_state->step) + substep_progress) / static_cast<float>(LoadingState::Step::FINISHED);
        ImGui::ProgressBar(overall_progress);
        switch (loading_state->step) {
            case LoadingState::Step::STARTING:
                ImGui::Text("Starting");
                break;

            case LoadingState::Step::LOAD_SLICE:
                ImGui::Text("Loading slices");
                break;

            case LoadingState::Step::FINISHED:
                ImGui::Text("Finished");
                break;

            case LoadingState::Step::ERROR:
                ImGui::Text("Error");
                break;
        }
    }

    ImGui::Text("%f s", this->loading_time.load().count() / 1000.0);
}

void Dataset::load(std::size_t staging_buffer_count) {
    struct StagingBuffer {
        lava::device_p device;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocation_info;
        VmaAllocator allocator = VK_NULL_HANDLE;

        bool create(lava::device_p device, VkDeviceSize size) {
            this->device = device;
            this->allocator = device->alloc();

            const VkBufferCreateInfo staging_buffer_create_info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .flags = 0,
                .size = size,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            };
            const VmaAllocationCreateInfo staging_buffer_allocation_create_info{
                .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            };

            lava::timer sw;
            if (vmaCreateBufferWithAlignment(
                    this->allocator,
                    &staging_buffer_create_info,
                    &staging_buffer_allocation_create_info,
                    1,
                    &this->buffer,
                    &this->allocation,
                    &this->allocation_info) != VK_SUCCESS) {
                lava::log()->error("failed to create staging buffer");
                return false;
            }
            lava::log()->info("allocate staging buffer for data upload ({} ms)", sw.elapsed().count());

            return true;
        }

        ~StagingBuffer() {
            if (buffer && allocation && allocator) {
                vmaDestroyBuffer(this->allocator, this->buffer, this->allocation);
            }
        }
    };

    const auto absolute_dataset_path = std::filesystem::absolute(this->data->filename);
    const auto dataset_filename = absolute_dataset_path.filename().string();
    std::time_t t = std::time(0); // get time now
    std::tm* now = std::localtime(&t);

    std::array<std::string_view, 7> weekdays = {
        "Sunday",
        "Monday",
        "Tuesday",
        "Wednesday",
        "Thursday",
        "Friday",
        "Saturday",
    };
    const std::string filename = fmt::format("{}-{}-{}-{}-{}-{}-{}-{}-loading.csv", now->tm_year + 1900, now->tm_mon, now->tm_mday, weekdays[now->tm_wday], now->tm_hour, now->tm_min, now->tm_sec, dataset_filename);
    std::ofstream log_file(filename);
    fmt::print(log_file, "slice,file_read,texture_upload,dataset_path,dataset_dimensions\n");
    fmt::print(
        log_file, ",,,{},{}x{}x{}x{}\n",
        absolute_dataset_path,
        this->data->dimensions.x, this->data->dimensions.y, this->data->dimensions.z, this->data->dimensions.w);

    this->loading_state.write()->set_step(LoadingState::Step::STARTING);
    lava::timer loading_timer;
    this->loading_time.exchange(loading_timer.elapsed());

    std::vector<StagingBuffer> staging_buffers(staging_buffer_count);
    lava::VkCommandBuffers staging_command_buffers(staging_buffer_count);
    lava::VkFences staging_fences(staging_buffer_count);
    std::vector<std::optional<std::size_t>> slice_loaded_by_staging_buffer(staging_buffer_count);
    VkQueryPool query_pool;

    {
        VkQueryPoolCreateInfo query_pool_create{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = static_cast<uint32_t>(2 * staging_buffer_count),

        };
        if (this->device->call().vkCreateQueryPool(this->device->get(), &query_pool_create, nullptr, &query_pool) != VK_SUCCESS) {
            lava::log()->error("failed to create query pool");
            this->loading_state.write()->set_step(LoadingState::Step::ERROR);
            return;
        }
    }

    auto queue = this->device->queues()[queue_indices::TRANSFER];
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue.family,
    };

    for (auto& buffer : staging_buffers) {
        if (!buffer.create(this->device, this->data->time_slice_size)) {
            lava::log()->error("failed to create staging buffer");
            this->loading_state.write()->set_step(LoadingState::Step::ERROR);
            return;
        }
    }

    VkCommandPool command_pool = VK_NULL_HANDLE;
    if (!this->device->vkCreateCommandPool(&pool_info, &command_pool)) {
        lava::log()->error("failed to create command pool for data staging");
        this->loading_state.write()->set_step(LoadingState::Step::ERROR);
        return;
    }

    if (!this->device->vkAllocateCommandBuffers(command_pool, staging_buffer_count, staging_command_buffers.data())) {
        lava::log()->error("failed to create command buffers for data staging");
        this->loading_state.write()->set_step(LoadingState::Step::ERROR);
        return;
    }

    for (auto& fence : staging_fences) {
        VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        if (!this->device->vkCreateFence(&fence_info, &fence)) {
            lava::log()->error("failed to create fence for data staging");
            this->loading_state.write()->set_step(LoadingState::Step::ERROR);
            return;
        }
    }

    this->loading_state.write()->set_step(LoadingState::Step::LOAD_SLICE, this->data->dimensions.w * this->data->channel_count);

    while (this->images.size() < this->data->dimensions.w * this->data->channel_count) {
        for (std::size_t i = 0; i < staging_buffer_count; ++i) {
            auto& buffer = staging_buffers[i];
            auto& fence = staging_fences[i];
            auto& command_buffer = staging_command_buffers[i];

            if (this->device->vkWaitForFences(1, &fence, true, 0).value != VK_SUCCESS) {
                continue;
            }

            if (slice_loaded_by_staging_buffer[i].has_value()) {
                std::array<std::uint64_t, 2> timestamps;

                if (vkGetQueryPoolResults(this->device->get(), query_pool, i * 2, 2, sizeof(timestamps), timestamps.data(), sizeof(timestamps[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) {
                    lava::log()->error("failed to receive gpu times!");
                    this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                    return;
                }

                const float timestamp_period = this->device->get_properties().limits.timestampPeriod;
                const double duration_ns = (timestamps[1] - timestamps[0]) * (double)timestamp_period;
                const double duration_ms = duration_ns / 1000.0 / 1000.0;
                fmt::print(log_file, "{},,{}\n", *slice_loaded_by_staging_buffer[i], duration_ms);
                slice_loaded_by_staging_buffer[i].reset();
            }

            const std::size_t image_index = this->images.size();
            const std::size_t channel_index = image_index / this->data->dimensions[3];
            const std::size_t time_slice_index = image_index % this->data->dimensions[3];
            lava::log()->debug("load slice {} of channel {}", time_slice_index, channel_index);

            slice_loaded_by_staging_buffer[i] = image_index;

            const auto t0 = std::chrono::steady_clock::now();
            this->data->read_time_slice(channel_index, time_slice_index, buffer.allocation_info.pMappedData);
            std::chrono::duration<double, std::milli> slice_read_time = std::chrono::steady_clock::now() - t0;

            lava::log()->info("data read ({} ms)", slice_read_time.count());
            fmt::print(log_file, "{},{}\n", image_index, slice_read_time.count());

            lava::timer sw;
            auto& image = this->images.emplace_back();
            if (!image.create(device, data, this->sampler)) {
                lava::log()->error("failed to allocate image");
                this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                return;
            }
            lava::log()->info("image allocation ({} ms)", sw.elapsed().count());

            VkCommandBufferBeginInfo begin_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
                lava::log()->error("failed to begin command buffer");
                this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                return;
            }
            if (this->device->vkResetFences(1, &fence).value != VK_SUCCESS) {
                lava::log()->error("failed to reset fence");
                this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                return;
            }

            vkCmdResetQueryPool(command_buffer, query_pool, i * 2, 2);

            // Memory barrier to -> (VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                VkImageMemoryBarrier barrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = image.image,
                    .subresourceRange = VkImageSubresourceRange{
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = data->format == DataSource::Format::BC6H ? data->dimensions.z : 1,
                    },
                };
                vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            VkBufferImageCopy region = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = data->format == DataSource::Format::BC6H ? data->dimensions.z : 1,
                },
                .imageOffset = VkOffset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                .imageExtent = VkExtent3D{
                    .width = data->dimensions.x,
                    .height = data->dimensions.y,
                    .depth = data->format == DataSource::Format::BC6H ? 1 : data->dimensions.z,
                },
            };

            vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, i * 2 + 0);
            vkCmdCopyBufferToImage(command_buffer, buffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, i * 2 + 1);

            if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
                lava::log()->error("failed to end command buffer");
                this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                return;
            }

            VkSubmitInfo submit_info{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &command_buffer};
            if (!this->device->vkQueueSubmit(queue.vk_queue, 1, &submit_info, fence)) {
                lava::log()->error("failed to submit command buffer");
                this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                return;
            }

            this->loading_state.write()->advance_substep();
            this->loading_time.exchange(loading_timer.elapsed());
            break;
        }

        {
            VkResult result;
            do {
                result = this->device->vkWaitForFences(staging_fences.size(), staging_fences.data(), false, 1000 * 1000).value;
            } while (result == VK_TIMEOUT);
            if (result != VK_SUCCESS) {
                lava::log()->error("failed to wait for staging fences");
                this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                return;
            }
        }
    }

    {
        VkResult result;
        do {
            result = this->device->vkWaitForFences(staging_fences.size(), staging_fences.data(), true, 1000 * 1000).value;
        } while (result == VK_TIMEOUT);
        if (result != VK_SUCCESS) {
            lava::log()->error("failed to wait for staging fences");
            this->loading_state.write()->set_step(LoadingState::Step::ERROR);
            return;
        }
    }

    for (std::size_t i = 0; i < staging_buffer_count; ++i) {
        if (slice_loaded_by_staging_buffer[i].has_value()) {
            std::array<std::uint64_t, 2> timestamps;

            if (vkGetQueryPoolResults(this->device->get(), query_pool, i * 2, 2, sizeof(timestamps), timestamps.data(), sizeof(timestamps[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) {
                lava::log()->error("failed to receive gpu times!");
                this->loading_state.write()->set_step(LoadingState::Step::ERROR);
                return;
            }

            const float timestamp_period = this->device->get_properties().limits.timestampPeriod;
            const double duration_ns = (timestamps[1] - timestamps[0]) * (double)timestamp_period;
            const double duration_ms = duration_ns / 1000.0 / 1000.0;
            fmt::print(log_file, "{},,{}\n", *slice_loaded_by_staging_buffer[i], duration_ms);
            slice_loaded_by_staging_buffer[i].reset();
        }
    }

    this->device->vkFreeCommandBuffers(command_pool, staging_command_buffers.size(), staging_command_buffers.data());
    this->device->vkDestroyCommandPool(command_pool);
    for (auto& fence : staging_fences) {
        this->device->vkDestroyFence(fence);
    }
    this->device->call().vkDestroyQueryPool(this->device->get(), query_pool, nullptr);

    this->loading_time.exchange(loading_timer.elapsed());
    lava::log()->info("load dataset ({} s)", this->loading_time.load().count() / 1000.0);
    this->loading_state.write()->set_step(LoadingState::Step::FINISHED);
}

void Dataset::transition_images(VkCommandBuffer command_buffer) {
    auto graphics_queue = this->device->queues()[queue_indices::GRAPHICS];
    auto transfer_queue = this->device->queues()[queue_indices::TRANSFER];

    for (auto& image : this->images) {
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.image,
            .subresourceRange = VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = data->format == DataSource::Format::BC6H ? data->dimensions.z : 1,
            },
        };
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    this->transitioned = true;
}
