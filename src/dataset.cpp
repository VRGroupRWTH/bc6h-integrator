#include "dataset.hpp"
#include <imgui.h>
#include <liblava/base/physical_device.hpp>
#include <liblava/core/time.hpp>
#include <liblava/resource/buffer.hpp>
#include <liblava/util/log.hpp>
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

    Image time_slice;
    const VkImageCreateInfo image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_3D,
        .format = format,
        .extent = VkExtent3D{
            .width = data->dimensions.x,
            .height = data->dimensions.y,
            .depth = data->dimensions.z,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocation_create_info{
        .flags = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VmaAllocationInfo allocation_info;

    if (vmaCreateImage(device->get_allocator()->get(), &image_create_info, &allocation_create_info, &this->image, &this->allocation, &allocation_info) != VK_SUCCESS) {
        lava::log()->error("failed to create image for dataset");
        return false;
    }
    // lava::log()->info("allocated memory for slice t={}: {} bytes", t, allocation_info.size);
    // this->allocator = device->get_allocator()->get();

    const VkImageViewCreateInfo image_view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = this->image,
        .viewType = VK_IMAGE_VIEW_TYPE_3D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    device->vkCreateImageView(&image_view_info, &this->view);

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

Dataset::Ptr Dataset::create(lava::device_p device, DataSource::Ptr data) {
    if (data == nullptr) {
        lava::log()->error("failed to create dataset: invalid data source");
        return nullptr;
    }

    auto dataset = std::make_shared<Dataset>(data);
    dataset->device = device;
    dataset->loading_thread = std::thread(Dataset::load, device, dataset);
    return dataset;
}

void Dataset::destroy() {
    if (this->sampler != VK_NULL_HANDLE) {
        assert(this->device);
        this->device->vkDestroySampler(this->sampler);
        this->sampler = VK_NULL_HANDLE;
    }
    this->loading_thread.join();
}

bool Dataset::is_loading() {
    switch (this->loading_state.read()->step) {
        case LoadingState::Step::FINISHED:
        case LoadingState::Step::ERROR:
            return true;

        default:
            return false;
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

            case LoadingState::Step::STAGING_BUFFER_ALLOCATION:
                ImGui::Text("Allocating staging buffer");
                break;

            case LoadingState::Step::READ_DATA:
                ImGui::Text("Read data to staging buffer");
                break;

            case LoadingState::Step::TEXTURE_ALLOCATION:
                ImGui::Text("Allocating textures");
                break;

            case LoadingState::Step::TRANSFER:
                ImGui::Text("Transfer staging buffer to textures");
                break;
        }
    }
}

void Dataset::load(lava::device_p device, Ptr dataset) {
    auto data = dataset->data;

    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    device->vkCreateSampler(&sampler_info, &dataset->sampler);

    dataset->staging.emplace(StagingBuffer{.allocator = device->get_allocator()->get()});
    const VkBufferCreateInfo staging_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .flags = 0,
        .size = static_cast<VkDeviceSize>(data->data_size),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo staging_buffer_allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    dataset->loading_state.write()->set_step(LoadingState::Step::STAGING_BUFFER_ALLOCATION);
    lava::timer sw;
    if (vmaCreateBufferWithAlignment(
            dataset->staging->allocator,
            &staging_buffer_create_info,
            &staging_buffer_allocation_create_info,
            1,
            &dataset->staging->buffer,
            &dataset->staging->allocation,
            &dataset->staging->allocation_info
        ) != VK_SUCCESS) {
        lava::log()->error("failed to create staging buffer for data upload");
        dataset->loading_state.write()->set_step(LoadingState::Step::ERROR);
        return;
    }
    lava::log()->info("allocate staging buffer for data upload ({} ms)", sw.elapsed().count());

    dataset->loading_state.write()->set_step(LoadingState::Step::READ_DATA);
    sw.reset();
    data->read(dataset->staging->allocation_info.pMappedData); // TODO: add chunk size
    lava::log()->info("read data to staging buffer ({} ms)", sw.elapsed().count());

    dataset->loading_state.write()->set_step(LoadingState::Step::TEXTURE_ALLOCATION, data->dimensions.w);
    sw.reset();
    std::vector<Image> images(data->dimensions.w * data->channel_count);
    for (auto& image : images) {
        if (!image.create(device, data, dataset->sampler)) {
            lava::log()->error("failed to allocate image");
            dataset->loading_state.write()->set_step(LoadingState::Step::ERROR);
            return;
        }
        dataset->loading_state.write()->advance_substep();
    }
    lava::log()->info("allocated images ({} ms)", sw.elapsed().count());
    dataset->images = std::move(images);

    dataset->loading_state.write()->set_step(LoadingState::Step::TRANSFER);
}

bool Dataset::transfer_if_necessary(VkCommandBuffer command_buffer) {
    auto loading_state = this->loading_state.write();
    if (loading_state->step != LoadingState::Step::TRANSFER) {
        return false;
    }

    for (int c = 0; c < data->channel_count; ++c) {
        for (int t = 0; t < data->dimensions.w; ++t) {
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
                    .image = this->get_image(c, t).image,
                    .subresourceRange = VkImageSubresourceRange{
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };
                vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            VkBufferImageCopy region = {
                .bufferOffset = static_cast<VkDeviceSize>(data->channel_size * c + data->time_slice_size * t),
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageOffset = VkOffset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                .imageExtent = VkExtent3D{
                    .width = data->dimensions.x,
                    .height = data->dimensions.y,
                    .depth = data->dimensions.z,
                },
            };

            vkCmdCopyBufferToImage(command_buffer, this->staging->buffer, this->get_image(c, t).image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // Memory barrier (VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) -> (VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                VkImageMemoryBarrier barrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = this->get_image(c, t).image,
                    .subresourceRange = VkImageSubresourceRange{
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };
                vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }
    }

    loading_state->set_step(LoadingState::Step::FINISHED);
    lava::log()->info("Data transfered");
    return true;
}
