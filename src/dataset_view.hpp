#pragma once

#include "dataset.hpp"
#include <liblava/lava.hpp>
#include <memory>
#include <vulkan/vulkan_core.h>

class DatasetView {
  public:
    using Ptr = std::shared_ptr<DatasetView>;

    DatasetView();
    ~DatasetView();

    bool create(lava::app& app);
    void destroy();

    void render(VkCommandBuffer command_buffer);
    void imgui();
    void set_dataset(Dataset::Ptr dataset);

  private:
    bool visible = true;
    Dataset::Ptr dataset;
    int z_slice = 1;
    int t_slice = 1;
    float min = -1.0f;
    float max = 1.0f;

    lava::device_p device;
    lava::mesh::ptr quad;

    lava::render_pass::ptr render_pass;
    lava::pipeline_layout::ptr pipeline_layout;
    lava::render_pipeline::ptr pipeline;
    lava::pipeline_layout::ptr analytic_pipeline_layout;
    lava::render_pipeline::ptr analytic_pipeline;

    lava::descriptor::ptr descriptor;
    lava::descriptor::pool::ptr descriptor_pool;
    std::vector<VkDescriptorSet> descriptor_sets;

    void allocate_descriptor_sets();
    void free_descriptor_sets();
};
