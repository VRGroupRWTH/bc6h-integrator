#pragma once

#include "dataset.hpp"
#include "liblava/resource/buffer.hpp"
#include <iterator>
#include <liblava/block/compute_pipeline.hpp>
#include <liblava/block/render_pass.hpp>
#include <optional>
#include <span>
#include <array>

struct Constants;

class Integrator {
  public:
    using Ptr = std::shared_ptr<Integrator>;

    static Ptr make() { return std::make_shared<Integrator>(); }

    Integrator();
    ~Integrator() { this->destroy(); }

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
    bool create_max_velocity_magnitude_buffer();
    bool create_command_pool();
    bool create_query_pool();

    bool create_descriptor();
    void destroy_descriptor();

    bool create_seeding_pipeline();
    void destroy_seeding_pipeline();

    bool create_integration_pipeline();
    void destroy_integration_pipeline();

    void write_dataset_to_descriptor();
    void destroy_integration();

    void reset_dataset();
    bool prepare_integration();
    bool integrate();
    bool perform_seeding(VkCommandBuffer command_buffer, VkFence fence, lava::timer& timer, Constants& constants);
    bool perform_integration(VkCommandBuffer command_buffer, VkFence fence, lava::timer& timer, Constants& constants);
    bool submit_and_measure_command(VkCommandBuffer command_buffer, VkFence fence, lava::timer& timer, std::function<void()> function);

    bool download_trajectories(const std::string& file_name);
    bool write_trajectories(const std::string& file_name, std::span<glm::vec4> line_buffer, std::span<VkDrawIndirectCommand> indirect_buffer);

    lava::app* app;
    Dataset::Ptr dataset;

    struct Integration {
        VkBuffer line_buffer;
        VmaAllocation line_buffer_allocation;

        VkBuffer indirect_buffer;
        VmaAllocation indirect_buffer_allocation;

        unsigned int seed_count;
        unsigned int integration_steps;

        unsigned int current_batch;
        unsigned int batch_count;

        double gpu_time = 0.0;
        double cpu_time = 0.0;

        bool complete = false;

        bool create_buffers(glm::uvec3 seed_spawn, std::uint32_t integration_steps, lava::device_p device, const lava::queue& compute_queue);
        void update_descriptor_set(lava::device_p device, VkDescriptorSet descriptor_set);
        void destroy(lava::device_p device, VkCommandPool command_pool);
    };
    std::optional<Integration> integration;

    std::ofstream log_file;
    unsigned int run;

    // Compute
    lava::buffer::ptr max_velocity_magnitude_buffer;
    lava::device_p device;
    lava::queue compute_queue;

    lava::descriptor::ptr descriptor;
    lava::descriptor::pool::ptr descriptor_pool;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkQueryPool query_pool = VK_NULL_HANDLE;

    lava::pipeline_layout::ptr seeding_pipeline_layout;
    lava::compute_pipeline::ptr seeding_pipeline;

    lava::pipeline_layout::ptr integration_pipeline_layout;
    lava::compute_pipeline::ptr integration_pipeline;
    bool recreate_integration_pipeline = true;

    // Rendering
    lava::pipeline_layout::ptr render_pipeline_layout;
    lava::render_pipeline::ptr render_pipeline;
    float line_width = 1.0f;
    glm::vec4 line_color = glm::vec4(0.0, 0.0, 0.0, 1.0);
    float scaling = 1.0f;
    uint32_t line_colormap = 0;
    bool line_colormap_invert = false;
    float line_velocity_min = 0.0f;
    float line_velocity_max = 1.0f;

    // Integration settings
    glm::uvec3 work_group_size = {8, 1, 1};
    glm::uvec3 seed_spawn = {20, 20, 20};
    float delta_time = 0.1;
    unsigned int integration_steps = 10000;
    unsigned int batch_size = 100;
    bool explicit_interpolation = false;
    bool analytic_dataset = false;
    bool should_integrate = false;

    std::thread integration_thread;

    //Download
    std::array<char, 512> download_file_name;
};
