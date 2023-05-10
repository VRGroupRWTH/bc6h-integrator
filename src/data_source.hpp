#pragma once

#include <filesystem>
#include <fstream>
#include <glm/vec4.hpp>
#include <ios>
#include <memory>
#include <vector>

struct DataSource {
    using Ptr = std::shared_ptr<DataSource>;

    enum class Format {
        Float32,
        BC6H,
    };

    static Ptr open_raw_file(const std::filesystem::path& path, glm::uvec4 dimensions);
    static Ptr open_ktx_file(const std::filesystem::path& path);

    std::streampos get_offset(int z, int t);
    void read(void* buffer);
    void read_z_slice(int z, int t, void* out);
    void imgui();

    std::string filename;
    Format format;
    std::ifstream file;
    glm::uvec4 dimensions;
    std::streampos data_offset;
    std::streamoff data_size;
    std::streamoff time_slice_size;
    std::streamoff z_slice_size;
};
