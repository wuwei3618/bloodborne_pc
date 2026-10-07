// SPDX-License-Identifier: GPL-2.0-or-later
// FSR 3.1's Vulkan backend (FidelityFX 1.1.4 in gpu/third_party/fsr-vulkan) picks the memory type
// of its images and buffers in findMemoryTypeIndex. For device-local requests it skips
// host-visible types, to keep them out of a discrete GPU's small host-visible heap. A unified
// memory device such as KosmicKrisp on Apple silicon has one type, device-local and host-visible:
// the backend must take it there, or FSR 3 context creation fails (gpu/patches/fsr-vulkan/0002).
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vulkan/vulkan.h>

uint32_t findMemoryTypeIndex(VkPhysicalDevice physicalDevice, VkMemoryRequirements memRequirements,
                             VkMemoryPropertyFlags requestedProperties,
                             VkBool32 deviceCoherentMemoryEnabled,
                             VkMemoryPropertyFlags& outProperties);

namespace {
VkPhysicalDeviceMemoryProperties device_memory{};

void SetMemoryTypes(std::initializer_list<VkMemoryPropertyFlags> types) {
    device_memory = {};
    device_memory.memoryHeapCount = 1;
    for (const VkMemoryPropertyFlags flags : types) {
        device_memory.memoryTypes[device_memory.memoryTypeCount++] = {flags, 0};
    }
}

uint32_t Find(VkMemoryPropertyFlags requested, uint32_t allowed_types = ~0u) {
    VkMemoryRequirements requirements{};
    requirements.memoryTypeBits = allowed_types;
    VkMemoryPropertyFlags properties = 0;
    const uint32_t index = findMemoryTypeIndex(reinterpret_cast<VkPhysicalDevice>(uintptr_t{1}),
                                               requirements, requested, VK_FALSE, properties);
    assert(index == UINT32_MAX || properties == device_memory.memoryTypes[index].propertyFlags);
    return index;
}
} // namespace

// The backend asks the Vulkan loader for the device's memory types; here the test answers.
extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* properties) {
    *properties = device_memory;
}

int main() {
    constexpr VkMemoryPropertyFlags local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    constexpr VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // KosmicKrisp on Apple silicon: a single type, device-local and host-visible.
    SetMemoryTypes({local | host | VK_MEMORY_PROPERTY_HOST_CACHED_BIT});
    assert(Find(local) == 0);
    assert(Find(local | host) == 0);
    assert(Find(local, 0) == UINT32_MAX); // a resource that allows no type still fails

    // Discrete GPU: device-local resources keep using the type that is not host-visible.
    SetMemoryTypes({local | host, local, host});
    assert(Find(local) == 1);

    // A device-coherent AMD type stays excluded when the feature is not enabled.
    SetMemoryTypes({local | host | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD});
    assert(Find(local) == UINT32_MAX);
    std::puts("FSR 3 memory type: PASS (unified memory uses its device-local, host-visible type)");
}
