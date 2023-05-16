#pragma once

#include "dataset.hpp"
#include "liblava/resource/buffer.hpp"
#include <iterator>
#include <liblava/block/compute_pipeline.hpp>
#include <liblava/block/render_pass.hpp>
#include <optional>

class Integrator {
  public:
    using Ptr = std::shared_ptr<Integrator>;

    static Ptr make() { return std::make_shared<Integrator>(); }

    void render(VkCommandBuffer command_buffer);
    void imgui();
    void set_dataset(Dataset::Ptr dataset);
    bool integration_in_progress();
    void check_for_integration();

    bool create(lava::app& app);
    void destroy();

    bool create_render_pipeline();
    void destroy_render_pipeline();

  private:
    bool create_progress_buffer();
    bool create_command_pool();
    bool create_query_pool();
    bool create_descriptor();
    bool create_compute_pipeline();

    void write_dataset_to_descriptor();
    void destroy_integration();

    void reset_dataset();
    bool integrate();

    lava::app* app;
    Dataset::Ptr dataset;

    struct Integration {
        VkBuffer line_buffer;
        VmaAllocation line_buffer_allocation;

        VkBuffer indirect_buffer;
        VmaAllocation indirect_buffer_allocation;

        VkCommandBuffer command_buffer;
        VkFence command_buffer_fence;

        unsigned int seed_count;
        unsigned int integration_steps;

        bool create_buffers(glm::uvec3 seed_spawn, std::uint32_t integration_steps, lava::device_p device, const lava::queue& compute_queue);
        void update_descriptor_set(lava::device_p device, VkDescriptorSet descriptor_set);
        void destroy(lava::device_p device, VkCommandPool command_pool);
    };
    std::optional<Integration> integration;

    // Compute
    lava::buffer::ptr progress_buffer;
    lava::device_p device;
    lava::queue compute_queue;

    lava::descriptor::ptr descriptor;
    lava::descriptor::pool::ptr descriptor_pool;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkQueryPool query_pool = VK_NULL_HANDLE;

    lava::pipeline_layout::ptr spawn_seeds_pipeline_layout;
    lava::compute_pipeline::ptr spawn_seeds_pipeline;

    // Rendering
    lava::pipeline_layout::ptr render_pipeline_layout;
    lava::render_pipeline::ptr render_pipeline;
    float line_width = 1.0f;
    glm::vec4 line_color = glm::vec4(0.0, 0.0, 0.0, 1.0);

    // Integration settings
    glm::uvec3 seed_spawn = {10, 10, 10};
    unsigned int integration_steps = 100;
    bool should_integrate = false;
};
