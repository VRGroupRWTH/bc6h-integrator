#include "dataset.hpp"
#include "imgui.h"
#include "liblava/lava.hpp"
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <glm/fwd.hpp>
#include <iterator>
#include <liblava/file/file_utils.hpp>
#include <liblava/frame/input.hpp>
#include <liblava/resource/mesh.hpp>
#include <liblava/resource/texture.hpp>
#include <memory>
#include <shaderc/shaderc.hpp>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

#include "imfilebrowser.hpp"

template <typename Rep, typename Period>
std::uint64_t to_ms(std::chrono::duration<Rep, Period> duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

struct DebugDataset {
    lava::texture::ptr texure;
};

int main(int argc, char* argv[]) {
    lava::engine app("imgui demo", {argc, argv});

    if (!app.setup())
        return lava::error::not_ready;

    auto quad = lava::create_mesh(app.device, lava::mesh_type::quad);

    lava::pipeline_layout::ptr layout;
    lava::render_pipeline::ptr pipeline;

    lava::descriptor::ptr descriptor;
    lava::descriptor::pool::ptr descriptor_pool;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    auto dataset = Dataset::load_ktx("/home/so225523/Data/BC6/Data/abc/abc2_highest_norm.ktx");
    auto view = 
    ImGui::FileBrowser file_dialog;
    file_dialog.SetPwd("/home/so225523/Data/BC6/Data/abc/");
    file_dialog.SetTitle("Load Dataset");
    file_dialog.SetTypeFilters({".ktx"});

    std::optional<DebugDataset> debug_dataset;

    ImGui::GetIO().FontGlobalScale = 2.0f;

    app.on_create = [&]() {
        layout = lava::pipeline_layout::make();
        if (!layout->create(app.device)) {
            return false;
        }

        pipeline = lava::render_pipeline::make(app.device, app.pipeline_cache);
        pipeline->set_layout(layout);

        if (!pipeline->add_shader(lava::file_data("/home/so225523/Code/bc6h-integrator/build/debug_vert.spirv"), VK_SHADER_STAGE_VERTEX_BIT)) {
            return false;
        }

        if (!pipeline->add_shader(lava::file_data("/home/so225523/Code/bc6h-integrator/build/debug_frag.spirv"), VK_SHADER_STAGE_FRAGMENT_BIT)) {
            return false;
        }

        pipeline->add_color_blend_attachment();

        pipeline->set_vertex_input_binding({0, sizeof(lava::vertex), VK_VERTEX_INPUT_RATE_VERTEX});

        pipeline->set_vertex_input_attributes({
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, lava::to_ui32(offsetof(lava::vertex, position))},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, lava::to_ui32(offsetof(lava::vertex, uv))},
        });

        descriptor = lava::descriptor::make();
        descriptor->add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        if (!descriptor->create(app.device)) {
            return false;
        }

        descriptor_pool = lava::descriptor::pool::make();
        if (!descriptor_pool->create(app.device, {
                                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                                 })) {
            return false;
        }

        layout = lava::pipeline_layout::make();
        layout->add(descriptor);

        if (!layout->create(app.device))
            return false;

        pipeline->set_layout(layout);

        descriptor_set = descriptor->allocate(descriptor_pool->get());

        pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
            if (debug_dataset.has_value()) {
                debug_dataset->texure->stage(cmd_buf);
                layout->bind(cmd_buf, descriptor_set);
                quad->bind_draw(cmd_buf);
            }
        };

        lava::render_pass::ptr render_pass = app.shading.get_pass();

        if (!pipeline->create(render_pass->get()))
            return false;

        render_pass->add_front(pipeline);

        return true;
    };
    app.imgui.on_draw = [&]() {
        if (ImGui::Begin("General")) {
            if (ImGui::CollapsingHeader("Dataset", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (dataset) {
                    ImGui::InputInt4("Dimensions", &dataset->dimensions.x, ImGuiInputTextFlags_ReadOnly);
                    bool debug = debug_dataset.has_value();
                    bool load_texture = false;
                    if (ImGui::Checkbox("Debug", &debug)) {
                        if (debug) {
                            debug_dataset.emplace();
                            debug_dataset->z_slice = 1;
                            debug_dataset->t_slice = 1;
                            debug_dataset->texure = lava::texture::make();
                            if (!debug_dataset->texure->create(app.device, glm::uvec2(dataset->dimensions), VK_FORMAT_BC6H_SFLOAT_BLOCK)) {
                                spdlog::error("failed to create texture");
                            }
                            const VkWriteDescriptorSet write_desc_sampler{
                                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = descriptor_set,
                                .dstBinding = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .pImageInfo = debug_dataset->texure->get_descriptor_info(),
                            };

                            app.device->vkUpdateDescriptorSets({write_desc_sampler});
                        } else {
                            debug_dataset.reset();
                        }
                        load_texture = true;
                    }
                    if (debug) {
                        if (ImGui::DragInt("Z", &debug_dataset->z_slice, 1.0, 1, dataset->dimensions.z)) {
                            load_texture = true;
                        }
                        if (ImGui::DragInt("T", &debug_dataset->t_slice, 1.0, 1, dataset->dimensions.w)) {
                            load_texture = true;
                        }
                        if (load_texture) {
                            lava::timer sw;
                            std::vector<std::byte> data(dataset->z_size_in_bytes);
                            dataset->read_z_slice(debug_dataset->z_slice - 1, debug_dataset->t_slice - 1, data.data());
                            debug_dataset->texure->upload(data.data(), data.size());
                            spdlog::info("loaded slice z={}, t={} in {}ms", debug_dataset->z_slice, debug_dataset->t_slice, to_ms(sw.elapsed()));
                        }
                    }
                    if (ImGui::Button("Unload")) {
                        dataset.reset();
                    }
                } else {
                    if (ImGui::Button("Browse")) {
                        file_dialog.Open();
                    }
                }
            }
        }
        ImGui::End();

        file_dialog.Display();
        if (file_dialog.HasSelected()) {
            dataset = Dataset::load_ktx(file_dialog.GetSelected());
            file_dialog.ClearSelected();
        }
    };

    lava::input_callback input_callback;
    input_callback.on_key_event = [&app](const lava::key_event& event) {
        if (event.pressed(lava::key::escape)) {
            app.shut_down();
            return true;
        }
        return false;
    };

    app.input.add(&input_callback);

    app.on_destroy = [&]() {
        pipeline->destroy();
        layout->destroy();
    };

    return app.run();
}
