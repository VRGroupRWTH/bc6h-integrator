#pragma once

#include <liblava/lava.hpp>
#include <optional>

class CommandParser {
public:
    CommandParser() = default;

    bool parse_commands(const argh::parser& cmd_line);

    std::optional<uint32_t> get_repetition_count() const;
    std::optional<float> get_repetition_delay() const;

    std::optional<uint32_t> get_work_group_size_x() const;
    std::optional<uint32_t> get_work_group_size_y() const;
    std::optional<uint32_t> get_work_group_size_z() const;

    std::optional<uint32_t> get_seed_dimensions_x() const;
    std::optional<uint32_t> get_seed_dimensions_y() const;
    std::optional<uint32_t> get_seed_dimensions_z() const;

    std::optional<uint32_t> get_integration_steps() const;
    std::optional<uint32_t> get_batch_size() const;
    std::optional<float> get_delta_time() const;

    std::optional<bool> use_explicit_interpolation() const;
    std::optional<bool> use_analytic_dataset() const;

  private:
    std::optional<uint32_t> repetition_count;
    std::optional<float> repetition_delay; //In ms

    std::optional<uint32_t> work_group_size_x;
    std::optional<uint32_t> work_group_size_y;
    std::optional<uint32_t> work_group_size_z;

    std::optional<uint32_t> seed_dimension_x;
    std::optional<uint32_t> seed_dimension_y;
    std::optional<uint32_t> seed_dimension_z;

    std::optional<uint32_t> integration_steps;
    std::optional<uint32_t> batch_size;
    std::optional<float> delta_time;

    std::optional<bool> explicit_interpolation;
    std::optional<bool> analytic_dataset;
};