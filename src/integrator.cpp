#include "integrator.hpp"
#include "shaders.hpp"
#include <array>
#include <imgui.h>
#include <liblava/app.hpp>
#include <vulkan/vulkan_core.h>
#include <glm/gtx/string_cast.hpp>

constexpr unsigned IMAGE_COUNT = 200;

struct Constants {
    glm::uvec4 dataset_dimensions;
    glm::uvec3 seed_dimensions;
    glm::uint step_count;
};

bool Integrator::create(lava::app& app) {
    const auto queues = app.device->compute_queues();
    const auto compute_queue = std::find_if(queues.begin(), queues.end(), [](auto q) { return q.priority == 1.0; });
    if (compute_queue == queues.end()) {
        lava::log()->error("failed to find appropriate compute queue");
        return false;
    }
    this->compute_queue = *compute_queue;

    this->device = app.device;

    // const VkBufferCreateInfo staging_buffer_create_info{
    //     .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    //     .flags = 0,
    //     .size = static_cast<VkDeviceSize>(4),
    //     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    //     .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    // };
    // const VmaAllocationCreateInfo staging_buffer_allocation_create_info{
    //     .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
    //     .usage = VMA_MEMORY_USAGE_AUTO,
    // };
    // vmacreate

    this->progress_buffer = lava::buffer::make();
    this->progress_buffer->create_mapped(this->device, nullptr, 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    if (this->progress_buffer->get_mapped_data() == nullptr) {
        lava::log()->error("failed to map data of progress buffer");
        return false;
    }
    memset(this->progress_buffer->get_mapped_data(), 0, 4);

    this->descriptor = lava::descriptor::make();
    lava::descriptor::binding::ptr dataset_binding = lava::descriptor::binding::make(0);
    dataset_binding->set_type(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    dataset_binding->set_stage_flags(VK_SHADER_STAGE_COMPUTE_BIT);
    dataset_binding->set_count(IMAGE_COUNT);
    this->descriptor->add(dataset_binding);
    this->descriptor->add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    this->descriptor->add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    this->descriptor->add_binding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    if (!descriptor->create(device)) {
        return false;
    }

    this->descriptor_pool = lava::descriptor::pool::make();
    if (!descriptor_pool->create(device, {
                                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMAGE_COUNT},
                                             {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                                         },
                                 1)) {
        return false;
    }

    this->spawn_seeds_pipeline_layout = lava::pipeline_layout::make();
    this->spawn_seeds_pipeline_layout->add(this->descriptor);
    this->spawn_seeds_pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants)});
    if (!spawn_seeds_pipeline_layout->create(device)) {
        destroy();
        return false;
    }

    this->spawn_seeds_pipeline = lava::compute_pipeline::make(app.device, app.pipeline_cache);
    this->spawn_seeds_pipeline->set_layout(this->spawn_seeds_pipeline_layout);
    if (!this->spawn_seeds_pipeline->set_shader_stage(spawn_seeds_comp_cdata, VK_SHADER_STAGE_COMPUTE_BIT)) {
        lava::log()->error("failed to set shader for seed spawning pipeline");
        return false;
    }
    if (!this->spawn_seeds_pipeline->create()) {
        lava::log()->error("failed to create seed spawning pipeline");
        return false;
    }

    this->render_pipeline_layout = lava::pipeline_layout::make();
    this->render_pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)});
    if (!render_pipeline_layout->create(device)) {
        destroy();
        return false;
    }

    auto color_attachment = lava::attachment::make(app.target->get_format());
    // color_attachment->set_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR);
    // color_attachment->set_final_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    this->render_pipeline = lava::render_pipeline::make(device, app.pipeline_cache);
    this->render_pipeline->set_rasterization_polygon_mode(VK_POLYGON_MODE_LINE);
    // this->render_pipeline->set_sizing();
    this->render_pipeline->set_input_topology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
    this->render_pipeline->set_layout(this->render_pipeline_layout);
    this->render_pipeline->add_color_blend_attachment();
    this->render_pipeline->set_vertex_input_binding({0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    this->render_pipeline->set_vertex_input_attributes({
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
    });

    if (!this->render_pipeline->add_shader(lines_vert_cdata, VK_SHADER_STAGE_VERTEX_BIT)) {
        lava::log()->error("cannot add vertex shader for line renderer");
        destroy();
        return false;
    }
    if (!this->render_pipeline->add_shader(lines_frag_cdata, VK_SHADER_STAGE_FRAGMENT_BIT)) {
        lava::log()->error("cannot add fragment shader for line renderer");
        destroy();
        return false;
    }
    this->render_pipeline->create(app.shading.get_vk_pass());
    app.shading.get_pass()->add_front(this->render_pipeline);
    // this->render_pass = app.shading.get_pass();
    // this->render_pass->add_front(this->pipeline);

    this->render_pipeline->on_process = [this](VkCommandBuffer command_buffer) { this->render(command_buffer); };

    this->app = &app;

    return true;
}

void Integrator::destroy() {
}

void Integrator::render(VkCommandBuffer command_buffer) {
    if (!this->integration.has_value()) {
        return;
    }

    this->render_pipeline->bind(command_buffer);
    const VkDeviceSize buffer_offsets = 0;
    const glm::mat4 view_projection = this->app->camera.get_view_projection();
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(view_projection), &view_projection);
    this->device->call().vkCmdBindVertexBuffers(command_buffer, 0, 1, &this->integration->line_buffer, &buffer_offsets);
    this->device->call().vkCmdDrawIndirect(command_buffer, this->integration->indirect_buffer, 0, this->integration->seed_count, sizeof(VkDrawIndirectCommand));
}

