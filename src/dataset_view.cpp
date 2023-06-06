#include "dataset_view.hpp"
#include "shaders.hpp"
#include "time_slices.hpp"
#include <cstdint>
#include <imgui.h>
#include <liblava/base/base.hpp>
#include <liblava/block/render_pass.hpp>
#include <liblava/util/math.hpp>
#include <vulkan/vulkan_core.h>

struct Constants {
    glm::vec2 resolution; //Resolution in cells / meter
    float minimum;
    float difference;
    float depth;
    float time;
    std::uint32_t channel_count;
};

DatasetView::DatasetView() {
}

DatasetView::~DatasetView() { destroy(); }

bool DatasetView::create(lava::app& app) {
    this->device = app.device;
    this->render_pass = app.shading.get_pass();
    auto render_target = app.target;

    this->quad = lava::create_mesh(this->device, lava::mesh_type::quad);

    this->descriptor = lava::descriptor::make();
    auto binding = lava::descriptor::binding::make(0);
    binding->set_count(3);
    binding->set_type(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    binding->set_stage_flags(VK_SHADER_STAGE_FRAGMENT_BIT);
    this->descriptor->add(binding);
    if (!descriptor->create(this->device)) {
        return false;
    }

    this->descriptor_pool = lava::descriptor::pool::make();
    if (!descriptor_pool->create(this->device, {
                                                   {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TIME_SLICES * 3},
                                               },
                                 MAX_TIME_SLICES * 3)) {
        return false;
    }

    this->pipeline_layout = lava::pipeline_layout::make();
    this->pipeline_layout->add(this->descriptor);
    this->pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants)});
    if (!pipeline_layout->create(this->device)) {
        destroy();
        return false;
    }

    auto color_attachment = lava::attachment::make(render_target->get_format());
    // color_attachment->set_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR);
    color_attachment->set_final_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    this->pipeline = lava::render_pipeline::make(this->device, app.pipeline_cache);
    this->pipeline->set_layout(this->pipeline_layout);
    this->pipeline->add_color_blend_attachment();
    this->pipeline->set_vertex_input_binding({0, sizeof(lava::vertex), VK_VERTEX_INPUT_RATE_VERTEX});
    this->pipeline->set_vertex_input_attributes({
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, lava::to_ui32(offsetof(lava::vertex, position))},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, lava::to_ui32(offsetof(lava::vertex, uv))},
    });

    if (!this->pipeline->add_shader(dataset_view_vert_cdata, VK_SHADER_STAGE_VERTEX_BIT)) {
        lava::log()->error("cannot add vertex shader for dataset view");
        destroy();
        return false;
    }
    if (!this->pipeline->add_shader(dataset_view_image_frag_cdata, VK_SHADER_STAGE_FRAGMENT_BIT)) {
        lava::log()->error("cannot add fragment shader for dataset view");
        destroy();
        return false;
    }
    this->pipeline->create(app.shading.get_vk_pass());
    this->pipeline->on_process = [this](VkCommandBuffer command_buffer) { this->render(command_buffer); };

    this->analytic_pipeline_layout = lava::pipeline_layout::make();
    this->analytic_pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants)});
    if (!analytic_pipeline_layout->create(this->device)) {
        destroy();
        return false;
    }

    this->analytic_pipeline = lava::render_pipeline::make(this->device, app.pipeline_cache);
    this->analytic_pipeline->set_layout(this->analytic_pipeline_layout);
    this->analytic_pipeline->add_color_blend_attachment();
    this->analytic_pipeline->set_vertex_input_binding({0, sizeof(lava::vertex), VK_VERTEX_INPUT_RATE_VERTEX});
    this->analytic_pipeline->set_vertex_input_attributes({
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, lava::to_ui32(offsetof(lava::vertex, position))},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, lava::to_ui32(offsetof(lava::vertex, uv))},
    });

    if (!this->analytic_pipeline->add_shader(dataset_view_vert_cdata, VK_SHADER_STAGE_VERTEX_BIT)) {
        lava::log()->error("cannot add vertex shader for dataset view");
        destroy();
        return false;
    }
    if (!this->analytic_pipeline->add_shader(dataset_view_analytic_frag_cdata, VK_SHADER_STAGE_FRAGMENT_BIT)) {
        lava::log()->error("cannot add fragment shader for dataset view");
        destroy();
        return false;
    }
    this->analytic_pipeline->create(app.shading.get_vk_pass());
    this->analytic_pipeline->on_process = [this](VkCommandBuffer command_buffer) { this->render(command_buffer); };

    return true;
}

