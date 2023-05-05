#pragma once

#include "dataset.hpp"
#include <liblava/lava.hpp>
#include <memory>
#include <vulkan/vulkan_core.h>

class DatasetView {
  public:
    using Ptr = std::shared_ptr<DatasetView>;

    DatasetView(Dataset::Ptr dataset);

    void render(VkCommandBuffer command_buffer);
    void imgui();

  private:
    Dataset::Ptr dataset;
    lava::texture texure;
    int z_slice = 1;
    int t_slice = 1;
};

DatasetView::Ptr make_dataset_view(Dataset::Ptr dataset);
