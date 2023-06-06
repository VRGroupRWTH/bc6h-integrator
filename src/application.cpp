#include "application.hpp"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

Application::Application(int argc, char* argv[]) : engine("bc6h integrator", {argc, argv}) {
    this->file_dialog.SetTitle("Load Dataset");
    this->file_dialog.SetTypeFilters({".raw", ".ktx"});
}

bool Application::setup() {
    this->engine.platform.on_create_param = [](lava::device::create_param& device_param) {
        device_param.features.largePoints = true;
        device_param.features.wideLines = true;
        device_param.features.fillModeNonSolid = true;
        device_param.features.multiDrawIndirect = true;
        // device_param.queue_family_infos[0].queues[0].priority = 1.0;
        device_param.add_queue(VK_QUEUE_COMPUTE_BIT, 1.0);
        device_param.add_queue(VK_QUEUE_TRANSFER_BIT, 1.0);
    };

    if (!this->engine.setup()) {
        return false;
    }

    this->engine.on_create = [this]() {
        lava::log()->debug("on_create()");
        this->engine.shading.get_pass()->set_clear_color(this->clear_color);
        this->view->create(this->engine);
        this->integrator->create_render_pipeline();
        this->check_ui_scale = true;
        return true;
    };

    this->engine.on_destroy = [this]() {
        lava::log()->debug("on_destroy()");
        this->view->destroy();
        this->integrator->destroy_render_pipeline();
        return true;
    };

    this->engine.camera.set_active(true);

    this->engine.on_update = [this](lava::delta dt) {
        if (this->unload) {
            this->engine.device->wait_for_idle();
            this->integrator->set_dataset(nullptr);
            this->view->set_dataset(nullptr);
            this->dataset = nullptr;
            this->unload = false;
        }
        if (this->engine.camera.activated()) {
            this->engine.camera.update_view(dt, this->engine.input.get_mouse_position());
        }
        this->integrator->check_for_integration();
        return true;
    };

    this->engine.on_process = [this](VkCommandBuffer command_buffer, lava::index frame) {
        if (this->dataset && this->dataset->loaded() && !this->dataset->transitioned) {
            this->dataset->transition_images(command_buffer);
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
    this->view = std::make_shared<DatasetView>();
    this->integrator = Integrator::make();
    if (!this->integrator->create(this->engine)) {
        return false;
    }

    this->engine.camera.position = glm::vec3(0, 0, 2);

    auto& pos_args = this->engine.get_cmd_line().pos_args();
    if (pos_args.size() > 1) {
        std::filesystem::path dataset_path = pos_args.back();
        lava::log()->debug("load dataset: {}", dataset_path.string());
        this->load_dataset(dataset_path);
        this->file_dialog.SetPwd(dataset_path.parent_path());
    }

    return true;
}

void Application::load_dataset(const std::filesystem::path& path) {
    if (path.extension() == ".raw") {
        this->dataset = Dataset::make(this->engine.device, DataSource::open_raw_file(path));
    } else if (path.extension() == ".ktx") {
        this->dataset = Dataset::make(this->engine.device, DataSource::open_ktx_file(path));
    } else {
        lava::log()->warn("Unknown extension: {}", path.extension().string());
        return;
    }
    this->view->set_dataset(this->dataset);
    this->integrator->set_dataset(this->dataset);
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
    this->check_ui_scale = false;
}

void Application::imgui() {
    if (this->check_ui_scale) {
        this->adjust_ui_scale();
    }

    if (ImGui::Begin("General")) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 background_color = this->engine.shading.get_pass()->get_clear_color();
            if (ImGui::ColorEdit4("Background Color", glm::value_ptr(background_color))) {
                this->engine.shading.get_pass()->set_clear_color(background_color);
            }
            ImGui::DragFloat3("Position", glm::value_ptr(this->engine.camera.position));
            ImGui::DragFloat3("Rotation", glm::value_ptr(this->engine.camera.rotation), 0.01f);
            if (ImGui::Button("Reset")) {
                this->engine.camera.reset();
                this->engine.camera.position = glm::vec3(0, 0, 2);
            }
        }
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
    ImGui::Separator();
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    ImGui::End();

    this->file_dialog.Display();
    if (file_dialog.HasSelected()) {
        this->load_dataset(this->file_dialog.GetSelected());
        this->file_dialog.ClearSelected();
    }
}
