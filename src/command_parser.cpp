#include "command_parser.hpp"

bool CommandParser::parse_commands(const argh::parser& cmd_line) {
    for (const std::string& flag : cmd_line.flags()) {
        if (flag == "explicit_interpolation") {
            this->explicit_interpolation = true;
        }

        if (flag == "analytic_dataset") {
            this->analytic_dataset = true;
        }
    }

    for (const std::pair<std::string, std::string>& parameter : cmd_line.params()) {
        if (parameter.first == "repetition_count") {
            int32_t repetition_count = atoi(parameter.second.c_str());

            if (repetition_count <= 0) {
                lava::log()->error("Parameter 'repetition_count' smaller or equal to 0!");

                return false;
            }

            this->repetition_count = repetition_count;
        }

        else if (parameter.first == "repetition_delay") {
            float repetition_delay = atof(parameter.second.c_str());

            if (repetition_delay < 0.0) {
                lava::log()->error("Parameter 'repetition_delay' smaller than 0!");

                return false;
            }

            this->repetition_delay = repetition_delay;
        }

        else if (parameter.first == "work_group_size_x") {
            int32_t work_group_size_x = atoi(parameter.second.c_str());

            if (work_group_size_x <= 0) {
                lava::log()->error("Parameter 'work_group_size_x' smaller or equal to 0!");

                return false;
            }

            this->work_group_size_x = work_group_size_x;
        }

        else if (parameter.first == "work_group_size_y") {
            int32_t work_group_size_y = atoi(parameter.second.c_str());

            if (work_group_size_y <= 0) {
                lava::log()->error("Parameter 'work_group_size_y' smaller or equal to 0!");

                return false;
            }

            this->work_group_size_y = work_group_size_y;
        }

        else if (parameter.first == "work_group_size_z") {
            int32_t work_group_size_z = atoi(parameter.second.c_str());

            if (work_group_size_z <= 0) {
                lava::log()->error("Parameter 'work_group_size_z' smaller or equal to 0!");

                return false;
            }

            this->work_group_size_z = work_group_size_z;
        }

        else if (parameter.first == "seed_dimension_x") {
            int32_t seed_dimension_x = atoi(parameter.second.c_str());

            if (seed_dimension_x <= 0) {
                lava::log()->error("Parameter 'seed_dimension_x' smaller or equal to 0!");

                return false;
            }

            this->seed_dimension_x = seed_dimension_x;
        }

        else if (parameter.first == "seed_dimension_y") {
            int32_t seed_dimension_y = atoi(parameter.second.c_str());

            if (seed_dimension_y <= 0) {
                lava::log()->error("Parameter 'seed_dimension_y' smaller or equal to 0!");

                return false;
            }

            this->seed_dimension_y = seed_dimension_y;
        }

        else if (parameter.first == "seed_dimension_z") {
            int32_t seed_dimension_z = atoi(parameter.second.c_str());

            if (seed_dimension_z <= 0) {
                lava::log()->error("Parameter 'seed_dimension_z' smaller or equal to 0!");

                return false;
            }

            this->seed_dimension_z = seed_dimension_z;
        }

        else if (parameter.first == "integration_steps") {
            int32_t integration_steps = atoi(parameter.second.c_str());

            if (integration_steps <= 0) {
                lava::log()->error("Parameter 'integration_steps' smaller or equal to 0!");

                return false;
            }

            this->integration_steps = integration_steps;
        }

        else if (parameter.first == "batch_size") {
            int32_t batch_size = atoi(parameter.second.c_str());

            if (batch_size <= 0) {
                lava::log()->error("Parameter 'batch_size' smaller or equal to 0!");

                return false;
            }

            this->batch_size = batch_size;
        }

       else if (parameter.first == "delta_time") {
            float delta_time = atof(parameter.second.c_str());

            if (delta_time < 0.0) {
                lava::log()->error("Parameter 'delta_time' smaller than 0!");

                return false;
            }

            this->delta_time = delta_time;
        }

        else {
            lava::log()->warn("Unkown parameter '" + parameter.first + "' !");

            return false;
        }
    }

    return true;
}

std::optional<uint32_t> CommandParser::get_repetition_count() const {
    return this->repetition_count;
}

std::optional<float> CommandParser::get_repetition_delay() const {
    return this->repetition_delay;
}

std::optional<uint32_t> CommandParser::get_work_group_size_x() const {
    return this->work_group_size_x;
}

std::optional<uint32_t> CommandParser::get_work_group_size_y() const {
    return this->work_group_size_y;
}

std::optional<uint32_t> CommandParser::get_work_group_size_z() const {
    return this->work_group_size_z;
}

std::optional<uint32_t> CommandParser::get_seed_dimensions_x() const {
    return this->seed_dimension_x;
}

std::optional<uint32_t> CommandParser::get_seed_dimensions_y() const {
    return this->seed_dimension_y;
}

std::optional<uint32_t> CommandParser::get_seed_dimensions_z() const {
    return this->seed_dimension_z;
}

std::optional<uint32_t> CommandParser::get_integration_steps() const {
    return this->integration_steps;
}

std::optional<uint32_t> CommandParser::get_batch_size() const {
    return this->batch_size;
}

std::optional<float> CommandParser::get_delta_time() const {
    return this->delta_time;
}

std::optional<bool> CommandParser::use_explicit_interpolation() const {
    return this->explicit_interpolation;
}

std::optional<bool> CommandParser::use_analytic_dataset() const {
    return this->analytic_dataset;
}