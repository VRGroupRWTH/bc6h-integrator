#pragma once

#include <filesystem>
#include <fstream>
#include <glm/vec4.hpp>
#include <ios>
#include <memory>
#include <vector>

struct Dataset {
    using Ptr = std::shared_ptr<Dataset>;

    enum class Format {
        Float32,
        BC6H,
    };

    static Ptr load_raw(const std::filesystem::path& path, glm::uvec4 dimensions);
    static Ptr load_ktx(const std::filesystem::path& path);

    std::streampos get_offset(int z, int t);
    void read_z_slice(int z, int t, void* out);

    std::ifstream file;
    glm::ivec4 dimensions;
    std::streampos data_offset;
    std::streamoff time_size_in_bytes;
    std::streamoff z_size_in_bytes;
};
