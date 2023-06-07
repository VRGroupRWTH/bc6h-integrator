#include "integrator.hpp"
#include "queues.hpp"
#include "shaders.hpp"
#include "time_slices.hpp"
#include <array>
#include <cstddef>
#include <glm/gtx/string_cast.hpp>
#include <imgui.h>
#include <liblava/app.hpp>
#include <spdlog/fmt/bundled/core.h>
#include <spdlog/fmt/bundled/ostream.h>
#include <vulkan/vulkan_core.h>

struct Constants {
    glm::vec4 dataset_dimensions;
    glm::uvec3 seed_dimensions;
    float dt;
    glm::uint total_step_count;
    glm::uint first_step;
    glm::uint step_count;
};

constexpr std::uint32_t LINE_BUFFER_BINDING = 0;
constexpr std::uint32_t MAX_VELOCITY_MAGNITUDE_BUFFER_BINDING = 1;
constexpr std::uint32_t INDIRECT_BUFFER_BINDING = 2;
constexpr std::uint32_t DATASET_BINDING_BASE = 3;
constexpr std::uint32_t WORK_GROUP_SIZE_X_CONSTANT_ID = 0;
constexpr std::uint32_t WORK_GROUP_SIZE_Y_CONSTANT_ID = 1;
constexpr std::uint32_t WORK_GROUP_SIZE_Z_CONSTANT_ID = 2;
constexpr std::uint32_t TIME_STEPS_CONSTANT_ID = 3;
constexpr std::uint32_t EXPLICIT_INTERPOLATION_ID = 4;

Integrator::Integrator() {
    this->download_file_name.fill('\0');
}

bool Integrator::create(lava::app& app) {
    const auto& queues = app.device->queues();
    this->compute_queue = queues[queue_indices::COMPUTE];
    this->device = app.device;
    this->app = &app;

    return this->create_command_pool() &&
           this->create_query_pool() &&
           this->create_max_velocity_magnitude_buffer();

    return true;
}

void Integrator::destroy() {
    this->destroy_integration();
    this->destroy_render_pipeline();
    this->destroy_seeding_pipeline();
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
    if (this->max_velocity_magnitude_buffer) {
        this->max_velocity_magnitude_buffer->destroy();
        this->max_velocity_magnitude_buffer = nullptr;
    }
}

void Integrator::render(VkCommandBuffer command_buffer) {
    if (!this->integration.has_value()) {
        return;
    }

    const VkBool32 invert_colormap = this->line_colormap_invert;

    this->render_pipeline->bind(command_buffer);
    const VkDeviceSize buffer_offsets = 0;
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), -0.5f * glm::vec3(this->dataset->data->dimensions));
    const glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(this->scaling));
    const glm::mat4 world = scaling * translation;
    const glm::mat4 view_projection = this->app->camera.get_view_projection();
    const glm::mat4 world_view_projection = view_projection * world;
    this->device->call().vkCmdSetLineWidth(command_buffer, this->line_width);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(world_view_projection), &world_view_projection);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 16 * 4, sizeof(this->line_color), glm::value_ptr(line_color));
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 20 * 4, sizeof(this->line_colormap), &this->line_colormap);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 20 * 4, sizeof(this->line_colormap), &this->line_colormap);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 21 * 4, sizeof(invert_colormap), &invert_colormap);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 22 * 4, sizeof(this->line_velocity_min), &this->line_velocity_min);
    this->device->call().vkCmdPushConstants(command_buffer, this->render_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 23 * 4, sizeof(this->line_velocity_max), &this->line_velocity_max);
    this->device->call().vkCmdBindVertexBuffers(command_buffer, 0, 1, &this->integration->line_buffer, &buffer_offsets);
    this->device->call().vkCmdDrawIndirect(command_buffer, this->integration->indirect_buffer, 0, this->integration->seed_count, sizeof(VkDrawIndirectCommand));
}

