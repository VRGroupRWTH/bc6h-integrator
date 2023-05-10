#pragma once

#include "dataset.hpp"
#include <liblava/lava.hpp>
#include <memory>
#include <vulkan/vulkan_core.h>

class DatasetView {
  public:
    using Ptr = std::shared_ptr<DatasetView>;

    DatasetView(Dataset::Ptr dataset);
    ~DatasetView();

    bool create(lava::app& app);
    void destroy();
    void render(VkCommandBuffer command_buffer);
    void imgui();

  private:
    bool visible = true;
    Dataset::Ptr dataset;
    int z_slice = 1;
    int t_slice = 1;
    float min = -1.0f;
    float max = 1.0f;

    lava::mesh::ptr quad;

    lava::render_pass::ptr render_pass;
    lava::pipeline_layout::ptr pipeline_layout;
    lava::render_pipeline::ptr pipeline;

    lava::descriptor::ptr descriptor;
    lava::descriptor::pool::ptr descriptor_pool;
    std::vector<VkDescriptorSet> descriptor_sets;
};

DatasetView::Ptr make_dataset_view(Dataset::Ptr dataset);
