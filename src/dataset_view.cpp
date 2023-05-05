#include "dataset_view.hpp"

void DatasetView::render(VkCommandBuffer command_buffer) {
}

void DatasetView::imgui() {
}

DatasetView::Ptr make_dataset_view(Dataset::Ptr dataset) {
    return std::make_shared<DatasetView>(dataset);
}
