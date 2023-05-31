#pragma once

#include <filesystem>
#include <fstream>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <ios>
#include <memory>
#include <vector>

struct DataSource {
    using Ptr = std::shared_ptr<DataSource>;

    enum class Format {
        Float16,
        Float32,
        BC6H,
    };

    static Ptr open_raw_file(const std::filesystem::path& path);
    static Ptr open_ktx_file(const std::filesystem::path& path);

    // std::streampos get_offset(int z, int t);
    void read(void* buffer);
    void read_time_slice(int c, int t, void* buffer);
    void imgui();
    glm::vec4 dimensions_in_meters_and_seconds() const { return glm::vec4(this->dimensions) / this->resolution; }
    glm::vec3 dimensions_in_meters() const { return glm::vec3(this->dimensions) / glm::vec3(this->resolution); }
    float time_in_seconds() const { return this->dimensions.w / this->resolution.w; }

    std::string filename;
    Format format;
    std::ifstream file;
    glm::uvec4 dimensions;
    unsigned channel_count;
    glm::vec4 resolution = glm::vec4(1.0, 1.0, 1.0, 1.0);
    std::streampos data_offset;
    std::streamsize data_size;
    std::streamsize time_slice_size;
    std::streamsize z_slice_size;
    std::streamsize channel_size;
};
