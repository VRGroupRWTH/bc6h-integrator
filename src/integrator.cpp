#include "integrator.hpp"
#include "queues.hpp"
#include "shaders.hpp"
#include "time_slices.hpp"
#include <array>
#include <cstddef>
#include <glm/gtx/string_cast.hpp>
#include <imgui.h>
#include <liblava/app.hpp>
#include <vulkan/vulkan_core.h>

struct Constants {
    glm::vec4 dataset_resolution;
    glm::vec4 dataset_dimensions;
    glm::uvec3 seed_dimensions;
    float dt;
    glm::uint total_step_count;
    glm::uint first_step;
    glm::uint step_count;
};

constexpr std::uint32_t LINE_BUFFER_BINDING = 0;
constexpr std::uint32_t PROGRESS_BUFFER_BINDING = 1;
constexpr std::uint32_t INDIRECT_BUFFER_BINDING = 2;
constexpr std::uint32_t DATASET_BINDING_BASE = 3;
constexpr std::uint32_t WORK_GROUP_SIZE_X_CONSTANT_ID = 0;
constexpr std::uint32_t WORK_GROUP_SIZE_Y_CONSTANT_ID = 1;
constexpr std::uint32_t WORK_GROUP_SIZE_Z_CONSTANT_ID = 2;
constexpr std::uint32_t TIME_STEPS_CONSTANT_ID = 3;

bool Integrator::create(lava::app& app) {
    const auto& queues = app.device->queues();
    this->compute_queue = queues[queue_indices::COMPUTE];
    this->device = app.device;
    this->app = &app;

    return this->create_command_pool() &&
           this->create_query_pool() &&
           this->create_progress_buffer();

    return true;
}

void Integrator::destroy() {
    this->destroy_integration();
    this->destroy_render_pipeline();
    this->destroy_integration_pipeline();
    this->destroy_descriptor();

    if (this->command_pool != VK_NULL_HANDLE) {
        this->device->vkDestroyCommandPool(this->command_pool);
        this->command_pool = VK_NULL_HANDLE;
    }
    if (this->query_pool) {
        this->device->call().vkDestroyQueryPool(this->device->get(), this->query_pool, nullptr);
        this->query_pool = VK_NULL_HANDLE;
    }
    if (this->progress_buffer) {
        this->progress_buffer->destroy();
        this->progress_buffer = nullptr;
    }
}

void Integrator::render(VkCommandBuffer command_buffer) {
    if (!this->integration.has_value()) {
        return;
    }

    this->render_pipeline->bind(command_buffer);
    const VkDeviceSize buffer_offsets = 0;
    const glm::vec3 offset = -this->dataset->data->dimensions_in_meters() * 0.5f;
    const glm::mat4 world = glm::translate(glm::mat4(), offset);
    const glm::mat4 view_projection = this->app->camera.get_view_projection();
    const glm::mat4 world_view_projection = view_projection;
    this->device->call().vkCmdSetLineWidth(command_buffer, this->line_width);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(world_view_projection), &world_view_projection);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 16 * 4, sizeof(this->line_color), glm::value_ptr(line_color));
    this->device->call().vkCmdBindVertexBuffers(command_buffer, 0, 1, &this->integration->line_buffer, &buffer_offsets);
    this->device->call().vkCmdDrawIndirect(command_buffer, this->integration->indirect_buffer, 0, this->integration->seed_count, sizeof(VkDrawIndirectCommand));
}

void Integrator::set_dataset(Dataset::Ptr dataset) {
    this->destroy_integration();
    this->destroy_integration_pipeline();
    this->destroy_descriptor();

    this->dataset = dataset;

    if (this->dataset) {
        this->create_descriptor();
    }
}

bool Integrator::integration_in_progress() {
    return this->integration.has_value() && !this->integration->complete;
    // this->device->vkWaitForFences(1, &this->integration->command_buffer_fence, true, 0).value == VK_TIMEOUT;
}

void Integrator::check_for_integration() {
    if (this->should_integrate) {
        this->should_integrate = false;
        if (this->prepare_integration()) {
            if (this->integration_thread.joinable()) {
                this->integration_thread.join();
            }
            this->integration_thread = std::thread(&Integrator::integrate, this);
        }
    }
}