void Integrator::set_dataset(Dataset::Ptr dataset) {
    this->log_file.close();
    this->run = 0;
    this->destroy_integration();
    this->destroy_seeding_pipeline();
    this->destroy_integration_pipeline();
    this->destroy_descriptor();

    this->dataset = dataset;

    if (this->dataset) {
        this->create_descriptor();

        const auto dimensions = this->dataset->data->dimensions;
        this->scaling = 1.0f / std::max(dimensions.x, std::max(dimensions.y, dimensions.z));
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
        this->log_file.close();
    }
    if (ImGui::DragInt3("Seed Dimensions", reinterpret_cast<int*>(glm::value_ptr(this->seed_spawn)))) {
        this->log_file.close();
    }
    if (ImGui::DragInt("Steps", reinterpret_cast<int*>(&this->integration_steps))) {
        this->log_file.close();
        if (this->dataset) {
            this->delta_time = 1.0f / this->dataset->data->dimensions.w;
        }
    }
    if (ImGui::DragInt("Batch Size", reinterpret_cast<int*>(&this->batch_size))) {
        this->log_file.close();
    }
    if (ImGui::DragFloat("Delta Time", &this->delta_time, 0.001f, 0.0f)) {
        this->log_file.close();
    }

    if (ImGui::Checkbox("Analytic Dataset", &this->analytic_dataset)) {
        this->recreate_integration_pipeline = true;
        this->log_file.close();
    }

    if (!this->analytic_dataset) {
        if (ImGui::Checkbox("Explicit Interpolation", &this->explicit_interpolation)) {
            this->recreate_integration_pipeline = true;
            this->log_file.close();
        }
    }

    ImGui::BeginDisabled(!this->dataset || this->integration_in_progress());
    if (ImGui::Button("Integrate")) {
        this->should_integrate = true;
    }
    ImGui::EndDisabled();

    ImGui::InputText("File Name", this->download_file_name.data(), this->download_file_name.size());

    ImGui::BeginDisabled(!this->integration.has_value() || this->download_file_name.empty());
    if (ImGui::Button("Download")) {
        this->download_trajectories(this->download_file_name.data());
    }
    ImGui::EndDisabled();

    if (this->integration_in_progress()) {
        auto progress = float(this->integration->current_batch) / this->integration->batch_count;
        // lava::log()->debug("{}", progress);
        ImGui::ProgressBar(progress);
    }
    if (this->integration) {
        ImGui::Text("Duration: %f ms (CPU), %f ms (GPU)", this->integration->cpu_time, this->integration->gpu_time);
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Scaling", &this->scaling, 0.01f, 0.00000001, 10.0f);
        const std::vector<const char*> line_colormap_names = {
            "Constant Color",
            "MATLAB_bone",
            "MATLAB_hot",
            "MATLAB_jet",
            "MATLAB_summer",
            "IDL_Rainbow",
            "IDL_Mac_Style",
            "IDL_CB-YIGn",
            "IDL_CB-YIGnBu",
            "IDL_CB-RdBu",
            "IDL_CB-RdYiGn",
            "IDL_CB-Spectral",
            "transform_rainbow"};

        ImGui::Combo("Line Colormap", (int32_t*)&this->line_colormap, line_colormap_names.data(), line_colormap_names.size());

        if (this->line_colormap == 0) {
            ImGui::ColorEdit4("Line Color", glm::value_ptr(this->line_color));
        }

        else {
            ImGui::Checkbox("Line Colormap Invert", &this->line_colormap_invert);
            ImGui::DragFloat("Line Velocity Min", &this->line_velocity_min, 0.01f);
            ImGui::DragFloat("Line Velocity Max", &this->line_velocity_max, 0.01f);
        }

        ImGui::DragFloat("Line Width", &this->line_width, 0.025f, 0.1, 10.0f);
    }
}

