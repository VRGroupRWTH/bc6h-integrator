#include "dataset.hpp"
#include <fstream>
#include <spdlog/spdlog.h>

struct KtxHeader {
    std::byte identifier[12];
    std::uint32_t endianess;
    std::uint32_t gl_type;
    std::uint32_t gl_type_size;
    std::uint32_t gl_format;
    std::uint32_t gl_internal_format;
    std::uint32_t gl_base_internal_format;
    std::uint32_t pixel_width;
    std::uint32_t pixel_height;
    std::uint32_t pixel_depth;
    std::uint32_t number_of_array_elements;
    std::uint32_t number_of_faces;
    std::uint32_t number_of_mipmap_levels;
    std::uint32_t bytes_of_key_value_data;
} header;

std::shared_ptr<Dataset> Dataset::load_ktx(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);

    KtxHeader header;

    if (!file) {
        spdlog::error("failed to open `{}`", path.string());
    }
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    std::unordered_map<std::string, std::vector<char>> key_value_data;
    {
        const auto end_of_key_value_data = file.tellg() + std::fstream::pos_type(header.bytes_of_key_value_data);
        std::vector<char> read_buffer;
        while (file.tellg() < end_of_key_value_data) {
            std::uint32_t key_and_value_byte_size;
            file.read(reinterpret_cast<char*>(&key_and_value_byte_size), sizeof(key_and_value_byte_size));
            read_buffer.resize(key_and_value_byte_size);
            file.read(read_buffer.data(), key_and_value_byte_size);

            auto null_terminator = std::find(read_buffer.begin(), read_buffer.end(), '\0');
            assert(null_terminator != read_buffer.end());
            key_value_data.insert(std::make_pair(std::string(read_buffer.begin(), null_terminator), std::vector(std::next(null_terminator), read_buffer.end())));
            std::array<char, 3> padding;
            file.read(padding.data(), 3 - ((key_and_value_byte_size + 3) % 4));
        }
    }

    if (!key_value_data.contains("Dimensions")) {
        spdlog::error("dimensions missing in dataset");
        return nullptr;
    }

    std::uint32_t image_size;
    file.read(reinterpret_cast<char*>(&image_size), sizeof(image_size));

    glm::uvec4 dimensions = *reinterpret_cast<glm::ivec4*>(key_value_data.at("Dimensions").data());

    assert(image_size % dimensions.w == 0);
    const std::uint32_t time_slice_size = image_size / dimensions.w;

    assert(time_slice_size % dimensions.z == 0);
    const std::uint32_t z_size_in_bytes = time_slice_size / dimensions.z;

    spdlog::info("ktx dataset loaded (file: {}, dimensions: {}x{}x{}x{})", path.string(), dimensions.x, dimensions.y, dimensions.z, dimensions.w);

    auto dataset = std::make_shared<Dataset>(Dataset{
        .dimensions = dimensions,
        .data_offset = file.tellg(),
        .time_size_in_bytes = time_slice_size,
        .z_size_in_bytes = z_size_in_bytes,
    });
    dataset->file.swap(file);
    return dataset;
}


std::streampos Dataset::get_offset(int z, int t) {
    return this->data_offset + t * this->time_size_in_bytes + z * this->z_size_in_bytes;
}

void Dataset::read_z_slice(int z, int t, void* out) {
    this->file.seekg(get_offset(z, t));
    this->file.read(reinterpret_cast<char*>(out), this->z_size_in_bytes);
}