void Integrator::imgui() {
    if (ImGui::DragInt3("Work Group Size", reinterpret_cast<int*>(glm::value_ptr(this->work_group_size)))) {
        this->recreate_integration_pipeline = true;
    }
    ImGui::DragInt3("Seed Dimensions", reinterpret_cast<int*>(glm::value_ptr(this->seed_spawn)));
    ImGui::DragInt("Steps", reinterpret_cast<int*>(&this->integration_steps));
    ImGui::DragInt("Batch Size", reinterpret_cast<int*>(&this->batch_size));
    ImGui::DragFloat("Delta Time", &this->delta_time, 0.001f, 0.0f);

    ImGui::BeginDisabled(!this->dataset || this->integration_in_progress());
    if (ImGui::Button("Integrate")) {
        this->should_integrate = true;
    }
    ImGui::EndDisabled();
    if (this->integration_in_progress()) {
        auto progress = reinterpret_cast<const std::uint32_t*>(this->progress_buffer->get_mapped_data());
        lava::log()->debug("{}", *progress);
        ImGui::ProgressBar(static_cast<float>(*progress) / (this->integration->seed_count * this->integration->integration_steps));
    }
    if (this->integration) {
        ImGui::Text("Duration: %f ms (CPU), %f ms (GPU)", this->integration->cpu_time, this->integration->gpu_time);
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Line Width", &this->line_width, 0.025f, 0.1, 10.0f);
        ImGui::ColorEdit4("Line Color", glm::value_ptr(this->line_color));
    }
}

bool Integrator::create_progress_buffer() {
    this->progress_buffer = lava::buffer::make();
    this->progress_buffer->create_mapped(this->device, nullptr, 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    if (this->progress_buffer->get_mapped_data() == nullptr) {
        lava::log()->error("failed to map data of progress buffer");
        return false;
    }
    memset(this->progress_buffer->get_mapped_data(), 0, 4);

    return true;
}

bool Integrator::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = this->compute_queue.family,
    };
    if (this->device->vkCreateCommandPool(&pool_info, &this->command_pool).value != VK_SUCCESS) {
        lava::log()->error("failed to create command pool");
        return false;
    }
    return true;
}

bool Integrator::create_query_pool() {
    VkQueryPoolCreateInfo query_pool_create{
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2,

    };
    if (this->device->call().vkCreateQueryPool(this->device->get(), &query_pool_create, nullptr, &this->query_pool) != VK_SUCCESS) {
        lava::log()->error("failed to create query pool");
        return false;
    }

    return true;
}

