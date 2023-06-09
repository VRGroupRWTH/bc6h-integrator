#include "data_source.hpp"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <liblava/util/log.hpp>
#include <spdlog/spdlog.h>

std::shared_ptr<DataSource> DataSource::open_raw_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        spdlog::error("failed to open `{}`", path.string());
        return nullptr;
    }

    glm::uvec4 dimensions;
    file.read(reinterpret_cast<char*>(glm::value_ptr(dimensions)), sizeof(dimensions));

    const std::streamsize depth_slice_size_f32 = dimensions.x * dimensions.y * sizeof(float);
    const std::streamsize time_slice_size_f32 = depth_slice_size_f32 * dimensions.z;
    const std::streamsize channel_size_f32 = time_slice_size_f32 * dimensions.w;
    const std::streamsize data_size_f32 = channel_size_f32 * 3; // 3 = channel_count
    const auto expected_file_size_f32 = data_size_f32 + sizeof(glm::vec4);

    const std::streamsize depth_slice_size_f16 = dimensions.x * dimensions.y * 2;
    const std::streamsize time_slice_size_f16 = depth_slice_size_f16 * dimensions.z;
    const std::streamsize channel_size_f16 = time_slice_size_f16 * dimensions.w;
    const std::streamsize data_size_f16 = channel_size_f16 * 3; // 3 = channel_count
    const auto expected_file_size_f16 = data_size_f16 + sizeof(glm::vec4);

    const std::streamsize depth_slice_size_bc6h = ((dimensions.x + 3) / 4) *  ((dimensions.y + 3) / 4) * 16;
    const std::streamsize time_slice_size_bc6h = depth_slice_size_bc6h * dimensions.z;
    const std::streamsize channel_size_bc6h = time_slice_size_bc6h * dimensions.w;
    const std::streamsize data_size_bc6h = channel_size_bc6h * 1; // 1 = channel_count
    const auto expected_file_size_bc6h = data_size_bc6h + sizeof(glm::vec4);

    file.seekg(0, std::ios::end);
    const auto file_size = file.tellg();

    std::shared_ptr<DataSource> data_source;
    if (file_size == expected_file_size_f16) {
        data_source = std::make_shared<DataSource>(DataSource{
            .filename = path.string(),
            .format = Format::Float16,
            .dimensions = dimensions,
            .channel_count = 3,
            .data_offset = sizeof(glm::uvec4),
            .data_size = data_size_f16,
            .time_slice_size = time_slice_size_f16,
            .z_slice_size = depth_slice_size_f16,
            .channel_size = channel_size_f16,
        });
    } else if (file_size == expected_file_size_f32) {
        data_source = std::make_shared<DataSource>(DataSource{
            .filename = path.string(),
            .format = Format::Float32,
            .dimensions = dimensions,
            .channel_count = 3,
            .data_offset = sizeof(glm::uvec4),
            .data_size = data_size_f32,
            .time_slice_size = time_slice_size_f32,
            .z_slice_size = depth_slice_size_f32,
            .channel_size = channel_size_f32,
        });
    } else if (file_size == expected_file_size_bc6h) {
        data_source = std::make_shared<DataSource>(DataSource{
            .filename = path.string(),
            .format = Format::BC6H,
            .dimensions = dimensions,
            .channel_count = 1,
            .data_offset = sizeof(glm::uvec4),
            .data_size = data_size_bc6h,
            .time_slice_size = time_slice_size_bc6h,
            .z_slice_size = depth_slice_size_bc6h,
            .channel_size = channel_size_bc6h,
        });
    } else {
        lava::log()->error("file size mismatch: expected {} (Float16), {} (Float32) or {} (BC6H) bytes, got {} bytes", expected_file_size_f16, expected_file_size_f32, expected_file_size_bc6h, file_size);
        return nullptr;
    }
    lava::log()->info("raw dataset loaded (file: {}, dimensions: {}x{}x{}x{})", path.string(), dimensions.x, dimensions.y, dimensions.z, dimensions.w);
    data_source->file.swap(file);
    return data_source;
}

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

std::shared_ptr<DataSource> DataSource::open_ktx_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);

    KtxHeader header;

    if (!file) {
        spdlog::error("failed to open `{}`", path.string());
        return nullptr;
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
        lava::log()->error("dimensions missing in dataset");
        return nullptr;
    }

    std::uint32_t image_size;
    file.read(reinterpret_cast<char*>(&image_size), sizeof(image_size));

    glm::uvec4 dimensions = *reinterpret_cast<glm::ivec4*>(key_value_data.at("Dimensions").data());

    assert(image_size % dimensions.w == 0);
    const std::uint32_t time_slice_size = image_size / dimensions.w;

    assert(time_slice_size % dimensions.z == 0);
    const std::uint32_t z_size_in_bytes = time_slice_size / dimensions.z;

    lava::log()->info("ktx dataset loaded (file: {}, dimensions: {}x{}x{}x{})", path.string(), dimensions.x, dimensions.y, dimensions.z, dimensions.w);

    auto dataset = std::make_shared<DataSource>(DataSource{
        .filename = path.string(),
        .format = Format::BC6H,
        .dimensions = dimensions,
        .channel_count = 1,
        .data_offset = file.tellg(),
        .data_size = image_size,
        .time_slice_size = time_slice_size,
        .z_slice_size = z_size_in_bytes,
        .channel_size = image_size,
    });
    dataset->file.swap(file);
    return dataset;
}

void DataSource::imgui() {
    ImGui::InputText("Filename", this->filename.data(), this->filename.length(), ImGuiInputTextFlags_ReadOnly);
    ImGui::InputInt4("Dimensions", reinterpret_cast<int*>(glm::value_ptr(this->dimensions)), ImGuiInputTextFlags_ReadOnly);
}

void DataSource::read(void* buffer) {
    assert(buffer);
    this->file.seekg(this->data_offset);
    this->file.read(reinterpret_cast<char*>(buffer), this->data_size);
}

void DataSource::read_time_slice(int c, int t, void* buffer) {
    assert(buffer);
    this->file.seekg(this->data_offset + c * this->channel_size + t * this->time_slice_size);
    this->file.read(reinterpret_cast<char*>(buffer), this->time_slice_size);
}
