#include "imgui.h"
#include "liblava/lava.hpp"
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <glm/fwd.hpp>
#include <iterator>
#include <memory>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

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
};

struct Slice {
    VkImage image;
    VkDeviceMemory memory;
};

std::optional<Slice> create_slice(VkDevice device, glm::uvec3 extend, std::uint32_t size_in_bytes) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = extend.x;
    image_info.extent.height = extend.y;
    image_info.extent.depth = extend.z;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_BC6H_SFLOAT_BLOCK;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    Slice slice;
    if (vkCreateImage(device, &image_info, nullptr, &slice.image) != VK_SUCCESS) {
        spdlog::error("failed to create slice image");
        return std::nullopt;
    }

    VkMemoryAllocateInfo mem_alloc_info = {};
    mem_alloc_info.memoryTypeIndex = 4; // TODO: Device local, host visible memory
    mem_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc_info.allocationSize = size_in_bytes;
    if (vkAllocateMemory(device, &mem_alloc_info, nullptr, &slice.memory) != VK_SUCCESS) {
        spdlog::error("failed to create slice memory");
        return std::nullopt;
    }

    return slice;
}

template <typename Rep, typename Period>
std::uint64_t to_ms(std::chrono::duration<Rep, Period> duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int main(int argc, char* argv[]) {
    lava::engine app("imgui demo", {argc, argv});

    if (!app.setup())
        return lava::error::not_ready;

    app.on_create = []() {
        std::ifstream file("/home/so225523/Data/BC6/Data/abc/abc2_highest_norm.ktx", std::ios::binary);
        if (!file) {
            return false;
        }

        glm::uvec4 dimensions;

        KtxHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        std::unique_ptr<std::byte[]> data;

        // struct Slice {
        //     std::byte* pointer;
        // };

        // std::vector<Slice> slices;
        
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
            return false;
        }

        dimensions = *reinterpret_cast<glm::ivec4*>(key_value_data.at("Dimensions").data());
        spdlog::info("simensions: {}x{}x{}x{}", dimensions.x, dimensions.y, dimensions.z, dimensions.w);


        std::vector<Slice> available_slices;

        {
            using Clock = std::chrono::steady_clock;
            std::uint32_t image_size;
            file.read(reinterpret_cast<char*>(&image_size), sizeof(image_size));


            spdlog::info("allocating memory");
            const auto t0 = Clock::now();
            data = std::make_unique<std::byte[]>(image_size);
            const auto t1 = Clock::now();
            spdlog::info("finished allocating memory ({}ms)", to_ms(t1 - t0));

            assert(image_size % dimensions.w == 0);

            std::uint32_t time_slice_size = image_size / dimensions.w;

            spdlog::info("slice size: {} ({}MiB)", time_slice_size, static_cast<double>(time_slice_size) / (1024 * 1024));

            spdlog::info("loading slices");
            const auto t2 = Clock::now();
            for (std::uint32_t t = 0; t < dimensions.w; ++t) {
                // spdlog::info("Reading slice {}", t);
                file.read(reinterpret_cast<char*>(data.get() + t * time_slice_size), time_slice_size);
                // spdlog::info("Finished reading slice {}", t);
            }
            const auto t3 = Clock::now();
            spdlog::info("finished loading slices ({}ms total, {}ms per slice)", to_ms(t3 - t2), static_cast<double>(to_ms(t3 - t2)) / time_slice_size);
        }

        spdlog::info("read {} bytes", file.tellg());

        return true;
    };

    app.imgui.on_draw = []() { ImGui::ShowDemoWindow(); };

    return app.run();
}