bool Integrator::create_descriptor() {
    lava::log()->debug("create descriptor for {} channels", this->dataset->data->channel_count);
    this->descriptor = lava::descriptor::make();
    this->descriptor->add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    this->descriptor->add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    this->descriptor->add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    for (int i = 0; i < this->dataset->data->channel_count; ++i) {
        lava::descriptor::binding::ptr dataset_binding = lava::descriptor::binding::make(3 + i);
        dataset_binding->set_type(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        dataset_binding->set_stage_flags(VK_SHADER_STAGE_COMPUTE_BIT);
        dataset_binding->set_count(MAX_TIME_SLICES);
        this->descriptor->add(dataset_binding);
    }
    if (!descriptor->create(device)) {
        lava::log()->error("failed to create descriptor for integration");
        return false;
    }

    this->descriptor_pool = lava::descriptor::pool::make();
    if (!descriptor_pool->create(device, {
                                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TIME_SLICES * this->dataset->data->channel_count},
                                             {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                                         },
                                 1)) {
        lava::log()->error("failed to create descriptor pool for integration");
        return false;
    }

    this->descriptor_set = this->descriptor->allocate(this->descriptor_pool->get());

    const VkDescriptorBufferInfo progress_buffer_info{
        .buffer = this->progress_buffer->get(),
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    this->device->vkUpdateDescriptorSets({{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = this->descriptor_set,
        .dstBinding = PROGRESS_BUFFER_BINDING,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &progress_buffer_info,
    }});

    return true;
}

void Integrator::destroy_descriptor() {
    if (this->descriptor_set) {
        this->descriptor->free(this->descriptor_set, this->descriptor_pool->get());
        this->descriptor_set = VK_NULL_HANDLE;
    }
    if (this->descriptor_pool) {
        this->descriptor_pool->destroy();
        this->descriptor_pool = nullptr;
    }
    if (this->descriptor) {
        this->descriptor->destroy();
        this->descriptor = nullptr;
    }
}

bool Integrator::create_render_pipeline() {
    this->render_pipeline_layout = lava::pipeline_layout::make();
    this->render_pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)});
    this->render_pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), sizeof(glm::vec4)});
    if (!render_pipeline_layout->create(device)) {
        destroy();
        return false;
    }

    this->render_pipeline = lava::render_pipeline::make(device, app->pipeline_cache);
    this->render_pipeline->set_rasterization_polygon_mode(VK_POLYGON_MODE_LINE);
    this->render_pipeline->set_input_topology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
    this->render_pipeline->add_dynamic_state(VK_DYNAMIC_STATE_LINE_WIDTH);
    this->render_pipeline->set_layout(this->render_pipeline_layout);
    this->render_pipeline->add_color_blend_attachment();
    this->render_pipeline->set_depth_test_and_write();
    this->render_pipeline->set_depth_compare_op(VK_COMPARE_OP_LESS);
    this->render_pipeline->set_vertex_input_binding({0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX});
    this->render_pipeline->set_vertex_input_attributes({
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
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
    this->render_pipeline->create(app->shading.get_vk_pass());
    app->shading.get_pass()->add_front(this->render_pipeline);
    this->render_pipeline->on_process = [this](VkCommandBuffer command_buffer) { this->render(command_buffer); };

    return true;
}

void Integrator::destroy_render_pipeline() {
    if (this->render_pipeline) {
        this->render_pipeline->destroy();
        this->render_pipeline = nullptr;
    }

    if (this->render_pipeline_layout) {
        this->render_pipeline_layout->destroy();
        this->render_pipeline_layout = nullptr;
    }
}

bool Integrator::create_integration_pipeline() {
    lava::log()->debug("create integration pipeline");
    this->integration_pipeline_layout = lava::pipeline_layout::make();
    this->integration_pipeline_layout->add(this->descriptor);
    this->integration_pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants)});
    if (!integration_pipeline_layout->create(device)) {
        return false;
    }

    this->integration_pipeline = lava::compute_pipeline::make(this->device, this->app->pipeline_cache);
    this->integration_pipeline->set_layout(this->integration_pipeline_layout);

    const lava::cdata* shader;
    if (this->dataset->data->channel_count == 1) {
        lava::log()->debug("bc6h texture dataset");
        shader = &integrate_bc6h_comp_cdata;
    } else if (this->dataset->data->channel_count == 3) {
        lava::log()->debug("raw textures dataset");
        shader = &integrate_raw_comp_cdata;
    } else {
        lava::log()->error("cannot create integration pipeline: invalid dataset");
        return false;
    }

    if (!shader) {
        lava::log()->error("internal error");
        return false;
    }

    struct IntegrationConstants {
        glm::uint work_group_size_x;
        glm::uint work_group_size_y;
        glm::uint work_group_size_z;
        glm::uint time_steps;
    } const integration_constants = {
        .work_group_size_x = this->work_group_size.x,
        .work_group_size_y = this->work_group_size.y,
        .work_group_size_z = this->work_group_size.z,
        .time_steps = this->dataset->data->dimensions.w,
    };

    lava::pipeline::shader_stage::ptr shader_stage = lava::pipeline::shader_stage::make(VK_SHADER_STAGE_COMPUTE_BIT);
    shader_stage->add_specialization_entry({
        .constantID = WORK_GROUP_SIZE_X_CONSTANT_ID,
        .offset = offsetof(IntegrationConstants, work_group_size_x),
        .size = sizeof(glm::uint),
    });
    shader_stage->add_specialization_entry({
        .constantID = WORK_GROUP_SIZE_Y_CONSTANT_ID,
        .offset = offsetof(IntegrationConstants, work_group_size_y),
        .size = sizeof(glm::uint),
    });
    shader_stage->add_specialization_entry({
        .constantID = WORK_GROUP_SIZE_Z_CONSTANT_ID,
        .offset = offsetof(IntegrationConstants, work_group_size_z),
        .size = sizeof(glm::uint),
    });
    shader_stage->add_specialization_entry({
        .constantID = TIME_STEPS_CONSTANT_ID,
        .offset = offsetof(IntegrationConstants, time_steps),
        .size = sizeof(glm::uint),
    });
    if (!shader_stage->create(this->device, *shader, lava::cdata(&integration_constants, sizeof(integration_constants)))) {
        lava::log()->error("failed to create integration shader stage");
        return false;
    }
    this->integration_pipeline->set(shader_stage);

    if (!this->integration_pipeline->create()) {
        lava::log()->error("failed to create integration pipeline");
        return false;
    }

    this->recreate_integration_pipeline = false;

    return true;
}

