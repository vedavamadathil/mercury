#pragma once

#include <string.h>
#include <string>
#include <vector>
#include <map>

#include <vulkan/vulkan_core.h>

struct VkFormatInfo {
	uint32_t size;
	uint32_t channels;
};

extern const std::map <VkFormat, VkFormatInfo> vk_format_table;