void DatasetView::destroy() {
    this->free_descriptor_sets();
    if (this->descriptor_pool) {
        this->descriptor_pool->destroy();
        this->descriptor_pool = nullptr;
    }
    if (this->descriptor) {
        this->descriptor->destroy();
        this->descriptor = nullptr;
    }
    if (this->pipeline_layout) {
        this->pipeline_layout->destroy();
        this->pipeline_layout = nullptr;
    }
    if (this->pipeline) {
        this->render_pass->remove(this->pipeline);
        this->pipeline->destroy();
        this->pipeline = nullptr;
    }
}

void DatasetView::render(VkCommandBuffer command_buffer) {
    if (!this->visible || !this->dataset) {
        return;
    }

    const bool dataset_loaded = this->dataset->loaded();
    if (dataset_loaded) {
        if (this->descriptor_sets.size() == 0) {
            this->allocate_descriptor_sets();
        }

        Constants c{
            .resolution = glm::vec2(this->dataset->data->resolution),
            .minimum = this->min,
            .difference = this->max - this->min,
            .channel_count = this->dataset->data->channel_count,
        };

        if (this->dataset->data->format != DataSource::Format::Analytic) {
            c.depth = static_cast<float>(this->z_slice - 1) / (this->dataset->data->dimensions.z - 1);
            c.time = static_cast<float>(this->t_slice - 1) / (this->dataset->data->dimensions.w - 1);
        }

        else {
            c.depth = this->z_slice * this->dataset->data->dimensions_in_meters().z;
            c.time = this->t_slice * this->dataset->data->time_in_seconds();
        }

        if (this->dataset->data->format != DataSource::Format::Analytic) {
            this->pipeline_layout->bind(command_buffer, this->descriptor_sets[this->t_slice - 1]);    
            vkCmdPushConstants(command_buffer, this->pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants), &c);
        }

        else {
            vkCmdPushConstants(command_buffer, this->analytic_pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants), &c);
        }
        
        this->quad->bind_draw(command_buffer);
    }
}

void DatasetView::imgui() {
    ImGui::Checkbox("Visible", &this->visible);
    ImGui::DragInt("Z", &this->z_slice, 1.0, 1, dataset->data->dimensions.z);
    ImGui::DragInt("T", &this->t_slice, 1.0, 1, dataset->data->dimensions.w);
    ImGui::DragFloat("Minimum Value", &this->min);
    ImGui::DragFloat("Maximum Value", &this->max);
}


void DatasetView::set_dataset(Dataset::Ptr dataset) {
    this->dataset = dataset;
    this->free_descriptor_sets();

    this->render_pass->remove(this->pipeline);
    this->render_pass->remove(this->analytic_pipeline);

    if (dataset->data->format != DataSource::Format::Analytic) {
        this->render_pass->add_front(this->pipeline);
    }

    else {
        this->render_pass->add_front(this->analytic_pipeline);   
    }
}

void DatasetView::allocate_descriptor_sets() {
    assert(this->descriptor);
    assert(this->descriptor_pool);
    assert(this->descriptor_sets.size() == 0);

    const auto num_time_slices = this->dataset->data->dimensions.w;
    this->descriptor_sets.reserve(num_time_slices);
    std::vector<VkWriteDescriptorSet> descriptor_set_writes;
    for (unsigned t = 0; t < num_time_slices; ++t) {
        auto descriptor_set = this->descriptor->allocate(this->descriptor_pool->get());
        this->descriptor_sets.push_back(descriptor_set);

        const bool single_channel = this->dataset->data->channel_count == 1;

        descriptor_set_writes.push_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &this->dataset->get_image(0, t).image_info
        });
        descriptor_set_writes.push_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &this->dataset->get_image(single_channel ? 0 : 1, t).image_info
        });
        descriptor_set_writes.push_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &this->dataset->get_image(single_channel ? 0 : 2, t).image_info
        });
    }
    this->device->vkUpdateDescriptorSets(descriptor_set_writes.size(), descriptor_set_writes.data());
}

void DatasetView::free_descriptor_sets() {
    if (!this->descriptor || !this->descriptor_pool || this->descriptor_sets.size() == 0) {
        return;
    }

    if (!this->descriptor->free(this->descriptor_sets, this->descriptor_pool->get())) {
        lava::log()->error("failed to free descriptor sets");
    }
}
