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
        Analytic
    };

    static Ptr open_raw_file(const std::filesystem::path& path);
    static Ptr open_ktx_file(const std::filesystem::path& path);
    static Ptr open_analytic();

    // std::streampos get_offset(int z, int t);
    void read(void* buffer);
    void read_time_slice(int c, int t, void* buffer);
    void imgui();

    std::string filename;
    Format format;
    std::ifstream file;
    glm::uvec4 dimensions;
    unsigned channel_count;
    std::streampos data_offset;
    std::streamsize data_size;
    std::streamsize time_slice_size;
    std::streamsize z_slice_size;
    std::streamsize channel_size;
};
