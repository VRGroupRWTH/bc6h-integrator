#pragma once

#include "dataset.hpp"
#include "liblava/resource/buffer.hpp"
#include <liblava/block/compute_pipeline.hpp>
#include <liblava/block/render_pass.hpp>
#include <optional>

class Integrator {
  public:
    using Ptr = std::shared_ptr<Integrator>;

    static Ptr make() { return std::make_shared<Integrator>(); }

    bool create(lava::app& app);
    void destroy();
    void render(VkCommandBuffer command_buffer);
    void imgui();
    void prepare_for_dataset(Dataset::Ptr dataset);
    bool integration_in_progress() { return this->integration.has_value(); }

  private:
    void reset_dataset();
    bool integrate();

    struct Integration {
        VkBuffer line_buffer;
        VmaAllocation line_buffer_allocation;

        VkBuffer indirect_buffer;
        VmaAllocation indirect_buffer_allocation;

        VkDescriptorSet descriptor_set;
        unsigned int seed_count;
        unsigned int integration_steps;

        VkCommandPool command_pool;
    };
    lava::app* app;
    std::optional<Integration> integration;
    Dataset::Ptr dataset;

    // Compute
    lava::buffer::ptr progress_buffer;
    lava::device_p device;
    lava::queue compute_queue;

    lava::descriptor::ptr descriptor;
    lava::descriptor::pool::ptr descriptor_pool;

    lava::pipeline_layout::ptr spawn_seeds_pipeline_layout;
    lava::compute_pipeline::ptr spawn_seeds_pipeline;

    // Rendering
    lava::pipeline_layout::ptr render_pipeline_layout;
    lava::render_pipeline::ptr render_pipeline;
    // lava::

    // lava::descriptor::ptr descriptor;
    // lava::descriptor::pool::ptr descriptor_pool;
    // std::vector<VkDescriptorSet> descriptor_sets;


    // Integration settings
    glm::uvec3 seed_spawn = {10, 10, 10};
    unsigned int integration_steps = 100;
};