void Integrator::destroy_integration_pipeline() {
    if (this->integration_pipeline_layout) {
        this->integration_pipeline_layout->destroy();
        this->integration_pipeline_layout = nullptr;
    }

    if (this->integration_pipeline) {
        this->integration_pipeline->destroy();
        this->integration_pipeline = nullptr;
    }
}

void Integrator::write_dataset_to_descriptor() {
    std::vector<VkWriteDescriptorSet> descriptor_writes;
    descriptor_writes.reserve(this->dataset->data->channel_count * MAX_TIME_SLICES);

    for (std::uint32_t c = 0; c < this->dataset->data->channel_count; ++c) {
        for (std::uint32_t i = 0; i < MAX_TIME_SLICES; ++i) {
            descriptor_writes.push_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = this->descriptor_set,
                .dstBinding = DATASET_BINDING_BASE + c,
                .dstArrayElement = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &this->dataset->get_image(c, i % this->dataset->data->dimensions.w).image_info});
        }
    }
    vkUpdateDescriptorSets(this->descriptor->get_device()->get(), descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);
}

void Integrator::reset_dataset() {
}

bool Integrator::Integration::create_buffers(glm::uvec3 seed_spawn, std::uint32_t integration_steps, lava::device_p device, const lava::queue& compute_queue) {
    this->integration_steps = integration_steps;
    this->seed_count = seed_spawn.x * seed_spawn.y * seed_spawn.z;

    const std::size_t buffer_size = seed_count * integration_steps * sizeof(glm::vec4);

    const std::array<std::uint32_t, 2> queue_family_indices = {
        device->get_queues()[queue_indices::GRAPHICS].family,
        compute_queue.family,
    };

    const VkBufferCreateInfo line_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = queue_family_indices.size(),
        .pQueueFamilyIndices = queue_family_indices.data(),
    };
    const VmaAllocationCreateInfo line_buffer_alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    if (vmaCreateBuffer(
            device->alloc(),
            &line_buffer_create_info,
            &line_buffer_alloc_info,
            &this->line_buffer,
            &this->line_buffer_allocation,
            nullptr
        ) != VK_SUCCESS) {
        lava::log()->error("failed to create line buffer");
        return false;
    }

    const VkBufferCreateInfo indirect_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(VkDrawIndirectCommand) * seed_count,
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = queue_family_indices.size(),
        .pQueueFamilyIndices = queue_family_indices.data(),
    };
    const VmaAllocationCreateInfo indirect_buffer_alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    if (vmaCreateBuffer(
            device->alloc(),
            &indirect_buffer_create_info,
            &indirect_buffer_alloc_info,
            &this->indirect_buffer,
            &this->indirect_buffer_allocation,
            nullptr
        ) != VK_SUCCESS) {
        lava::log()->error("failed to create indirect buffer");
        return false;
    }

    return true;
}

