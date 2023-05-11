#include "dataset_view.hpp"
#include "shaders.hpp"
#include <imgui.h>
#include <liblava/base/base.hpp>
#include <liblava/block/render_pass.hpp>
#include <liblava/util/math.hpp>
#include <vulkan/vulkan_core.h>

struct Constants {
    float minimum;
    float difference;
    float depth;
};

DatasetView::DatasetView(Dataset::Ptr dataset) : dataset(dataset) {
    if (this->dataset) {
        this->z_slice = (this->dataset->data->dimensions.z + 1) / 2;
    }
}

DatasetView::~DatasetView() { destroy(); }

bool DatasetView::create(lava::app& app) {
    auto device = app.device;
    auto render_target = app.target;

    this->quad = lava::create_mesh(device, lava::mesh_type::quad);

    this->descriptor = lava::descriptor::make();
    this->descriptor->add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    if (!descriptor->create(device)) {
        return false;
    }

    this->descriptor_pool = lava::descriptor::pool::make();
    if (!descriptor_pool->create(device, {
                                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, this->dataset->data->dimensions.w},
                                         },
                                 this->dataset->data->dimensions.w)) {
        return false;
    }

    this->pipeline_layout = lava::pipeline_layout::make();
    this->pipeline_layout->add(this->descriptor);
    this->pipeline_layout->add_push_constant_range({VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants)});
    if (!pipeline_layout->create(device)) {
        destroy();
        return false;
    }

    auto color_attachment = lava::attachment::make(render_target->get_format());
    // color_attachment->set_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR);
    color_attachment->set_final_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    this->pipeline = lava::render_pipeline::make(device, app.pipeline_cache);
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
    if (!this->pipeline->add_shader(dataset_view_frag_cdata, VK_SHADER_STAGE_FRAGMENT_BIT)) {
        lava::log()->error("cannot add fragment shader for dataset view");
        destroy();
        return false;
    }
    this->pipeline->create(app.shading.get_vk_pass());
    this->render_pass = app.shading.get_pass();
    this->render_pass->add_front(this->pipeline);

    this->pipeline->on_process = [this](VkCommandBuffer command_buffer) { this->render(command_buffer); };

    return true;
}

void DatasetView::destroy() {
    // this->pipeline
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
    if (!this->visible) {
        return;
    }

    const bool dataset_loaded = this->dataset->loaded();
    if (dataset_loaded) {
        if (this->descriptor_sets.size() == 0) {
            const auto num_time_slices = this->dataset->data->dimensions.w;
            this->descriptor_sets.reserve(num_time_slices);
            for (unsigned t = 0; t < num_time_slices; ++t) {
                VkDescriptorSet descriptor_set = this->descriptor->allocate(this->descriptor_pool->get());
                const VkWriteDescriptorSet descriptor_write{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptor_set,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &this->dataset->time_slices[t].image_info,
                };
                vkUpdateDescriptorSets(this->descriptor->get_device()->get(), 1, &descriptor_write, 0, nullptr);
                this->descriptor_sets.push_back(descriptor_set);
            }
        }

        Constants c{
            .minimum = this->min,
            .difference = this->max - this->min,
            .depth = static_cast<float>(this->z_slice - 1) / (this->dataset->data->dimensions.z - 1),
        };

        this->pipeline_layout->bind(command_buffer, this->descriptor_sets[this->t_slice - 1]);
        vkCmdPushConstants(command_buffer, this->pipeline_layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants), &c);
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

DatasetView::Ptr make_dataset_view(Dataset::Ptr dataset) {
    return std::make_shared<DatasetView>(dataset);
}