void Integrator::prepare_for_dataset(Dataset::Ptr dataset) {
    this->reset_dataset();
    this->dataset = dataset;
}

void Integrator::imgui() {
    ImGui::DragInt3("Seed Dimensions", reinterpret_cast<int*>(glm::value_ptr(this->seed_spawn)));
    ImGui::DragInt("Steps", reinterpret_cast<int*>(&this->integration_steps));

    ImGui::BeginDisabled(!this->dataset || this->integration_in_progress());
    if (ImGui::Button("Integrate")) {
        this->integrate();
    }
    ImGui::EndDisabled();
    if (this->integration_in_progress()) {
        auto progress = reinterpret_cast<const std::uint32_t*>(this->progress_buffer->get_mapped_data());
        // lava::log()->info("{}", *progress);
        ImGui::ProgressBar(static_cast<float>(*progress) / (this->integration->seed_count * this->integration->integration_steps));
    }
}

void Integrator::reset_dataset() {
}

bool Integrator::integrate() {
    this->integration.reset();

    this->integration.emplace();

    const std::uint32_t seed_count = this->seed_spawn.x * this->seed_spawn.y * this->seed_spawn.z;
    this->integration->seed_count = seed_count;

    const std::size_t buffer_size = seed_count * this->integration_steps * sizeof(glm::vec3);

    const std::array<std::uint32_t, 2> queue_family_indices = {
        this->device->get_graphics_queue().family,
        this->compute_queue.family,
    };

    const VkBufferCreateInfo line_buffer_create_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = queue_family_indices.size(),
        .pQueueFamilyIndices = queue_family_indices.data(),
    };
    const VmaAllocationCreateInfo line_buffer_alloc_info {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    if (vmaCreateBuffer(
            device->alloc(),
            &line_buffer_create_info,
            &line_buffer_alloc_info,
            &this->integration->line_buffer,
            &this->integration->line_buffer_allocation,
            nullptr
        ) != VK_SUCCESS) {
        lava::log()->error("failed to create line buffer");
        return false;
    }

    const VkBufferCreateInfo indirect_buffer_create_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(VkDrawIndirectCommand) * seed_count,
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = queue_family_indices.size(),
        .pQueueFamilyIndices = queue_family_indices.data(),
    };
    const VmaAllocationCreateInfo indirect_buffer_alloc_info {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    if (vmaCreateBuffer(
            device->alloc(),
            &indirect_buffer_create_info,
            &indirect_buffer_alloc_info,
            &this->integration->indirect_buffer,
            &this->integration->indirect_buffer_allocation,
            nullptr
        ) != VK_SUCCESS) {
        lava::log()->error("failed to create indirect buffer");
        return false;
    }
    // this->integration->line_buffer = lava::buffer::make();
    // this->integration->line_buffer->create(this->device, nullptr, buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    this->integration->descriptor_set = this->descriptor->allocate(this->descriptor_pool->get());

    const VkDescriptorBufferInfo line_buffer_info{
        .buffer = this->integration->line_buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    const VkDescriptorBufferInfo progress_buffer_info{
        .buffer = this->progress_buffer->get(),
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    const VkDescriptorBufferInfo indirect_buffer_info{
        .buffer = this->integration->indirect_buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    std::vector<VkWriteDescriptorSet> descriptor_writes = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = this->integration->descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &line_buffer_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = this->integration->descriptor_set,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &progress_buffer_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = this->integration->descriptor_set,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &indirect_buffer_info,
        },
    };
    for (std::uint32_t i = 0; i < IMAGE_COUNT; ++i) {
        descriptor_writes.push_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = this->integration->descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &this->dataset->time_slices[i % this->dataset->time_slices.size()].image_info
        });
    }
    vkUpdateDescriptorSets(this->descriptor->get_device()->get(), descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);

    if (this->device->vkCreateCommandPool(this->compute_queue.family, &this->integration->command_pool).value != VK_SUCCESS) {
        lava::log()->error("failed to create command pool");
        return false;
    }

    VkCommandBuffer command_buffer;
    if (!this->device->vkAllocateCommandBuffers(this->integration->command_pool, 1, &command_buffer)) {
        lava::log()->error("failed to create command buffer for integration");
        return false;
    }

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        lava::log()->error("failed to begin command buffer");
        return false;
    }

    this->spawn_seeds_pipeline->bind(command_buffer);

    this->spawn_seeds_pipeline_layout->bind(command_buffer, this->integration->descriptor_set, 0, {}, VK_PIPELINE_BIND_POINT_COMPUTE);
    Constants c{
        .dataset_dimensions = this->dataset->data->dimensions,
        .seed_dimensions = this->seed_spawn,
        .step_count = this->integration_steps,
    };
    device->call().vkCmdPushConstants(command_buffer, this->spawn_seeds_pipeline_layout->get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &c);
    device->call().vkCmdDispatch(command_buffer, this->seed_spawn.x, this->seed_spawn.y, this->seed_spawn.z);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        lava::log()->error("failed to end command buffer");
        return false;
    }

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    if (!device->vkQueueSubmit(this->compute_queue.vk_queue, 1, &submit_info, nullptr)) {
        lava::log()->error("failed to submit command buffer");
        return false;
    }

    return true;
}