void Integrator::Integration::update_descriptor_set(lava::device_p device, VkDescriptorSet descriptor_set) {
    const VkDescriptorBufferInfo line_buffer_info{
        .buffer = this->line_buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    const VkDescriptorBufferInfo indirect_buffer_info{
        .buffer = this->indirect_buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    device->vkUpdateDescriptorSets({
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = LINE_BUFFER_BINDING,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &line_buffer_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = INDIRECT_BUFFER_BINDING,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &indirect_buffer_info,
        },
    });
}

void Integrator::Integration::destroy(lava::device_p device, VkCommandPool command_pool) {
    device->wait_for_idle();
    vmaDestroyBuffer(device->alloc(), this->line_buffer, this->line_buffer_allocation);
    vmaDestroyBuffer(device->alloc(), this->indirect_buffer, this->indirect_buffer_allocation);
}

void Integrator::destroy_integration() {
    if (this->integration.has_value()) {
        this->integration->destroy(this->device, this->command_pool);
        this->integration.reset();
    }
}

bool Integrator::prepare_integration() {
    if (this->recreate_integration_pipeline || !this->integration_pipeline) {
        if (this->integration_pipeline) {
            this->destroy_integration_pipeline();
        }
        if (!this->create_integration_pipeline()) {
            return false;
        }
    }

    this->destroy_integration();
    this->integration.emplace();
    lava::log()->debug("create buffers");
    lava::log()->flush();

    lava::timer sw;
    this->integration->create_buffers(this->seed_spawn, this->integration_steps, this->device, this->compute_queue);
    lava::log()->debug("integration buffers created ({} ms)", sw.elapsed().count());
    this->integration->update_descriptor_set(this->device, this->descriptor_set);
    this->write_dataset_to_descriptor();
    return true;
}

bool Integrator::integrate() {

    Constants c{
        .dataset_resolution = this->dataset->data->resolution,
        .dataset_dimensions = this->dataset->data->dimensions_in_meters_and_seconds(),
        .seed_dimensions = this->seed_spawn,
        .dt = this->delta_time,
        .total_step_count = this->integration->integration_steps,
        .first_step = 0,
        .step_count = this->integration_steps,
    };

    lava::timer sw;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (!this->device->vkAllocateCommandBuffers(this->command_pool, 1, &command_buffer)) {
        lava::log()->error("failed to create command buffer for integration");
        return false;
    }

    VkFence fence;

    VkFenceCreateInfo fence_create{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    this->device->vkCreateFence(&fence_create, &fence);

    lava::log()->debug("start integration");

    unsigned int step_count = 0;
    while (step_count < this->integration_steps) {
        c.first_step = step_count;
        c.step_count = std::min(this->integration_steps - step_count, this->batch_size);

        lava::log()->debug("batch (first_step = {}, step_count = {})", c.first_step, c.step_count);

        VkCommandBufferBeginInfo begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
            lava::log()->error("failed to begin command buffer");
            return false;
        }

        device->call().vkCmdResetQueryPool(command_buffer, this->query_pool, 0, 2);

        device->call().vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, this->query_pool, 0);

        this->integration_pipeline->bind(command_buffer);
        this->integration_pipeline_layout->bind(command_buffer, this->descriptor_set, 0, {}, VK_PIPELINE_BIND_POINT_COMPUTE);

        device->call().vkCmdPushConstants(
                command_buffer, 
                this->integration_pipeline_layout->get(),
                VK_SHADER_STAGE_COMPUTE_BIT,
                0, sizeof(Constants), &c);

        // device->call().vkCmdPushConstants(
        //     command_buffer,
        //     this->integration_pipeline_layout->get(),
        //     VK_SHADER_STAGE_COMPUTE_BIT,
        //     offsetof(Constants, first_step),
        //     sizeof(uint32_t) * 2,
        //     &c.first_step
        // );
        device->call().vkCmdDispatch(
            command_buffer,
            (this->seed_spawn.x + this->work_group_size.x - 1) / this->work_group_size.x,
            (this->seed_spawn.y + this->work_group_size.y - 1) / this->work_group_size.y,
            (this->seed_spawn.z + this->work_group_size.z - 1) / this->work_group_size.z
        );

        device->call().vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, this->query_pool, 1);

        if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
            lava::log()->error("failed to end command buffer");
            return false;
        }

        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer};
        if (!device->vkQueueSubmit(this->compute_queue.vk_queue, 1, &submit_info, fence)) {
            lava::log()->error("failed to submit command buffer");
            return false;
        }
        this->integration->cpu_time = sw.elapsed().count();
        while (this->device->vkWaitForFences(1, &fence, true, 1000 * 1000).value == VK_TIMEOUT) {
        }
        this->device->vkResetFences(1, &fence);
        this->integration->cpu_time = sw.elapsed().count();

        std::array<std::uint64_t, 2> timestamps;
        if (this->device->call().vkGetQueryPoolResults(
                this->device->get(),
                this->query_pool,
                0, 2,
                sizeof(timestamps), timestamps.data(), sizeof(timestamps[0]),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            ) != VK_SUCCESS) {
            lava::log()->error("failed to receive gpu times");
        } else {
            const std::uint64_t duration_ns = timestamps[1] - timestamps[0];
            const double duration_ms = duration_ns / 1000.0 / 1000.0;
            this->integration->gpu_time += duration_ms;
        }

        step_count += c.step_count;
        // break;
    }
    lava::log()->debug("integration dispatched ({} ms)", sw.elapsed().count());

    this->device->vkFreeCommandBuffers(this->command_pool, 1, &command_buffer);

    this->integration->cpu_time = sw.elapsed().count();
    this->integration->complete = true;

    return true;
}