bool Integrator::create_max_velocity_magnitude_buffer() {
    this->max_velocity_magnitude_buffer = lava::buffer::make();
    this->max_velocity_magnitude_buffer->create_mapped(this->device, nullptr, 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    if (this->max_velocity_magnitude_buffer->get_mapped_data() == nullptr) {
        lava::log()->error("failed to map data of progress buffer");
        return false;
    }
    memset(this->max_velocity_magnitude_buffer->get_mapped_data(), 0, 4);

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

    const VkDescriptorBufferInfo max_velocity_magnitude_buffer_info{
        .buffer = this->max_velocity_magnitude_buffer->get(),
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    this->device->vkUpdateDescriptorSets({{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = this->descriptor_set,
        .dstBinding = MAX_VELOCITY_MAGNITUDE_BUFFER_BINDING,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &max_velocity_magnitude_buffer_info,
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
    this->render_pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), sizeof(glm::vec4) + sizeof(uint32_t) + sizeof(VkBool32) + 2 * sizeof(float)});
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

bool Integrator::create_seeding_pipeline() {
    lava::log()->debug("create seeding pipeline");

    VkPushConstantRange constant_range;
    constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    constant_range.offset = 0;
    constant_range.size = sizeof(Constants);

    this->seeding_pipeline_layout = lava::pipeline_layout::make();
    this->seeding_pipeline_layout->add(this->descriptor);
    this->seeding_pipeline_layout->add(constant_range);

    if (!this->seeding_pipeline_layout->create(this->device)) {
        lava::log()->error("can't create seeding pipeline layout!");

        return false;
    }

    std::array<uint32_t, 3> seeding_constants;
    seeding_constants[0] = this->work_group_size.x;
    seeding_constants[1] = this->work_group_size.x;
    seeding_constants[2] = this->work_group_size.x;

    VkSpecializationMapEntry workgroup_size_x;
    workgroup_size_x.constantID = WORK_GROUP_SIZE_X_CONSTANT_ID;
    workgroup_size_x.offset = 0;
    workgroup_size_x.size = sizeof(uint32_t);

    VkSpecializationMapEntry workgroup_size_y;
    workgroup_size_y.constantID = WORK_GROUP_SIZE_Y_CONSTANT_ID;
    workgroup_size_y.offset = sizeof(uint32_t);
    workgroup_size_y.size = sizeof(uint32_t);

    VkSpecializationMapEntry workgroup_size_z;
    workgroup_size_z.constantID = WORK_GROUP_SIZE_Z_CONSTANT_ID;
    workgroup_size_z.offset = 2 * sizeof(uint32_t);
    workgroup_size_z.size = sizeof(uint32_t);

    lava::pipeline::shader_stage::ptr shader_stage = lava::pipeline::shader_stage::make(VK_SHADER_STAGE_COMPUTE_BIT);
    shader_stage->add_specialization_entry(workgroup_size_x);
    shader_stage->add_specialization_entry(workgroup_size_y);
    shader_stage->add_specialization_entry(workgroup_size_z);

    if (!shader_stage->create(this->device, seeding_comp_cdata, lava::cdata(seeding_constants.data(), seeding_constants.size() * sizeof(uint32_t)))) {
        lava::log()->error("can't create seeding compute shader!");

        return false;
    }

    this->seeding_pipeline = lava::compute_pipeline::make(this->device, this->app->pipeline_cache);
    this->seeding_pipeline->set_layout(this->seeding_pipeline_layout);
    this->seeding_pipeline->set(shader_stage);

    if (!this->seeding_pipeline->create()) {
        lava::log()->error("can't create seeding pipeline!");

        return false;
    }

    return true;
}

void Integrator::destroy_seeding_pipeline() {
    if (this->seeding_pipeline_layout) {
        this->seeding_pipeline_layout->destroy();
        this->seeding_pipeline_layout = nullptr;
    }

    if (this->seeding_pipeline) {
        this->seeding_pipeline->destroy();
        this->seeding_pipeline = nullptr;
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
    if (this->analytic_dataset) {
        lava::log()->debug("analytic dataset");
        shader = &integrate_analytic_comp_cdata;
    } else if (this->dataset->data->channel_count == 1) {
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
        VkBool32 explicit_interpolation;
    } const integration_constants = {
        .work_group_size_x = this->work_group_size.x,
        .work_group_size_y = this->work_group_size.y,
        .work_group_size_z = this->work_group_size.z,
        .time_steps = this->dataset->data->dimensions.w,
        .explicit_interpolation = this->explicit_interpolation};

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
    shader_stage->add_specialization_entry({
        .constantID = EXPLICIT_INTERPOLATION_ID,
        .offset = offsetof(IntegrationConstants, explicit_interpolation),
        .size = sizeof(VkBool32),
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

    const std::size_t buffer_size = seed_count * (integration_steps + 1) * sizeof(glm::vec4); // Increase integration steps by one for seeding position

    const std::array<std::uint32_t, 2> queue_family_indices = {
        device->get_queues()[queue_indices::GRAPHICS].family,
        compute_queue.family,
    };

    const VkBufferCreateInfo line_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
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
            nullptr) != VK_SUCCESS) {
        lava::log()->error("failed to create line buffer");
        return false;
    }

    const VkBufferCreateInfo indirect_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(VkDrawIndirectCommand) * seed_count,
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
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
            nullptr) != VK_SUCCESS) {
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
    if (this->recreate_integration_pipeline || !this->seeding_pipeline || !this->integration_pipeline) {
        if (this->seeding_pipeline) {
            this->destroy_seeding_pipeline();
        }

        if (!this->create_seeding_pipeline()) {
            return false;
        }

        if (this->integration_pipeline) {
            this->destroy_integration_pipeline();
        }

        if (!this->create_integration_pipeline()) {
            return false;
        }

        this->recreate_integration_pipeline = false;
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
    if (!this->log_file.is_open()) {
        const auto absolute_dataset_path = std::filesystem::absolute(this->dataset->data->filename);
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

        const std::string filename = fmt::format("{}-{}-{}-{}-{}-{}-{}-{}-integration.csv", now->tm_year + 1900, now->tm_mon, now->tm_mday, weekdays[now->tm_wday], now->tm_hour, now->tm_min, now->tm_sec, dataset_filename);
        this->log_file = std::ofstream(filename);
        fmt::print(this->log_file, "run,compute_shader_duration,dataset_path,dataset_dimensions,work_group_size,seed_spawn,timestep,integration_steps,batch_size,explicit_interpolation,analytic\n");
        fmt::print(
            this->log_file, ",,{},{}x{}x{}x{},{}x{}x{},{}x{}x{},{},{},{},{},{}\n",
            absolute_dataset_path,
            this->dataset->data->dimensions.x, this->dataset->data->dimensions.y, this->dataset->data->dimensions.z, this->dataset->data->dimensions.w,
            this->work_group_size.x, this->work_group_size.y, this->work_group_size.z,
            this->seed_spawn.x, this->seed_spawn.y, this->seed_spawn.z,
            this->delta_time,
            this->integration_steps,
            this->batch_size,
            this->explicit_interpolation,
            this->analytic_dataset);
    }

    VkCommandBufferAllocateInfo command_buffer_info;
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = nullptr;
    command_buffer_info.commandPool = this->command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(this->device->get(), &command_buffer_info, &command_buffer) != VK_SUCCESS) {
        lava::log()->error("can't create command buffer for integration!");

        return false;
    }

    VkFenceCreateInfo fence_info;
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = nullptr;
    fence_info.flags = 0;

    VkFence fence = VK_NULL_HANDLE;

    if (vkCreateFence(this->device->get(), &fence_info, lava::memory::instance().alloc(), &fence) != VK_SUCCESS) {
        lava::log()->error("can't create fence for integration!");

        return false;
    }

    Constants constants;
    constants.dataset_dimensions = this->dataset->data->dimensions;
    constants.seed_dimensions = this->seed_spawn;
    constants.dt = this->delta_time;
    constants.total_step_count = this->integration->integration_steps;
    constants.first_step = 0;
    constants.step_count = this->integration_steps;

    this->integration->cpu_time = 0.0;
    this->integration->gpu_time = 0.0;
    this->integration->complete = false;
    this->integration->batch_count = (this->integration_steps + this->batch_size - 1) / this->batch_size;
    this->integration->current_batch = 0;

    memset(this->max_velocity_magnitude_buffer->get_mapped_data(), 0, 4);

    lava::timer timer;

    if (!this->perform_seeding(command_buffer, fence, timer, constants)) {
        return false;
    }

    if (!this->perform_integration(command_buffer, fence, timer, constants)) {
        return false;
    }

    this->integration->cpu_time = timer.elapsed().count();
    this->integration->complete = true;

    vkFreeCommandBuffers(this->device->get(), this->command_pool, 1, &command_buffer);
    vkDestroyFence(this->device->get(), fence, lava::memory::instance().alloc());

    this->log_file.flush();
    this->run++;

    return true;
}

bool Integrator::perform_seeding(VkCommandBuffer command_buffer, VkFence fence, lava::timer& timer, Constants& constants) {
    lava::log()->debug("start seeding");

    bool result = this->submit_and_measure_command(command_buffer, fence, timer, [=]() {
        this->seeding_pipeline->bind(command_buffer);
        this->seeding_pipeline_layout->bind(command_buffer, this->descriptor_set, 0, {}, VK_PIPELINE_BIND_POINT_COMPUTE);

        glm::uvec3 work_group_count = (this->seed_spawn + this->work_group_size - 1u) / this->work_group_size;

        vkCmdPushConstants(command_buffer, this->seeding_pipeline_layout->get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &constants);
        vkCmdDispatch(command_buffer, work_group_count.x, work_group_count.y, work_group_count.z);
    });

    return result;
}

bool Integrator::perform_integration(VkCommandBuffer command_buffer, VkFence fence, lava::timer& timer, Constants& constants) {
    lava::log()->debug("start integration");

    unsigned int step_count = 0;
    while (step_count < this->integration_steps) {
        constants.first_step = step_count;
        constants.step_count = std::min(this->integration_steps - step_count, this->batch_size);

        lava::log()->debug("batch (first_step = {}, step_count = {})", constants.first_step, constants.step_count);

        bool result = this->submit_and_measure_command(command_buffer, fence, timer, [=]() {
            this->integration_pipeline->bind(command_buffer);
            this->integration_pipeline_layout->bind(command_buffer, this->descriptor_set, 0, {}, VK_PIPELINE_BIND_POINT_COMPUTE);

            glm::uvec3 work_group_count = (this->seed_spawn + this->work_group_size - 1u) / this->work_group_size;

            vkCmdPushConstants(command_buffer, this->seeding_pipeline_layout->get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &constants);
            vkCmdDispatch(command_buffer, work_group_count.x, work_group_count.y, work_group_count.z);
        });

        if (!result) {
            return false;
        }

        step_count += constants.step_count;
        this->integration->current_batch += 1;
    }

    lava::log()->debug("integration dispatched ({} ms)", timer.elapsed().count());

    return true;
}

bool Integrator::submit_and_measure_command(VkCommandBuffer command_buffer, VkFence fence, lava::timer& timer, std::function<void()> function) {
    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = nullptr;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        lava::log()->error("can't begin command buffer!");

        return false;
    }

    vkCmdResetQueryPool(command_buffer, this->query_pool, 0, 2);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, this->query_pool, 0);

    function();

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, this->query_pool, 1);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        lava::log()->error("can't end command buffer!");

        return false;
    }

    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;

    if (vkQueueSubmit(this->compute_queue.vk_queue, 1, &submit_info, fence) != VK_SUCCESS) {
        lava::log()->error("can't submit command buffer!");

        return false;
    }

    this->integration->cpu_time = timer.elapsed().count();

    while (true) {
        const uint64_t check_intervall = 10000000; // Timeout after 10ms

        VkResult result = vkWaitForFences(this->device->get(), 1, &fence, VK_TRUE, check_intervall);

        this->integration->cpu_time = timer.elapsed().count(); // CPU time is updated every 10ms

        if (result == VK_SUCCESS) {
            break;
        }

        else if (result == VK_TIMEOUT) {
            continue;
        }

        else {
            lava::log()->error("can't wait for completion of command buffer!");

            return false;
        }
    }

    if (vkResetFences(this->device->get(), 1, &fence) != VK_SUCCESS) {
        lava::log()->error("can't reset fence!");

        return false;
    }

    std::array<std::uint64_t, 2> timestamps;

    if (vkGetQueryPoolResults(this->device->get(), this->query_pool, 0, 2, sizeof(timestamps), timestamps.data(), sizeof(timestamps[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) {
        lava::log()->error("failed to receive gpu times!");

        return false;
    }

    const float timestamp_period = this->device->get_properties().limits.timestampPeriod;
    const double duration_ns = (timestamps[1] - timestamps[0]) * (double)timestamp_period;
    const double duration_ms = duration_ns / 1000.0 / 1000.0;
    fmt::print(this->log_file, "{},{}\n", this->run, duration_ms);

    this->integration->gpu_time += duration_ms;
    this->line_velocity_max = *reinterpret_cast<const float*>(this->max_velocity_magnitude_buffer->get_mapped_data());

    return true;
}

bool Integrator::download_trajectories(const std::string& file_name) {
    const std::size_t seed_count = this->integration.value().seed_count;
    const std::size_t integration_steps = this->integration.value().integration_steps;

    const std::size_t line_buffer_size = seed_count * (integration_steps + 1) * sizeof(glm::vec4); // Increase integration steps by one for seeding position
    const std::size_t indirect_buffer_size = seed_count * sizeof(VkDrawIndirectCommand);

    VmaAllocationCreateInfo allocation_info;
    allocation_info.flags = 0;
    allocation_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    allocation_info.requiredFlags = 0;
    allocation_info.preferredFlags = 0;
    allocation_info.memoryTypeBits = 0;
    allocation_info.pool = VK_NULL_HANDLE;
    allocation_info.pUserData = nullptr;
    allocation_info.priority = 0.0f;

    VkBufferCreateInfo line_staging_buffer_info;
    line_staging_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    line_staging_buffer_info.pNext = nullptr;
    line_staging_buffer_info.flags = 0;
    line_staging_buffer_info.size = line_buffer_size;
    line_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    line_staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    line_staging_buffer_info.queueFamilyIndexCount = 0;
    line_staging_buffer_info.pQueueFamilyIndices = nullptr;

    VkBuffer line_staging_buffer = VK_NULL_HANDLE;
    VmaAllocation line_staging_buffer_allocation = VK_NULL_HANDLE;

    if (vmaCreateBuffer(this->device->alloc(), &line_staging_buffer_info, &allocation_info, &line_staging_buffer, &line_staging_buffer_allocation, nullptr) != VK_SUCCESS) {
        lava::log()->error("Can't create staging buffer for line buffer during download!");

        return false;
    }

    VkBufferCreateInfo indirect_staging_buffer_info;
    indirect_staging_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indirect_staging_buffer_info.pNext = nullptr;
    indirect_staging_buffer_info.flags = 0;
    indirect_staging_buffer_info.size = indirect_buffer_size;
    indirect_staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    indirect_staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    indirect_staging_buffer_info.queueFamilyIndexCount = 0;
    indirect_staging_buffer_info.pQueueFamilyIndices = nullptr;

    VkBuffer indirect_staging_buffer = VK_NULL_HANDLE;
    VmaAllocation indirect_staging_buffer_allocation = VK_NULL_HANDLE;

    if (vmaCreateBuffer(this->device->alloc(), &indirect_staging_buffer_info, &allocation_info, &indirect_staging_buffer, &indirect_staging_buffer_allocation, nullptr) != VK_SUCCESS) {
        lava::log()->error("Can't create staging buffer for indirect buffer during download!");

        return false;
    }

    VkCommandBufferAllocateInfo command_buffer_info;
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = nullptr;
    command_buffer_info.commandPool = this->command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(this->device->get(), &command_buffer_info, &command_buffer) != VK_SUCCESS) {
        lava::log()->error("Can't create command buffer for trajectory download!");

        return false;
    }

    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = nullptr;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        lava::log()->error("Can't begin command buffer for trajectory download!");

        return false;
    }

    VkBufferCopy line_copy;
    line_copy.srcOffset = 0;
    line_copy.dstOffset = 0;
    line_copy.size = line_buffer_size;

    vkCmdCopyBuffer(command_buffer, this->integration.value().line_buffer, line_staging_buffer, 1, &line_copy);

    VkBufferCopy indirect_copy;
    indirect_copy.srcOffset = 0;
    indirect_copy.dstOffset = 0;
    indirect_copy.size = indirect_buffer_size;

    vkCmdCopyBuffer(command_buffer, this->integration.value().indirect_buffer, indirect_staging_buffer, 1, &indirect_copy);

    std::array<VkBufferMemoryBarrier, 2> buffer_barriers;
    buffer_barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barriers[0].pNext = nullptr;
    buffer_barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barriers[0].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barriers[0].buffer = line_staging_buffer;
    buffer_barriers[0].offset = 0;
    buffer_barriers[0].size = VK_WHOLE_SIZE;

    buffer_barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barriers[1].pNext = nullptr;
    buffer_barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barriers[1].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barriers[1].buffer = indirect_staging_buffer;
    buffer_barriers[1].offset = 0;
    buffer_barriers[1].size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, buffer_barriers.size(), buffer_barriers.data(), 0, nullptr);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        lava::log()->error("Can't end command buffer for trajectory download!");

        return false;
    }

    // Make sure that no one is reading or writing the line and indirect buffer
    if (vkDeviceWaitIdle(this->device->get()) != VK_SUCCESS) {
        lava::log()->error("Can't wait for device idle during trajectory download!");

        return false;
    }

    VkFenceCreateInfo fence_info;
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = nullptr;
    fence_info.flags = 0;

    VkFence fence = VK_NULL_HANDLE;

    if (vkCreateFence(this->device->get(), &fence_info, lava::memory::instance().alloc(), &fence) != VK_SUCCESS) {
        lava::log()->error("Can't create fence for trajectory download!");

        return false;
    }

    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;

    if (vkQueueSubmit(this->compute_queue.vk_queue, 1, &submit_info, fence) != VK_SUCCESS) {
        lava::log()->error("Can't submit command buffer for trajectory download!");

        return false;
    }

    if (vkWaitForFences(this->device->get(), 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != VK_SUCCESS) {
        lava::log()->error("Can't wait for fence during trajectory download!");

        return false;
    }

    if (vmaInvalidateAllocation(this->device->alloc(), line_staging_buffer_allocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS) {
        lava::log()->error("Can't invalidate staging buffer for line buffer!");

        return false;
    }

    if (vmaInvalidateAllocation(this->device->alloc(), indirect_staging_buffer_allocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS) {
        lava::log()->error("Can't invalidate staging buffer for indirect buffer!");

        return false;
    }

    glm::vec4* line_buffer_pointer = nullptr;
    VkDrawIndirectCommand* indirect_buffer_pointer = nullptr;

    if (vmaMapMemory(this->device->alloc(), line_staging_buffer_allocation, (void**)&line_buffer_pointer) != VK_SUCCESS) {
        lava::log()->error("Can't map line staging buffer!");

        return false;
    }

    if (vmaMapMemory(this->device->alloc(), indirect_staging_buffer_allocation, (void**)&indirect_buffer_pointer) != VK_SUCCESS) {
        lava::log()->error("Can't map indirect staging buffer!");

        return false;
    }

    std::span<glm::vec4> line_buffer_span = std::span<glm::vec4>(line_buffer_pointer, line_buffer_pointer + seed_count * integration_steps);
    std::span<VkDrawIndirectCommand> indirect_buffer_span = std::span<VkDrawIndirectCommand>(indirect_buffer_pointer, indirect_buffer_pointer + seed_count);

    if (!this->write_trajectories(file_name, line_buffer_span, indirect_buffer_span)) {
        return false;
    }

    vmaUnmapMemory(this->device->alloc(), line_staging_buffer_allocation);
    vmaUnmapMemory(this->device->alloc(), indirect_staging_buffer_allocation);

    vkDestroyFence(this->device->get(), fence, lava::memory::instance().alloc());
    vkFreeCommandBuffers(this->device->get(), this->command_pool, 1, &command_buffer);

    vmaDestroyBuffer(this->device->alloc(), line_staging_buffer, line_staging_buffer_allocation);
    vmaDestroyBuffer(this->device->alloc(), indirect_staging_buffer, indirect_staging_buffer_allocation);

    return true;
}

bool Integrator::write_trajectories(const std::string& file_name, std::span<glm::vec4> line_buffer, std::span<VkDrawIndirectCommand> indirect_buffer) {
    if (file_name.empty()) {
        lava::log()->error("File name empty!");

        return false;
    }

    std::fstream length_file;
    std::fstream trajectory_file;

    length_file.open(file_name + "_length.bin", std::ios::out | std::ios::binary);

    if (!length_file.good()) {
        lava::log()->error("Can't open length file!");

        return false;
    }

    trajectory_file.open(file_name + "_trajectory.bin", std::ios::out | std::ios::binary);

    if (!trajectory_file.good()) {
        lava::log()->error("Can't open trajectory file!");

        return false;
    }

    for (VkDrawIndirectCommand& indirect_command : indirect_buffer) {
        uint32_t offset = indirect_command.firstVertex;
        uint32_t length = indirect_command.vertexCount;

        length_file.write((const char*)&length, sizeof(length));
        trajectory_file.write((const char*)(line_buffer.data() + offset), length * sizeof(glm::vec4));
    }

    length_file.close();
    trajectory_file.close();

    return true;
}
