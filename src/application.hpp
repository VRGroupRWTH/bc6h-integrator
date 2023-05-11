#pragma once

#include "dataset.hpp"
#include "dataset_view.hpp"
#include "integrator.hpp"
#include "imgui.h"
#include <filesystem>
#include <glm/fwd.hpp>
#include <liblava/engine/engine.hpp>

// Needs to be included after imgui.h, so separate it
#include "imfilebrowser.hpp"

class Application {
  public:
    Application(int argc, char* argv[]);

    bool setup();
    void load_dataset(const std::filesystem::path& path);
    int run();

  private:
    lava::engine engine;
    Dataset::Ptr dataset;
    DatasetView::Ptr view;
    Integrator::Ptr integrator;
    ImGui::FileBrowser file_dialog;
    bool unload = false;
    glm::vec3 clear_color = glm::vec3(1.0, 1.0, 1.0);
    lava::input_callback input_callback;

    void adjust_ui_scale();
    void imgui();
};
