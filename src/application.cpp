#include "application.hpp"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

Application::Application(int argc, char* argv[]) : engine("bc6h integrator", {argc, argv}) {
    this->file_dialog.SetPwd("/home/so225523/Data/BC6/Data/abc/");
    this->file_dialog.SetTitle("Load Dataset");
    this->file_dialog.SetTypeFilters({".raw", ".ktx"});
}

bool Application::setup() {

    this->engine.platform.on_create_param = [](lava::device::create_param& device_param) {
        device_param.features.fillModeNonSolid = true;
        device_param.features.multiDrawIndirect = true;
        device_param.queue_family_infos[0].queues[0].priority = 0.5;
        device_param.add_queue(VK_QUEUE_COMPUTE_BIT, 1.0);
    };

    if (!this->engine.setup()) {
        return false;
    }

    this->engine.camera.set_active(true);
    this->adjust_ui_scale();

    this->engine.shading.get_pass()->set_clear_color(this->clear_color);
    this->engine.on_update = [this](lava::delta dt) {
        if (this->unload) {
            this->engine.device->wait_for_idle();
            this->integrator->prepare_for_dataset(nullptr);
            this->view = nullptr;
            this->dataset = nullptr;
            this->unload = false;
        }
        if (this->engine.camera.activated()) {
            this->engine.camera.update_view(dt, this->engine.input.get_mouse_position());
        }
        return true;
    };

    this->engine.on_process = [this](VkCommandBuffer command_buffer, lava::index frame) {
        if (this->dataset) {
            this->dataset->transfer_if_necessary(command_buffer);
        }
    };

    this->engine.imgui.on_draw = [this]() {
        this->imgui();
    };

    this->input_callback.on_key_event = [this](const lava::key_event& event) {
        if (event.pressed(lava::key::escape)) {
            this->engine.shut_down();
            return true;
        }
        return false;
    };

    this->engine.input.add(&this->input_callback);

    this->integrator = Integrator::make();
    if (!this->integrator->create(this->engine)) {
        return false;
    }

    // this->engine.on_destroy = [this]() {
    //     this->view.reset();
    // };

    return true;
}

void Application::load_dataset(const std::filesystem::path& path) {
    // state->dataset = Dataset::create(device, DataSource::open_raw_file("/home/so225523/Data/100x100x100x100.raw", glm::uvec4(100, 100, 100, 100)));
    this->dataset = Dataset::create(this->engine.device, DataSource::open_ktx_file(path));
    if (this->dataset) {
        this->view = make_dataset_view(this->dataset);
        if (this->view) {
            this->view->create(this->engine);
        }
        this->integrator->prepare_for_dataset(this->dataset);
    }
}

int Application::run() {
    return this->engine.run();
}

void Application::adjust_ui_scale() {
    float dpi_scale_x;
    float dpi_scale_y;
    glfwGetWindowContentScale(this->engine.window.get(), &dpi_scale_x, &dpi_scale_y);
    float font_scale = std::max(dpi_scale_x, dpi_scale_y);
    if (font_scale <= 0) {
        font_scale = 1.0f;
    }

    lava::log()->info("set imgui scale to {} (content scale was {{{},{}}})", font_scale, dpi_scale_x, dpi_scale_y);
    ImGui::GetIO().FontGlobalScale = font_scale;
}

void Application::imgui() {
    if (ImGui::Begin("General")) {
        if (ImGui::CollapsingHeader("Dataset", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->dataset) {
                this->dataset->imgui();
                if (ImGui::Button("Unload")) {
                    this->unload = true;
                }
            } else {
                if (ImGui::Button("Load")) {
                    this->file_dialog.Open();
                }
            }
        }

        if (this->dataset && this->dataset->loaded()) {
            if (this->view && ImGui::CollapsingHeader("Debug View", ImGuiTreeNodeFlags_DefaultOpen)) {
                this->view->imgui();
            }
            if (this->integrator && ImGui::CollapsingHeader("Integration", ImGuiTreeNodeFlags_DefaultOpen)) {
                this->integrator->imgui();
            }
        }
    }
    ImGui::End();

    this->file_dialog.Display();
    if (file_dialog.HasSelected()) {
        this->load_dataset(this->file_dialog.GetSelected());
        this->file_dialog.ClearSelected();
    }
}