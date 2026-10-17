#include <dlfcn.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

#define CK(expr, what)                                                       \
   do {                                                                      \
      VkResult _r = (expr);                                                  \
      if (_r != VK_SUCCESS) {                                                \
         printf("FAIL %s result=%d line=%d\n", what, _r, __LINE__);         \
         return 1;                                                           \
      }                                                                      \
   } while (0)

#define LOAD_INSTANCE(name)                                                  \
   PFN_vk##name vk##name = (PFN_vk##name)gipa(instance, "vk" #name);          \
   if (!vk##name) {                                                          \
      printf("FAIL missing vk" #name "\n");                                  \
      return 1;                                                              \
   }

#define LOAD_DEVICE(name)                                                    \
   PFN_vk##name vk##name =                                                   \
      (PFN_vk##name)vkGetDeviceProcAddr(device, "vk" #name);                 \
   if (!vk##name) {                                                          \
      printf("FAIL missing vk" #name "\n");                                  \
      return 1;                                                              \
   }

struct test_buffer {
   VkBuffer buffer;
   VkDeviceMemory memory;
   VkDeviceSize allocation_size;
   VkMemoryPropertyFlags memory_flags;
   VkDeviceAddress address;
};

struct test_as {
   VkAccelerationStructureKHR handle;
   struct test_buffer storage;
   VkDeviceSize size;
};

struct ir_header_prefix {
   int32_t min_bounds[3];
   int32_t max_bounds[3];
   uint32_t active_leaf_count;
   uint32_t tmp_active_leaf_count;
   uint32_t internal_node_count;
};

struct ir_triangle_node_prefix {
   float min_bounds[3];
   float max_bounds[3];
};

struct ir_triangle_node_layout {
   struct ir_triangle_node_prefix bounds;
   float coords[3][3];
   uint32_t triangle_id;
   uint32_t geometry_id_and_flags;
};

struct ir_box_node_layout {
   struct ir_triangle_node_prefix bounds;
   uint32_t children[2];
   uint32_t bvh_offset;
   uint32_t flags;
};

struct ir_instance_node_layout {
   struct ir_triangle_node_prefix bounds;
   uint64_t base_ptr;
   uint32_t custom_instance_and_mask;
   uint32_t sbt_offset_and_flags;
   float otw_matrix[3][4];
   uint32_t instance_id;
   uint32_t root_flags;
};

/* Generated from as_ray_query_probe.comp for Vulkan 1.2. */
static const uint32_t as_ray_query_probe_spv[] = {
#include "as_ray_query_probe.spv.h"
};

/* Generated from as_ir_traversal.comp for Vulkan 1.2. */
static const uint32_t as_ir_traversal_spv[] = {
#include "as_ir_traversal.spv.h"
};

_Static_assert(sizeof(struct ir_triangle_node_prefix) == 24,
               "BVH leaf AABB layout changed");
_Static_assert(sizeof(struct ir_triangle_node_layout) == 68,
               "BVH triangle node layout changed");
_Static_assert(sizeof(struct ir_box_node_layout) == 40,
               "BVH internal node layout changed");
_Static_assert(sizeof(struct ir_instance_node_layout) == 96,
               "BVH instance node layout changed");

static uint32_t
find_memory_type(const VkPhysicalDeviceMemoryProperties *memory,
                 uint32_t type_bits, VkMemoryPropertyFlags required,
                 VkMemoryPropertyFlags preferred)
{
   for (uint32_t i = 0; i < memory->memoryTypeCount; i++) {
      VkMemoryPropertyFlags flags = memory->memoryTypes[i].propertyFlags;
      if ((type_bits & (1u << i)) && (flags & required) == required &&
          (flags & preferred) == preferred)
         return i;
   }

   for (uint32_t i = 0; i < memory->memoryTypeCount; i++) {
      VkMemoryPropertyFlags flags = memory->memoryTypes[i].propertyFlags;
      if ((type_bits & (1u << i)) && (flags & required) == required)
         return i;
   }

   return UINT32_MAX;
}

static VkResult
create_buffer(PFN_vkCreateBuffer create_buffer,
              PFN_vkGetBufferMemoryRequirements get_requirements,
              PFN_vkAllocateMemory allocate_memory,
              PFN_vkBindBufferMemory bind_memory,
              PFN_vkGetBufferDeviceAddress get_address,
              const VkPhysicalDeviceMemoryProperties *memory,
              VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage,
              VkMemoryPropertyFlags required_flags,
              VkMemoryPropertyFlags preferred_flags,
              struct test_buffer *out)
{
   VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   VkResult result = create_buffer(device, &buffer_info, NULL, &out->buffer);
   if (result != VK_SUCCESS)
      return result;

   VkMemoryRequirements requirements;
   get_requirements(device, out->buffer, &requirements);
   uint32_t type_index =
      find_memory_type(memory, requirements.memoryTypeBits, required_flags,
                       preferred_flags);
   if (type_index == UINT32_MAX)
      return VK_ERROR_FEATURE_NOT_PRESENT;

   const bool needs_address =
      (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
   VkMemoryAllocateFlagsInfo flags_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
      .flags = needs_address ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0,
   };
   VkMemoryAllocateInfo allocation_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = needs_address ? &flags_info : NULL,
      .allocationSize = requirements.size,
      .memoryTypeIndex = type_index,
   };
   result = allocate_memory(device, &allocation_info, NULL, &out->memory);
   if (result != VK_SUCCESS)
      return result;

   out->allocation_size = requirements.size;
   out->memory_flags = memory->memoryTypes[type_index].propertyFlags;
   result = bind_memory(device, out->buffer, out->memory, 0);
   if (result != VK_SUCCESS)
      return result;

   if (needs_address) {
      VkBufferDeviceAddressInfo address_info = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
         .buffer = out->buffer,
      };
      out->address = get_address(device, &address_info);
      if (!out->address)
         return VK_ERROR_INVALID_DEVICE_ADDRESS_EXT;
   }

   return VK_SUCCESS;
}

static float
decode_emulated_float(int32_t value)
{
   uint32_t bits = (uint32_t)(value < 0 ? INT32_MIN - value : value);
   float decoded;
   memcpy(&decoded, &bits, sizeof(decoded));
   return decoded;
}

static bool
close_enough(float a, float b)
{
   return fabsf(a - b) <= 0.0001f * fmaxf(1.0f, fabsf(b));
}

static float
float_from_bits(uint32_t bits)
{
   float value;
   memcpy(&value, &bits, sizeof(value));
   return value;
}

static bool
ray_query_matrix_matches(const uint32_t *values, const float expected[4][3])
{
   for (uint32_t column = 0; column < 4; column++) {
      for (uint32_t row = 0; row < 3; row++) {
         if (!close_enough(float_from_bits(values[column * 4 + row]),
                           expected[column][row]))
            return false;
      }
   }
   return true;
}

static VkResult
submit_as_build(VkDevice device, VkCommandPool command_pool, VkQueue queue,
                PFN_vkAllocateCommandBuffers allocate_command_buffers,
                PFN_vkFreeCommandBuffers free_command_buffers,
                PFN_vkBeginCommandBuffer begin_command_buffer,
                PFN_vkEndCommandBuffer end_command_buffer,
                PFN_vkCmdPipelineBarrier cmd_pipeline_barrier,
                PFN_vkCmdBuildAccelerationStructuresKHR cmd_build,
                PFN_vkQueueSubmit queue_submit,
                PFN_vkQueueWaitIdle queue_wait_idle,
                VkAccelerationStructureBuildGeometryInfoKHR *build_info,
                const VkAccelerationStructureBuildRangeInfoKHR *const *ranges)
{
   VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer command_buffer;
   VkResult result =
      allocate_command_buffers(device, &alloc_info, &command_buffer);
   if (result != VK_SUCCESS)
      return result;

   VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   result = begin_command_buffer(command_buffer, &begin_info);
   if (result != VK_SUCCESS)
      goto free_command_buffer;

   VkMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
   };
   cmd_pipeline_barrier(command_buffer,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        0, 1, &barrier, 0, NULL, 0, NULL);

   cmd_build(command_buffer, 1, build_info, ranges);

   result = end_command_buffer(command_buffer);
   if (result != VK_SUCCESS)
      goto free_command_buffer;

   VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
   };
   result = queue_submit(queue, 1, &submit_info, VK_NULL_HANDLE);
   if (result == VK_SUCCESS)
      result = queue_wait_idle(queue);

free_command_buffer:
   free_command_buffers(device, command_pool, 1, &command_buffer);
   return result;
}

static VkResult
copy_as_to_readback(VkDevice device, VkCommandPool command_pool, VkQueue queue,
                    PFN_vkAllocateCommandBuffers allocate_command_buffers,
                    PFN_vkFreeCommandBuffers free_command_buffers,
                    PFN_vkBeginCommandBuffer begin_command_buffer,
                    PFN_vkEndCommandBuffer end_command_buffer,
                    PFN_vkCmdPipelineBarrier cmd_pipeline_barrier,
                    PFN_vkCmdCopyBuffer cmd_copy_buffer,
                    PFN_vkQueueSubmit queue_submit,
                    PFN_vkQueueWaitIdle queue_wait_idle, VkBuffer source,
                    VkBuffer destination, VkDeviceSize size)
{
   VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer command_buffer;
   VkResult result =
      allocate_command_buffers(device, &alloc_info, &command_buffer);
   if (result != VK_SUCCESS)
      return result;

   VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   result = begin_command_buffer(command_buffer, &begin_info);
   if (result != VK_SUCCESS)
      goto free_command_buffer;

   VkBufferMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = source,
      .offset = 0,
      .size = size,
   };
   cmd_pipeline_barrier(
      command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);
   VkBufferCopy copy = {.size = size};
   cmd_copy_buffer(command_buffer, source, destination, 1, &copy);
   result = end_command_buffer(command_buffer);
   if (result != VK_SUCCESS)
      goto free_command_buffer;

   VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
   };
   result = queue_submit(queue, 1, &submit_info, VK_NULL_HANDLE);
   if (result == VK_SUCCESS)
      result = queue_wait_idle(queue);

free_command_buffer:
   free_command_buffers(device, command_pool, 1, &command_buffer);
   return result;
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("usage: %s <libvulkan_panfrost.so>\n", argv[0]);
      return 2;
   }

   void *icd = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!icd) {
      printf("FAIL dlopen %s\n", dlerror());
      return 1;
   }
   icd_gipa_fn gipa = (icd_gipa_fn)dlsym(icd, "vk_icdGetInstanceProcAddr");
   if (!gipa) {
      printf("FAIL missing vk_icdGetInstanceProcAddr\n");
      return 1;
   }

   PFN_vkCreateInstance vkCreateInstance =
      (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   if (!vkCreateInstance) {
      printf("FAIL missing vkCreateInstance\n");
      return 1;
   }
   VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = VK_API_VERSION_1_3,
   };
   VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
   };
   VkInstance instance;
   CK(vkCreateInstance(&instance_info, NULL, &instance), "CreateInstance");

   LOAD_INSTANCE(EnumeratePhysicalDevices)
   LOAD_INSTANCE(GetPhysicalDeviceProperties)
   LOAD_INSTANCE(GetPhysicalDeviceFeatures2)
   LOAD_INSTANCE(GetPhysicalDeviceProperties2)
   LOAD_INSTANCE(GetPhysicalDeviceMemoryProperties)
   LOAD_INSTANCE(GetPhysicalDeviceQueueFamilyProperties)
   LOAD_INSTANCE(EnumerateDeviceExtensionProperties)
   LOAD_INSTANCE(CreateDevice)

   uint32_t physical_count = 0;
   CK(vkEnumeratePhysicalDevices(instance, &physical_count, NULL),
      "EnumeratePhysicalDevices(count)");
   if (!physical_count) {
      printf("FAIL no physical devices\n");
      return 1;
   }
   VkPhysicalDevice *physical_devices =
      calloc(physical_count, sizeof(*physical_devices));
   if (!physical_devices) {
      printf("FAIL out of host memory\n");
      return 1;
   }
   CK(vkEnumeratePhysicalDevices(instance, &physical_count, physical_devices),
      "EnumeratePhysicalDevices");

   VkPhysicalDevice physical = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < physical_count; i++) {
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
      if (strstr(properties.deviceName, "Mali"))
         physical = physical_devices[i];
   }
   free(physical_devices);
   if (!physical) {
      printf("FAIL no Mali device\n");
      return 1;
   }

   uint32_t extension_count = 0;
   CK(vkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count,
                                           NULL),
      "EnumerateDeviceExtensionProperties(count)");
   VkExtensionProperties *extensions =
      calloc(extension_count, sizeof(*extensions));
   if (!extensions) {
      printf("FAIL out of host memory\n");
      return 1;
   }
   CK(vkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count,
                                           extensions),
      "EnumerateDeviceExtensionProperties");
   bool have_as = false, have_deferred = false;
   bool have_ray_query = false, have_ray_tracing_pipeline = false;
   for (uint32_t i = 0; i < extension_count; i++) {
      have_as |= strcmp(extensions[i].extensionName,
                        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
      have_deferred |=
         strcmp(extensions[i].extensionName,
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0;
      have_ray_query |=
         strcmp(extensions[i].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) ==
         0;
      have_ray_tracing_pipeline |=
         strcmp(extensions[i].extensionName,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0;
   }
   free(extensions);
   if (!have_as || !have_deferred || !have_ray_query) {
      printf("FAIL PanVK did not expose required AS/Ray Query extensions\n");
      return 1;
   }
   if (have_ray_tracing_pipeline) {
      printf("FAIL PanVK exposed unimplemented ray tracing pipeline\n");
      return 1;
   }
   printf("PASS PanVK advertises AS/deferred/ray query; RT pipeline hidden\n");

   VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
   };
   VkPhysicalDeviceFeatures2 ray_query_features2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &ray_query_features,
   };
   vkGetPhysicalDeviceFeatures2(physical, &ray_query_features2);
   if (!ray_query_features.rayQuery) {
      printf("FAIL PanVK did not expose rayQuery feature\n");
      return 1;
   }
   printf("PASS PanVK exposes rayQuery feature\n");

   VkPhysicalDeviceBufferDeviceAddressFeatures bda_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
   };
   ray_query_features.pNext = &bda_features;
   VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features = {
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
      .pNext = &ray_query_features,
   };
   VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &as_features,
   };
   vkGetPhysicalDeviceFeatures2(physical, &features);
   if (!as_features.accelerationStructure || !bda_features.bufferDeviceAddress) {
      printf("FAIL missing AS or buffer device address feature\n");
      return 1;
   }
   if (as_features.accelerationStructureCaptureReplay ||
       as_features.accelerationStructureHostCommands ||
       as_features.descriptorBindingAccelerationStructureUpdateAfterBind ||
       as_features.accelerationStructureIndirectBuild) {
      printf("FAIL PanVK exposed an unimplemented optional AS feature\n");
      return 1;
   }

   VkPhysicalDeviceAccelerationStructurePropertiesKHR as_properties = {
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
   };
   VkPhysicalDeviceProperties2 properties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &as_properties,
   };
   vkGetPhysicalDeviceProperties2(physical, &properties);
   VkDeviceSize scratch_alignment =
      as_properties.minAccelerationStructureScratchOffsetAlignment;
   if (!scratch_alignment ||
       (scratch_alignment & (scratch_alignment - 1)) != 0) {
      printf("FAIL invalid scratch alignment=%llu\n",
             (unsigned long long)scratch_alignment);
      return 1;
   }

   uint32_t queue_family_count = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count, NULL);
   VkQueueFamilyProperties *queue_families =
      calloc(queue_family_count, sizeof(*queue_families));
   if (!queue_families) {
      printf("FAIL out of host memory\n");
      return 1;
   }
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count,
                                            queue_families);
   uint32_t queue_family = UINT32_MAX;
   for (uint32_t i = 0; i < queue_family_count; i++) {
      if (queue_families[i].queueCount &&
          (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
         queue_family = i;
         break;
      }
   }
   free(queue_families);
   if (queue_family == UINT32_MAX) {
      printf("FAIL no compute queue family\n");
      return 1;
   }

   const char *device_extensions[] = {
      VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
      VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
      VK_KHR_RAY_QUERY_EXTENSION_NAME,
   };
   const float queue_priority = 1.0f;
   VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queue_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
   };
   VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &as_features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount =
         sizeof(device_extensions) / sizeof(device_extensions[0]),
      .ppEnabledExtensionNames = device_extensions,
   };
   VkDevice device;
   CK(vkCreateDevice(physical, &device_info, NULL, &device), "CreateDevice");

   PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr =
      (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
   if (!vkGetDeviceProcAddr) {
      printf("FAIL missing vkGetDeviceProcAddr\n");
      return 1;
   }
   LOAD_DEVICE(GetDeviceQueue)
   LOAD_DEVICE(GetBufferDeviceAddress)
   LOAD_DEVICE(CreateBuffer)
   LOAD_DEVICE(GetBufferMemoryRequirements)
   LOAD_DEVICE(AllocateMemory)
   LOAD_DEVICE(BindBufferMemory)
   LOAD_DEVICE(MapMemory)
   LOAD_DEVICE(UnmapMemory)
   LOAD_DEVICE(FlushMappedMemoryRanges)
   LOAD_DEVICE(InvalidateMappedMemoryRanges)
   LOAD_DEVICE(DestroyBuffer)
   LOAD_DEVICE(FreeMemory)
   LOAD_DEVICE(CreateDescriptorSetLayout)
   LOAD_DEVICE(DestroyDescriptorSetLayout)
   LOAD_DEVICE(CreateDescriptorPool)
   LOAD_DEVICE(DestroyDescriptorPool)
   LOAD_DEVICE(AllocateDescriptorSets)
   LOAD_DEVICE(UpdateDescriptorSets)
   LOAD_DEVICE(CreateDescriptorUpdateTemplate)
   LOAD_DEVICE(DestroyDescriptorUpdateTemplate)
   LOAD_DEVICE(UpdateDescriptorSetWithTemplate)
   LOAD_DEVICE(CreateShaderModule)
   LOAD_DEVICE(DestroyShaderModule)
   LOAD_DEVICE(CreatePipelineLayout)
   LOAD_DEVICE(DestroyPipelineLayout)
   LOAD_DEVICE(CreateComputePipelines)
   LOAD_DEVICE(DestroyPipeline)
   LOAD_DEVICE(CmdBindPipeline)
   LOAD_DEVICE(CmdBindDescriptorSets)
   LOAD_DEVICE(CmdPushConstants)
   LOAD_DEVICE(CmdDispatch)
   LOAD_DEVICE(CreateAccelerationStructureKHR)
   LOAD_DEVICE(DestroyAccelerationStructureKHR)
   LOAD_DEVICE(GetAccelerationStructureDeviceAddressKHR)
   LOAD_DEVICE(GetAccelerationStructureBuildSizesKHR)
   LOAD_DEVICE(CmdBuildAccelerationStructuresKHR)
   LOAD_DEVICE(CmdBuildAccelerationStructuresIndirectKHR)
   LOAD_DEVICE(BuildAccelerationStructuresKHR)
   LOAD_DEVICE(WriteAccelerationStructuresPropertiesKHR)
   LOAD_DEVICE(CopyAccelerationStructureKHR)
   LOAD_DEVICE(CopyMemoryToAccelerationStructureKHR)
   LOAD_DEVICE(CopyAccelerationStructureToMemoryKHR)
   LOAD_DEVICE(CreateCommandPool)
   LOAD_DEVICE(DestroyCommandPool)
   LOAD_DEVICE(AllocateCommandBuffers)
   LOAD_DEVICE(FreeCommandBuffers)
   LOAD_DEVICE(BeginCommandBuffer)
   LOAD_DEVICE(EndCommandBuffer)
   LOAD_DEVICE(CmdPipelineBarrier)
   LOAD_DEVICE(CmdCopyBuffer)
   LOAD_DEVICE(QueueSubmit)
   LOAD_DEVICE(QueueWaitIdle)
   LOAD_DEVICE(DestroyDevice)
   VkQueue queue;
   vkGetDeviceQueue(device, queue_family, 0, &queue);

   VkPhysicalDeviceMemoryProperties memory_properties;
   vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
   VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = queue_family,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
   };
   VkCommandPool command_pool;
   CK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool),
      "CreateCommandPool");

   if (vkBuildAccelerationStructuresKHR(device, VK_NULL_HANDLE, 0, NULL,
                                        NULL) !=
          VK_ERROR_FEATURE_NOT_PRESENT ||
       vkWriteAccelerationStructuresPropertiesKHR(
          device, 0, NULL, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
          0, NULL, 0) != VK_ERROR_FEATURE_NOT_PRESENT ||
       vkCopyAccelerationStructureKHR(device, VK_NULL_HANDLE, NULL) !=
          VK_ERROR_FEATURE_NOT_PRESENT ||
       vkCopyMemoryToAccelerationStructureKHR(device, VK_NULL_HANDLE, NULL) !=
          VK_ERROR_FEATURE_NOT_PRESENT ||
       vkCopyAccelerationStructureToMemoryKHR(device, VK_NULL_HANDLE, NULL) !=
          VK_ERROR_FEATURE_NOT_PRESENT) {
      printf("FAIL unsupported host AS operations did not return "
             "VK_ERROR_FEATURE_NOT_PRESENT\n");
      return 1;
   }

   VkCommandBufferAllocateInfo unsupported_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer unsupported_cmd;
   CK(vkAllocateCommandBuffers(device, &unsupported_alloc_info,
                              &unsupported_cmd),
      "Allocate unsupported-operation command buffer");
   VkCommandBufferBeginInfo unsupported_begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };
   CK(vkBeginCommandBuffer(unsupported_cmd, &unsupported_begin_info),
      "Begin unsupported-operation command buffer");
   vkCmdBuildAccelerationStructuresIndirectKHR(unsupported_cmd, 0, NULL, NULL,
                                               NULL, NULL);
   VkResult unsupported_end_result = vkEndCommandBuffer(unsupported_cmd);
   vkFreeCommandBuffers(device, command_pool, 1, &unsupported_cmd);
   if (unsupported_end_result != VK_ERROR_FEATURE_NOT_PRESENT) {
      printf("FAIL indirect AS build result=%d expected=%d\n",
             unsupported_end_result, VK_ERROR_FEATURE_NOT_PRESENT);
      return 1;
   }
   printf("PASS unsupported AS copy/property/host/indirect operations rejected\n");

   enum { TRIANGLE_COUNT = 257 };
   struct vertex {
      float x, y, z;
   } vertices[TRIANGLE_COUNT * 3];
   for (uint32_t i = 0; i < TRIANGLE_COUNT; i++) {
      float x = (float)(i * 2);
      vertices[i * 3 + 0] = (struct vertex){x, 0.0f, 0.0f};
      vertices[i * 3 + 1] = (struct vertex){x + 1.0f, 0.0f, 0.0f};
      vertices[i * 3 + 2] = (struct vertex){x, 1.0f, 1.0f};
   }

   struct test_buffer vertex_buffer = {0};
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, sizeof(vertices),
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertex_buffer),
      "Create vertex buffer");
   void *mapped_vertices = NULL;
   CK(vkMapMemory(device, vertex_buffer.memory, 0, vertex_buffer.allocation_size,
                  0, &mapped_vertices),
      "Map vertex buffer");
   memcpy(mapped_vertices, vertices, sizeof(vertices));
   if (!(vertex_buffer.memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = vertex_buffer.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkFlushMappedMemoryRanges(device, 1, &range), "Flush vertex buffer");
   }
   vkUnmapMemory(device, vertex_buffer.memory);

   VkAccelerationStructureGeometryKHR blas_geometry = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
      .geometry.triangles =
         {
            .sType =
               VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData.deviceAddress = vertex_buffer.address,
            .vertexStride = sizeof(struct vertex),
            .maxVertex = TRIANGLE_COUNT * 3 - 1,
            .indexType = VK_INDEX_TYPE_NONE_KHR,
         },
      .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
   };
   VkAccelerationStructureBuildGeometryInfoKHR blas_build = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
      .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1,
      .pGeometries = &blas_geometry,
   };
   uint32_t blas_primitive_count = TRIANGLE_COUNT;
   VkAccelerationStructureBuildSizesInfoKHR blas_sizes = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
   };
   vkGetAccelerationStructureBuildSizesKHR(
      device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build,
      &blas_primitive_count, &blas_sizes);
   if (!blas_sizes.accelerationStructureSize || !blas_sizes.buildScratchSize) {
      printf("FAIL invalid BLAS sizes AS=%llu scratch=%llu\n",
             (unsigned long long)blas_sizes.accelerationStructureSize,
             (unsigned long long)blas_sizes.buildScratchSize);
      return 1;
   }
   uint32_t empty_primitive_count = 0;
   VkAccelerationStructureBuildSizesInfoKHR empty_blas_sizes = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
   };
   vkGetAccelerationStructureBuildSizesKHR(
      device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build,
      &empty_primitive_count, &empty_blas_sizes);
   if (empty_blas_sizes.accelerationStructureSize < 128 + sizeof(uint32_t) * 8) {
      printf("FAIL invalid empty BLAS size=%llu\n",
             (unsigned long long)empty_blas_sizes.accelerationStructureSize);
      return 1;
   }
   printf("PASS BLAS size query for zero primitives: AS=%llu scratch=%llu\n",
          (unsigned long long)empty_blas_sizes.accelerationStructureSize,
          (unsigned long long)empty_blas_sizes.buildScratchSize);
   printf("BLAS sizes: AS=%llu scratch=%llu (triangles=%u)\n",
          (unsigned long long)blas_sizes.accelerationStructureSize,
          (unsigned long long)blas_sizes.buildScratchSize, TRIANGLE_COUNT);

   struct test_as blas = {.size = blas_sizes.accelerationStructureSize};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    blas.size,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &blas.storage),
      "Create BLAS storage");
   VkAccelerationStructureCreateInfoKHR as_create_info = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = blas.storage.buffer,
      .size = blas.size,
      .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
   };
   CK(vkCreateAccelerationStructureKHR(device, &as_create_info, NULL,
                                       &blas.handle),
      "Create BLAS");

   struct test_buffer blas_scratch = {0};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    blas_sizes.buildScratchSize + scratch_alignment - 1,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &blas_scratch),
      "Create BLAS scratch");
   VkDeviceAddress blas_scratch_address =
      (blas_scratch.address + scratch_alignment - 1) & ~(scratch_alignment - 1);
   blas_build.dstAccelerationStructure = blas.handle;
   blas_build.scratchData.deviceAddress = blas_scratch_address;
   VkAccelerationStructureBuildRangeInfoKHR blas_range = {
      .primitiveCount = TRIANGLE_COUNT,
   };
   const VkAccelerationStructureBuildRangeInfoKHR *blas_ranges[] = {
      &blas_range,
   };
   VkCommandBufferAllocateInfo command_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer command_buffer;
   CK(vkAllocateCommandBuffers(device, &command_alloc_info, &command_buffer),
      "Allocate BLAS command buffer");
   VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   CK(vkBeginCommandBuffer(command_buffer, &begin_info),
      "Begin BLAS command buffer");
   vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &blas_build,
                                       blas_ranges);
   CK(vkEndCommandBuffer(command_buffer), "End BLAS command buffer");
   VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
   };
   CK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
      "Submit BLAS build");
   CK(vkQueueWaitIdle(queue), "Wait for BLAS build");
   vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

   struct test_buffer blas_readback = {0};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    blas.size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &blas_readback),
      "Create BLAS readback");
   CK(vkAllocateCommandBuffers(device, &command_alloc_info, &command_buffer),
      "Allocate BLAS readback command buffer");
   CK(vkBeginCommandBuffer(command_buffer, &begin_info),
      "Begin BLAS readback command buffer");
   VkBufferMemoryBarrier blas_barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = blas.storage.buffer,
      .offset = 0,
      .size = blas.size,
   };
   vkCmdPipelineBarrier(
      command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &blas_barrier, 0, NULL);
   VkBufferCopy blas_copy = {.size = blas.size};
   vkCmdCopyBuffer(command_buffer, blas.storage.buffer, blas_readback.buffer, 1,
                   &blas_copy);
   CK(vkEndCommandBuffer(command_buffer), "End BLAS readback command buffer");
   submit_info.pCommandBuffers = &command_buffer;
   CK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
      "Submit BLAS readback");
   CK(vkQueueWaitIdle(queue), "Wait for BLAS readback");
   vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

   void *mapped_readback = NULL;
   CK(vkMapMemory(device, blas_readback.memory, 0, blas_readback.allocation_size,
                  0, &mapped_readback),
      "Map BLAS readback");
   if (!(blas_readback.memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = blas_readback.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &range),
         "Invalidate BLAS readback");
   }
   struct ir_header_prefix blas_header;
   memcpy(&blas_header, mapped_readback, sizeof(blas_header));
   const uint8_t *blas_bytes = mapped_readback;
   const uint32_t node_offset = 128;
   if (blas.size <= node_offset + sizeof(float) * 6) {
      printf("FAIL BLAS storage has no node region\n");
      return 1;
   }
   float first_leaf_bounds[6];
   memcpy(first_leaf_bounds, blas_bytes + node_offset,
          sizeof(first_leaf_bounds));
   struct ir_triangle_node_prefix first_internal_bounds;
   const VkDeviceSize internal_offset =
      node_offset +
      (VkDeviceSize)TRIANGLE_COUNT * sizeof(struct ir_triangle_node_layout);
   if (blas.size < internal_offset + sizeof(first_internal_bounds)) {
      printf("FAIL BLAS storage has no internal-node region\n");
      return 1;
   }
   memcpy(&first_internal_bounds, blas_bytes + internal_offset,
          sizeof(first_internal_bounds));
   uint32_t internal_parent_count[TRIANGLE_COUNT - 1] = {0};
   uint32_t leaf_parent_count[TRIANGLE_COUNT] = {0};
   if (blas_header.internal_node_count != TRIANGLE_COUNT - 1) {
      printf("FAIL BLAS IR internal nodes=%u expected=%u\n",
             blas_header.internal_node_count, TRIANGLE_COUNT - 1);
      return 1;
   }
   for (uint32_t node = 0; node < TRIANGLE_COUNT - 1; node++) {
      struct ir_box_node_layout internal_node;
      memcpy(&internal_node, blas_bytes + internal_offset +
                                 (VkDeviceSize)node *
                                    sizeof(internal_node),
             sizeof(internal_node));
      for (uint32_t child = 0; child < 2; child++) {
         const uint32_t child_id = internal_node.children[child];
         if (child_id == UINT32_MAX)
            continue;

         const uint32_t child_offset = child_id & ~3u;
         if ((child_id & 3u) == 1u) {
            if (child_offset < TRIANGLE_COUNT *
                                  sizeof(struct ir_triangle_node_layout) ||
                (child_offset -
                 TRIANGLE_COUNT * sizeof(struct ir_triangle_node_layout)) %
                       sizeof(struct ir_box_node_layout) !=
                   0) {
               printf("FAIL BLAS internal child ID=%u has invalid offset\n",
                      child_id);
               return 1;
            }
            const uint32_t child_index =
               (child_offset - TRIANGLE_COUNT *
                                   sizeof(struct ir_triangle_node_layout)) /
               sizeof(struct ir_box_node_layout);
            if (child_index >= TRIANGLE_COUNT - 1) {
               printf("FAIL BLAS internal child ID=%u exceeds internal-node array\n",
                      child_id);
               return 1;
            }
            internal_parent_count[child_index]++;
         } else if ((child_id & 3u) == 0u) {
            if (child_offset % sizeof(struct ir_triangle_node_layout) != 0 ||
                child_offset >= TRIANGLE_COUNT *
                                   sizeof(struct ir_triangle_node_layout)) {
               printf("FAIL BLAS triangle child ID=%u has invalid offset\n",
                      child_id);
               return 1;
            }
            leaf_parent_count[child_offset /
                              sizeof(struct ir_triangle_node_layout)]++;
         } else {
            printf("FAIL BLAS child ID=%u has unsupported node type\n",
                   child_id);
            return 1;
         }
      }
   }
   uint32_t root_node_id = UINT32_MAX;
   uint32_t root_count = 0;
   for (uint32_t node = 0; node < TRIANGLE_COUNT - 1; node++) {
      if (internal_parent_count[node] == 0) {
         root_node_id =
            (uint32_t)(TRIANGLE_COUNT *
                       sizeof(struct ir_triangle_node_layout) +
                       (VkDeviceSize)node *
                          sizeof(struct ir_box_node_layout)) |
            1u;
         root_count++;
      }
   }
   vkUnmapMemory(device, blas_readback.memory);
   const float expected_min[] = {0.0f, 0.0f, 0.0f};
   const float expected_max[] = {513.0f, 1.0f, 1.0f};
   const float first_tlas_instance_bounds[] = {
      3.0f,
      0.5f,
      0.0f,
      1029.0f,
      1.0f,
      3.0f,
   };
   const float second_tlas_instance_bounds[] = {
      1100.0f, 0.0f, 0.0f, 1613.0f, 1.0f, 1.0f,
   };
   const float third_tlas_instance_bounds[] = {
      2200.0f, 0.0f, 0.0f, 2713.0f, 1.0f, 1.0f,
   };
   const float fourth_tlas_instance_bounds[] = {
      3300.0f, 0.0f, 0.0f, 3813.0f, 1.0f, 1.0f,
   };
   const float expected_tlas_bounds[] = {
      3.0f, 0.0f, 0.0f, 3813.0f, 1.0f, 3.0f,
   };
   if (blas_header.active_leaf_count != TRIANGLE_COUNT) {
      printf("FAIL BLAS active leaves=%u expected=%u\n",
             blas_header.active_leaf_count, TRIANGLE_COUNT);
      return 1;
   }
   for (uint32_t i = 0; i < 3; i++) {
      float min_bound = decode_emulated_float(blas_header.min_bounds[i]);
      float max_bound = decode_emulated_float(blas_header.max_bounds[i]);
      if (!close_enough(min_bound, expected_min[i]) ||
          !close_enough(max_bound, expected_max[i])) {
         printf("FAIL BLAS bounds axis=%u got=[%g,%g] expected=[%g,%g]\n", i,
                min_bound, max_bound, expected_min[i], expected_max[i]);
         return 1;
      }
   }
   const float expected_first_leaf[] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
   for (uint32_t i = 0; i < 6; i++) {
      if (!close_enough(first_leaf_bounds[i], expected_first_leaf[i])) {
         printf("FAIL first BLAS node bound[%u]=%g expected=%g\n", i,
                first_leaf_bounds[i], expected_first_leaf[i]);
         return 1;
      }
   }
   if (!isfinite(first_internal_bounds.min_bounds[0]) ||
       !isfinite(first_internal_bounds.max_bounds[0]) ||
       first_internal_bounds.min_bounds[0] >
          first_internal_bounds.max_bounds[0] ||
       first_internal_bounds.min_bounds[0] < expected_min[0] ||
       first_internal_bounds.max_bounds[0] > expected_max[0]) {
      printf("FAIL first BLAS internal-node bounds=[%g,%g]\n",
             first_internal_bounds.min_bounds[0],
             first_internal_bounds.max_bounds[0]);
      return 1;
   }
   printf("PASS BLAS build/readback: 257 leaves and expected bounds\n");

   if (root_count != 1 || root_node_id == UINT32_MAX) {
      printf("FAIL BLAS IR root candidates=%u\n", root_count);
      return 1;
   }
   for (uint32_t node = 0; node < TRIANGLE_COUNT - 1; node++) {
      uint32_t expected_parents = node == ((root_node_id & ~3u) -
         TRIANGLE_COUNT * sizeof(struct ir_triangle_node_layout)) /
            sizeof(struct ir_box_node_layout)
         ? 0
         : 1;
      if (internal_parent_count[node] != expected_parents) {
         printf("FAIL BLAS IR internal node=%u parents=%u expected=%u\n",
                node, internal_parent_count[node], expected_parents);
         return 1;
      }
   }
   for (uint32_t leaf = 0; leaf < TRIANGLE_COUNT; leaf++) {
      if (leaf_parent_count[leaf] != 1) {
         printf("FAIL BLAS IR leaf=%u parents=%u expected=1\n", leaf,
                leaf_parent_count[leaf]);
         return 1;
      }
   }

   struct test_buffer traversal_results = {0};
   enum {
      TRAVERSAL_RAY_COUNT = 64,
      TLAS_INSTANCE_COUNT = 4,
      TLAS_INTERNAL_NODE_COUNT = TLAS_INSTANCE_COUNT - 1,
   };
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    TRAVERSAL_RAY_COUNT * 3 * 2 * 4 * sizeof(uint32_t),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &traversal_results),
      "Create BLAS traversal result buffer");

   VkDescriptorSetLayoutBinding traversal_bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
   };
   VkDescriptorSetLayoutCreateInfo traversal_set_layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = sizeof(traversal_bindings) / sizeof(traversal_bindings[0]),
      .pBindings = traversal_bindings,
   };
   VkDescriptorSetLayout traversal_set_layout;
   CK(vkCreateDescriptorSetLayout(device, &traversal_set_layout_info, NULL,
                                  &traversal_set_layout),
      "Create BLAS traversal descriptor layout");

   VkDescriptorPoolSize traversal_pool_size = {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 3,
   };
   VkDescriptorPoolCreateInfo traversal_pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &traversal_pool_size,
   };
   VkDescriptorPool traversal_pool;
   CK(vkCreateDescriptorPool(device, &traversal_pool_info, NULL,
                             &traversal_pool),
      "Create BLAS traversal descriptor pool");

   VkDescriptorSetAllocateInfo traversal_set_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = traversal_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &traversal_set_layout,
   };
   VkDescriptorSet traversal_set;
   CK(vkAllocateDescriptorSets(device, &traversal_set_alloc_info,
                               &traversal_set),
      "Allocate BLAS traversal descriptor set");
   VkDescriptorBufferInfo traversal_buffer_infos[] = {
      {
         .buffer = blas.storage.buffer,
         .offset = 0,
         .range = blas.size,
      },
      {
         .buffer = traversal_results.buffer,
         .offset = 0,
         .range = TRAVERSAL_RAY_COUNT * 3 * 2 * 4 * sizeof(uint32_t),
      },
      {
         .buffer = blas.storage.buffer,
         .offset = 0,
         .range = blas.size,
      },
   };
   VkWriteDescriptorSet traversal_writes[] = {
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = traversal_set,
         .dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &traversal_buffer_infos[0],
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = traversal_set,
         .dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &traversal_buffer_infos[1],
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = traversal_set,
         .dstBinding = 2,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &traversal_buffer_infos[2],
      },
   };
   vkUpdateDescriptorSets(device, sizeof(traversal_writes) /
                                     sizeof(traversal_writes[0]),
                          traversal_writes, 0, NULL);

   VkShaderModuleCreateInfo traversal_shader_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(as_ir_traversal_spv),
      .pCode = as_ir_traversal_spv,
   };
   VkShaderModule traversal_shader;
   CK(vkCreateShaderModule(device, &traversal_shader_info, NULL,
                           &traversal_shader),
      "Create BLAS traversal shader module");

   VkPushConstantRange traversal_push_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = 9 * sizeof(uint32_t),
   };
   VkPipelineLayoutCreateInfo traversal_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &traversal_set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &traversal_push_range,
   };
   VkPipelineLayout traversal_layout;
   CK(vkCreatePipelineLayout(device, &traversal_layout_info, NULL,
                             &traversal_layout),
      "Create BLAS traversal pipeline layout");
   VkComputePipelineCreateInfo traversal_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = traversal_shader,
         .pName = "main",
      },
      .layout = traversal_layout,
   };
   VkPipeline traversal_pipeline;
   CK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                               &traversal_pipeline_info, NULL,
                               &traversal_pipeline),
      "Create BLAS traversal compute pipeline");

   VkBufferMemoryBarrier as_to_compute = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = blas.storage.buffer,
      .offset = 0,
      .size = blas.size,
   };
   VkBufferMemoryBarrier compute_to_host = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = traversal_results.buffer,
      .offset = 0,
      .size = TRAVERSAL_RAY_COUNT * 3 * 2 * 4 * sizeof(uint32_t),
   };
   CK(vkAllocateCommandBuffers(device, &command_alloc_info, &command_buffer),
      "Allocate BLAS traversal command buffer");
   CK(vkBeginCommandBuffer(command_buffer, &begin_info),
      "Begin BLAS traversal command buffer");
   vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1,
                        &as_to_compute, 0, NULL);
   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                     traversal_pipeline);
   vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           traversal_layout, 0, 1, &traversal_set, 0, NULL);
   uint32_t traversal_push[] = {
      root_node_id,
      TRAVERSAL_RAY_COUNT,
      0,
      0x42c80000,
      0,
      0,
      0,
      0,
      0,
   };
   for (uint32_t decision = 0; decision < 3; decision++) {
      traversal_push[8] = decision;
      vkCmdPushConstants(command_buffer, traversal_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(traversal_push), traversal_push);
      vkCmdDispatch(command_buffer, TRAVERSAL_RAY_COUNT, 1, 1);
   }
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                        &compute_to_host, 0, NULL);
   CK(vkEndCommandBuffer(command_buffer), "End BLAS traversal command buffer");
   submit_info.pCommandBuffers = &command_buffer;
   CK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
      "Submit BLAS traversal");
   CK(vkQueueWaitIdle(queue), "Wait for BLAS traversal");
   vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

   uint32_t *traversal_output = NULL;
   CK(vkMapMemory(device, traversal_results.memory, 0,
                  traversal_results.allocation_size, 0,
                  (void **)&traversal_output),
      "Map BLAS traversal results");
   if (!(traversal_results.memory_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange traversal_range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = traversal_results.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &traversal_range),
         "Invalidate BLAS traversal results");
   }
   for (uint32_t decision = 0; decision < 3; decision++) {
      for (uint32_t ray = 0; ray < TRAVERSAL_RAY_COUNT; ray++) {
         const bool expected_hit = (ray & 3u) == 0;
         const uint32_t target_primitive = ((ray & 15u) / 4) * 64;
         const uint32_t *actual =
            &traversal_output[(decision * TRAVERSAL_RAY_COUNT + ray) * 8];
         const uint32_t *candidate = actual;
         const uint32_t *committed = actual + 4;
         const bool expected_committed = expected_hit && decision != 0;
         if (candidate[0] != expected_hit ||
             (expected_hit &&
              (candidate[1] != target_primitive ||
               candidate[2] != 0x80000000u ||
               !close_enough(float_from_bits(candidate[3]), 1.2f))) ||
             committed[0] != expected_committed ||
             (expected_committed &&
              (committed[1] != candidate[1] ||
               committed[2] != candidate[2] ||
               committed[3] != candidate[3])) ||
             (!expected_committed &&
              (committed[1] != 0xffffffffu ||
               committed[2] != 0xffffffffu || committed[3] != 0))) {
            printf("FAIL BLAS query decision=%u ray=%u candidate={%u,%u,0x%x,%g} "
                   "committed={%u,%u,0x%x,%g}\n",
                   decision, ray, candidate[0], candidate[1], candidate[2],
                   float_from_bits(candidate[3]), committed[0], committed[1],
                   committed[2], float_from_bits(committed[3]));
            return 1;
         }
      }
   }
   vkUnmapMemory(device, traversal_results.memory);
   printf("PASS GPU BLAS query probe: traversal candidates, reject, explicit "
          "confirm, opaque auto-commit, tMin/tMax\n");

   VkAccelerationStructureDeviceAddressInfoKHR blas_address_info = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = blas.handle,
   };
   VkDeviceAddress blas_address =
      vkGetAccelerationStructureDeviceAddressKHR(device, &blas_address_info);
   if (!blas_address) {
      printf("FAIL BLAS device address is zero\n");
      return 1;
   }

   VkAccelerationStructureInstanceKHR instance_descs[] = {
      {
         .transform =
            {
               .matrix =
                  {
                     {2.0f, 0.0f, 0.0f, 3.0f},
                     {0.0f, 0.0f, -0.5f, 1.0f},
                     {0.0f, 3.0f, 0.0f, 0.0f},
                  },
            },
         .instanceCustomIndex = 0x12345,
         .mask = 0x7f,
         .instanceShaderBindingTableRecordOffset = 0x23456,
         .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
         .accelerationStructureReference = blas_address,
      },
      {
         .transform =
            {
               .matrix =
                  {
                     {1.0f, 0.0f, 0.0f, 1100.0f},
                     {0.0f, 1.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 1.0f, 0.0f},
                  },
            },
         .instanceCustomIndex = 0x54321,
         .mask = 0x3f,
         .instanceShaderBindingTableRecordOffset = 0x34567,
         .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
         .accelerationStructureReference = blas_address,
      },
      {
         .transform =
            {
               .matrix =
                  {
                     {1.0f, 0.0f, 0.0f, 2200.0f},
                     {0.0f, 1.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 1.0f, 0.0f},
                  },
            },
         .instanceCustomIndex = 0x65432,
         .mask = 0x5f,
         .instanceShaderBindingTableRecordOffset = 0x45678,
         .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
         .accelerationStructureReference = blas_address,
      },
      {
         .transform =
            {
               .matrix =
                  {
                     {1.0f, 0.0f, 0.0f, 3300.0f},
                     {0.0f, 1.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 1.0f, 0.0f},
                  },
            },
         .instanceCustomIndex = 0x76543,
         .mask = 0x3d,
         .instanceShaderBindingTableRecordOffset = 0x56789,
         .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
         .accelerationStructureReference = blas_address,
      },
   };
   struct test_buffer instance_buffer = {0};
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, sizeof(instance_descs),
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &instance_buffer),
      "Create TLAS instance buffer");
   void *mapped_instance = NULL;
   CK(vkMapMemory(device, instance_buffer.memory, 0,
                  instance_buffer.allocation_size, 0, &mapped_instance),
      "Map TLAS instance buffer");
   memcpy(mapped_instance, instance_descs, sizeof(instance_descs));
   if (!(instance_buffer.memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = instance_buffer.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkFlushMappedMemoryRanges(device, 1, &range),
         "Flush TLAS instance buffer");
   }
   vkUnmapMemory(device, instance_buffer.memory);

   VkAccelerationStructureGeometryKHR tlas_geometry = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
      .geometry.instances =
         {
            .sType =
               VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data.deviceAddress = instance_buffer.address,
         },
   };
   VkAccelerationStructureBuildGeometryInfoKHR tlas_build = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
      .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1,
      .pGeometries = &tlas_geometry,
   };
   uint32_t tlas_primitive_count = TLAS_INSTANCE_COUNT;
   VkAccelerationStructureBuildSizesInfoKHR tlas_sizes = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
   };
   vkGetAccelerationStructureBuildSizesKHR(
      device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_build,
      &tlas_primitive_count, &tlas_sizes);
   if (!tlas_sizes.accelerationStructureSize || !tlas_sizes.buildScratchSize) {
      printf("FAIL invalid TLAS sizes AS=%llu scratch=%llu\n",
             (unsigned long long)tlas_sizes.accelerationStructureSize,
             (unsigned long long)tlas_sizes.buildScratchSize);
      return 1;
   }

   struct test_as tlas = {.size = tlas_sizes.accelerationStructureSize};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    tlas.size,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &tlas.storage),
      "Create TLAS storage");
   as_create_info.buffer = tlas.storage.buffer;
   as_create_info.size = tlas.size;
   as_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
   CK(vkCreateAccelerationStructureKHR(device, &as_create_info, NULL,
                                       &tlas.handle),
      "Create TLAS");
   struct test_buffer tlas_scratch = {0};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    tlas_sizes.buildScratchSize + scratch_alignment - 1,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &tlas_scratch),
      "Create TLAS scratch");
   tlas_build.dstAccelerationStructure = tlas.handle;
   tlas_build.scratchData.deviceAddress =
      (tlas_scratch.address + scratch_alignment - 1) & ~(scratch_alignment - 1);
   VkAccelerationStructureBuildRangeInfoKHR tlas_range = {
      .primitiveCount = TLAS_INSTANCE_COUNT,
   };
   const VkAccelerationStructureBuildRangeInfoKHR *tlas_ranges[] = {
      &tlas_range,
   };
   CK(vkAllocateCommandBuffers(device, &command_alloc_info, &command_buffer),
      "Allocate TLAS command buffer");
   CK(vkBeginCommandBuffer(command_buffer, &begin_info),
      "Begin TLAS command buffer");
   vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &tlas_build,
                                       tlas_ranges);
   CK(vkEndCommandBuffer(command_buffer), "End TLAS command buffer");
   submit_info.pCommandBuffers = &command_buffer;
   CK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
      "Submit TLAS build");
   CK(vkQueueWaitIdle(queue), "Wait for TLAS build");
   vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

   struct test_buffer tlas_readback = {0};
   const VkDeviceSize tlas_readback_size = tlas.size;
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, tlas_readback_size,
         VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &tlas_readback),
      "Create TLAS readback");
   CK(vkAllocateCommandBuffers(device, &command_alloc_info, &command_buffer),
      "Allocate TLAS readback command buffer");
   CK(vkBeginCommandBuffer(command_buffer, &begin_info),
      "Begin TLAS readback command buffer");
   VkBufferMemoryBarrier tlas_barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = tlas.storage.buffer,
      .offset = 0,
      .size = tlas.size,
   };
   vkCmdPipelineBarrier(
      command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &tlas_barrier, 0, NULL);
   VkBufferCopy tlas_copy = {.size = tlas_readback_size};
   vkCmdCopyBuffer(command_buffer, tlas.storage.buffer, tlas_readback.buffer, 1,
                   &tlas_copy);
   CK(vkEndCommandBuffer(command_buffer), "End TLAS readback command buffer");
   submit_info.pCommandBuffers = &command_buffer;
   CK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
      "Submit TLAS readback");
   CK(vkQueueWaitIdle(queue), "Wait for TLAS readback");
   vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
   void *mapped_tlas = NULL;
   CK(vkMapMemory(device, tlas_readback.memory, 0, tlas_readback.allocation_size,
                  0, &mapped_tlas),
      "Map TLAS readback");
   if (!(tlas_readback.memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = tlas_readback.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &range),
         "Invalidate TLAS readback");
   }
   struct ir_header_prefix tlas_header;
   memcpy(&tlas_header, mapped_tlas, sizeof(tlas_header));
   struct ir_instance_node_layout tlas_instances[TLAS_INSTANCE_COUNT];
   memcpy(tlas_instances, (uint8_t *)mapped_tlas + node_offset,
          sizeof(tlas_instances));
   if (tlas_header.active_leaf_count != TLAS_INSTANCE_COUNT) {
      printf("FAIL TLAS header leaves=%u expected=%u\n",
             tlas_header.active_leaf_count, TLAS_INSTANCE_COUNT);
      return 1;
   }
   if (tlas_header.internal_node_count != TLAS_INTERNAL_NODE_COUNT) {
      printf("FAIL TLAS internal nodes=%u expected=%u\n",
             tlas_header.internal_node_count, TLAS_INTERNAL_NODE_COUNT);
      return 1;
   }
   uint32_t tlas_internal_parent_count[TLAS_INTERNAL_NODE_COUNT] = {0};
   uint32_t tlas_instance_parent_count[TLAS_INSTANCE_COUNT] = {0};
   const VkDeviceSize tlas_internal_offset =
      node_offset +
      TLAS_INSTANCE_COUNT * sizeof(struct ir_instance_node_layout);
   for (uint32_t node = 0; node < TLAS_INTERNAL_NODE_COUNT; node++) {
      struct ir_box_node_layout internal;
      memcpy(&internal,
             (uint8_t *)mapped_tlas + tlas_internal_offset +
                node * sizeof(internal),
             sizeof(internal));
      for (uint32_t child = 0; child < 2; child++) {
         const uint32_t child_id = internal.children[child];
         if (child_id == UINT32_MAX) {
            printf("FAIL TLAS internal node=%u has missing child\n", node);
            return 1;
         }
         const uint32_t child_offset = child_id & ~3u;
         if ((child_id & 3u) == 2u) {
            if (child_offset % sizeof(struct ir_instance_node_layout) != 0 ||
                child_offset / sizeof(struct ir_instance_node_layout) >=
                   TLAS_INSTANCE_COUNT) {
               printf("FAIL TLAS child has invalid instance offset=%u\n",
                      child_offset);
               return 1;
            }
            tlas_instance_parent_count[
               child_offset / sizeof(struct ir_instance_node_layout)]++;
         } else if ((child_id & 3u) == 1u) {
            if (child_offset <
                   TLAS_INSTANCE_COUNT *
                      sizeof(struct ir_instance_node_layout) ||
                (child_offset -
                 TLAS_INSTANCE_COUNT *
                    sizeof(struct ir_instance_node_layout)) %
                   sizeof(struct ir_box_node_layout) != 0 ||
                (child_offset -
                 TLAS_INSTANCE_COUNT *
                    sizeof(struct ir_instance_node_layout)) /
                      sizeof(struct ir_box_node_layout) >=
                   TLAS_INTERNAL_NODE_COUNT) {
               printf("FAIL TLAS child has invalid internal offset=%u\n",
                      child_offset);
               return 1;
            }
            tlas_internal_parent_count[
               (child_offset -
                TLAS_INSTANCE_COUNT *
                   sizeof(struct ir_instance_node_layout)) /
               sizeof(struct ir_box_node_layout)]++;
         } else {
            printf("FAIL TLAS child has unsupported tag=%u\n", child_id & 3u);
            return 1;
         }
      }
   }
   uint32_t tlas_root_id = UINT32_MAX;
   uint32_t tlas_root_count = 0;
   for (uint32_t node = 0; node < TLAS_INTERNAL_NODE_COUNT; node++) {
      if (tlas_internal_parent_count[node] == 0) {
         tlas_root_id =
            (uint32_t)(TLAS_INSTANCE_COUNT *
                          sizeof(struct ir_instance_node_layout) +
                       node * sizeof(struct ir_box_node_layout)) |
            1u;
         tlas_root_count++;
      } else if (tlas_internal_parent_count[node] != 1) {
         printf("FAIL TLAS internal node=%u parents=%u\n", node,
                tlas_internal_parent_count[node]);
         return 1;
      }
   }
   if (tlas_root_count != 1 || tlas_root_id == UINT32_MAX) {
      printf("FAIL TLAS root candidates=%u\n", tlas_root_count);
      return 1;
   }
   for (uint32_t instance = 0; instance < TLAS_INSTANCE_COUNT; instance++) {
      if (tlas_instance_parent_count[instance] != 1) {
         printf("FAIL TLAS instance=%u parents=%u expected=1\n", instance,
                tlas_instance_parent_count[instance]);
         return 1;
      }
   }
   vkUnmapMemory(device, tlas_readback.memory);
   for (uint32_t i = 0; i < 3; i++) {
      float min_bound = decode_emulated_float(tlas_header.min_bounds[i]);
      float max_bound = decode_emulated_float(tlas_header.max_bounds[i]);
      const float expected_tlas_min = expected_tlas_bounds[i];
      const float expected_tlas_max = expected_tlas_bounds[i + 3];
      if (!close_enough(min_bound, expected_tlas_min) ||
          !close_enough(max_bound, expected_tlas_max)) {
         printf("FAIL TLAS bounds axis=%u got=[%g,%g] expected=[%g,%g]\n", i,
                min_bound, max_bound, expected_tlas_min, expected_tlas_max);
         return 1;
      }
   }
   const float *expected_instance_bounds[] = {
      first_tlas_instance_bounds,
      second_tlas_instance_bounds,
      third_tlas_instance_bounds,
      fourth_tlas_instance_bounds,
   };
   for (uint32_t i = 0; i < 6; i++) {
      for (uint32_t instance = 0; instance < TLAS_INSTANCE_COUNT; instance++) {
         const float instance_bound =
            i < 3 ? tlas_instances[instance].bounds.min_bounds[i]
                  : tlas_instances[instance].bounds.max_bounds[i - 3];
         if (!close_enough(instance_bound,
                           expected_instance_bounds[instance][i])) {
            printf("FAIL TLAS instance=%u bound[%u]=%g expected=%g\n",
                   instance, i, instance_bound,
                   expected_instance_bounds[instance][i]);
            return 1;
         }
      }
   }
   const float expected_otw_matrix[3][4] = {
      {2.0f, 0.0f, 0.0f, 3.0f},
      {0.0f, 0.0f, -0.5f, 1.0f},
      {0.0f, 3.0f, 0.0f, 0.0f},
   };
   const float second_expected_matrix[3][4] = {
      {1.0f, 0.0f, 0.0f, 1100.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
   };
   const float third_expected_matrix[3][4] = {
      {1.0f, 0.0f, 0.0f, 2200.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
   };
   const float fourth_expected_matrix[3][4] = {
      {1.0f, 0.0f, 0.0f, 3300.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
   };
   if (tlas_instances[0].base_ptr != blas_address ||
       tlas_instances[0].custom_instance_and_mask != 0x7f012345u ||
       tlas_instances[0].sbt_offset_and_flags != 0x01023456u ||
       tlas_instances[0].instance_id != 0 ||
       memcmp(tlas_instances[0].otw_matrix, expected_otw_matrix,
              sizeof(expected_otw_matrix)) != 0 ||
       tlas_instances[1].base_ptr != blas_address ||
       tlas_instances[1].custom_instance_and_mask != 0x3f054321u ||
       tlas_instances[1].sbt_offset_and_flags != 0x01034567u ||
       tlas_instances[1].instance_id != 1 ||
       memcmp(tlas_instances[1].otw_matrix, second_expected_matrix,
              sizeof(second_expected_matrix)) != 0 ||
       tlas_instances[2].base_ptr != blas_address ||
       tlas_instances[2].custom_instance_and_mask != 0x5f065432u ||
       tlas_instances[2].sbt_offset_and_flags != 0x01045678u ||
       tlas_instances[2].instance_id != 2 ||
       memcmp(tlas_instances[2].otw_matrix, third_expected_matrix,
              sizeof(third_expected_matrix)) != 0 ||
       tlas_instances[3].base_ptr != blas_address ||
       tlas_instances[3].custom_instance_and_mask != 0x3d076543u ||
       tlas_instances[3].sbt_offset_and_flags != 0x01056789u ||
       tlas_instances[3].instance_id != 3 ||
       memcmp(tlas_instances[3].otw_matrix, fourth_expected_matrix,
              sizeof(fourth_expected_matrix)) != 0) {
      printf("FAIL TLAS instance IR fields do not match the four inputs\n");
      return 1;
   }

   VkAccelerationStructureDeviceAddressInfoKHR tlas_address_info = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = tlas.handle,
   };
   VkDeviceAddress tlas_address =
      vkGetAccelerationStructureDeviceAddressKHR(device, &tlas_address_info);
   if (!tlas_address) {
      printf("FAIL TLAS device address is zero\n");
      return 1;
   }

   traversal_buffer_infos[2].buffer = tlas.storage.buffer;
   traversal_buffer_infos[2].range = tlas.size;
   VkWriteDescriptorSet tlas_traversal_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = traversal_set,
      .dstBinding = 2,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &traversal_buffer_infos[2],
   };
   vkUpdateDescriptorSets(device, 1, &tlas_traversal_write, 0, NULL);

   VkBufferMemoryBarrier traversal_as_barriers[] = {
      as_to_compute,
      {
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .buffer = tlas.storage.buffer,
         .offset = 0,
         .size = tlas.size,
      },
   };
   CK(vkAllocateCommandBuffers(device, &command_alloc_info, &command_buffer),
      "Allocate TLAS instance traversal command buffer");
   CK(vkBeginCommandBuffer(command_buffer, &begin_info),
      "Begin TLAS instance traversal command buffer");
   vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 2,
                        traversal_as_barriers, 0, NULL);
   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                     traversal_pipeline);
   vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           traversal_layout, 0, 1, &traversal_set, 0, NULL);
   uint32_t tlas_traversal_push[] = {
      root_node_id,
      TRAVERSAL_RAY_COUNT,
      0,
      0x42c80000,
      1,
      (uint32_t)blas_address,
      (uint32_t)(blas_address >> 32),
      tlas_root_id,
      0,
   };
   for (uint32_t decision = 0; decision < 3; decision++) {
      tlas_traversal_push[8] = decision;
      vkCmdPushConstants(command_buffer, traversal_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(tlas_traversal_push), tlas_traversal_push);
      vkCmdDispatch(command_buffer, TRAVERSAL_RAY_COUNT, 1, 1);
   }
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                        &compute_to_host, 0, NULL);
   CK(vkEndCommandBuffer(command_buffer),
      "End TLAS instance traversal command buffer");
   submit_info.pCommandBuffers = &command_buffer;
   CK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
      "Submit TLAS instance traversal");
   CK(vkQueueWaitIdle(queue), "Wait for TLAS instance traversal");
   vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

   CK(vkMapMemory(device, traversal_results.memory, 0,
                  traversal_results.allocation_size, 0,
                  (void **)&traversal_output),
      "Map TLAS instance traversal results");
   if (!(traversal_results.memory_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange traversal_range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = traversal_results.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &traversal_range),
         "Invalidate TLAS instance traversal results");
   }
   for (uint32_t decision = 0; decision < 3; decision++) {
      for (uint32_t ray = 0; ray < TRAVERSAL_RAY_COUNT; ray++) {
         const uint32_t local_ray = ray & 15u;
         const uint32_t instance_index = ray / 16u;
         const bool expected_hit =
            (local_ray & 3u) == 0 && local_ray != 0 &&
            !(instance_index == 1 && local_ray == 4) &&
            !(instance_index == 2 && local_ray == 8) &&
            !(instance_index == 3 && local_ray == 4);
         const uint32_t target_primitive = (local_ray / 4) * 64;
         const uint32_t *actual =
            &traversal_output[(decision * TRAVERSAL_RAY_COUNT + ray) * 8];
         const uint32_t *candidate = actual;
         const uint32_t *committed = actual + 4;
         const bool expected_committed = expected_hit && decision != 0;
         if (candidate[0] != expected_hit ||
             (expected_hit &&
              (candidate[1] != target_primitive ||
               candidate[2] != 0x80000000u ||
               !close_enough(float_from_bits(candidate[3]),
                             instance_index == 0 ? 2.1f : 1.2f))) ||
             committed[0] != expected_committed ||
             (expected_committed &&
              (committed[1] != candidate[1] ||
               committed[2] != candidate[2] ||
               committed[3] != candidate[3])) ||
             (!expected_committed &&
              (committed[1] != 0xffffffffu ||
               committed[2] != 0xffffffffu || committed[3] != 0))) {
            printf("FAIL TLAS query decision=%u ray=%u candidate={%u,%u,0x%x,%g} "
                   "committed={%u,%u,0x%x,%g}\n",
                   decision, ray, candidate[0], candidate[1], candidate[2],
                   float_from_bits(candidate[3]), committed[0], committed[1],
                   committed[2], float_from_bits(committed[3]));
            return 1;
         }
      }
   }
   vkUnmapMemory(device, traversal_results.memory);
   printf("PASS GPU TLAS/BLAS query probe: 4 instances, masks, transforms, "
          "candidate reject/confirm and opaque auto-commit\n");

   vkDestroyPipeline(device, traversal_pipeline, NULL);
   vkDestroyPipelineLayout(device, traversal_layout, NULL);
   vkDestroyShaderModule(device, traversal_shader, NULL);
   vkDestroyDescriptorPool(device, traversal_pool, NULL);
   vkDestroyDescriptorSetLayout(device, traversal_set_layout, NULL);
   vkDestroyBuffer(device, traversal_results.buffer, NULL);
   vkFreeMemory(device, traversal_results.memory, NULL);

   struct test_as empty_blas = {
      .size = empty_blas_sizes.accelerationStructureSize,
   };
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    empty_blas.size,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &empty_blas.storage),
      "Create empty BLAS storage");
   as_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
   as_create_info.buffer = empty_blas.storage.buffer;
   as_create_info.size = empty_blas.size;
   CK(vkCreateAccelerationStructureKHR(device, &as_create_info, NULL,
                                       &empty_blas.handle),
      "Create empty BLAS");
   if (empty_blas_sizes.buildScratchSize >
       blas_scratch.allocation_size -
          (blas_scratch_address - blas_scratch.address)) {
      printf("FAIL empty BLAS scratch size exceeds test allocation\n");
      return 1;
   }

   VkAccelerationStructureBuildGeometryInfoKHR empty_blas_build = blas_build;
   empty_blas_build.dstAccelerationStructure = empty_blas.handle;
   VkAccelerationStructureBuildRangeInfoKHR empty_blas_range = {
      .primitiveCount = 0,
   };
   const VkAccelerationStructureBuildRangeInfoKHR *empty_blas_ranges[] = {
      &empty_blas_range,
   };
   VkResult empty_build_result = submit_as_build(
      device, command_pool, queue, vkAllocateCommandBuffers,
      vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
      vkCmdPipelineBarrier, vkCmdBuildAccelerationStructuresKHR, vkQueueSubmit,
      vkQueueWaitIdle, &empty_blas_build, empty_blas_ranges);
   if (empty_build_result != VK_SUCCESS) {
      printf("FAIL empty BLAS build result=%d\n", empty_build_result);
      return 1;
   }

   struct test_buffer empty_blas_readback = {0};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    empty_blas.size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &empty_blas_readback),
      "Create empty BLAS readback");
   CK(copy_as_to_readback(device, command_pool, queue, vkAllocateCommandBuffers,
                          vkFreeCommandBuffers, vkBeginCommandBuffer,
                          vkEndCommandBuffer, vkCmdPipelineBarrier,
                          vkCmdCopyBuffer, vkQueueSubmit, vkQueueWaitIdle,
                          empty_blas.storage.buffer, empty_blas_readback.buffer,
                          empty_blas.size),
      "Read back empty BLAS");
   void *mapped_empty_blas = NULL;
   CK(vkMapMemory(device, empty_blas_readback.memory, 0,
                  empty_blas_readback.allocation_size, 0, &mapped_empty_blas),
      "Map empty BLAS readback");
   if (!(empty_blas_readback.memory_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = empty_blas_readback.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &range),
         "Invalidate empty BLAS readback");
   }
   struct ir_header_prefix empty_blas_header;
   memcpy(&empty_blas_header, mapped_empty_blas, sizeof(empty_blas_header));
   vkUnmapMemory(device, empty_blas_readback.memory);
   if (empty_blas_header.active_leaf_count != 0) {
      printf("FAIL empty BLAS active leaves=%u expected=0\n",
             empty_blas_header.active_leaf_count);
      return 1;
   }
   printf("PASS empty BLAS build/readback\n");

   uint32_t single_primitive_count = 1;
   VkAccelerationStructureBuildSizesInfoKHR single_blas_sizes = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
   };
   vkGetAccelerationStructureBuildSizesKHR(
      device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build,
      &single_primitive_count, &single_blas_sizes);
   if (!single_blas_sizes.accelerationStructureSize ||
       !single_blas_sizes.buildScratchSize ||
       single_blas_sizes.buildScratchSize >
          blas_scratch.allocation_size -
             (blas_scratch_address - blas_scratch.address)) {
      printf("FAIL invalid single-triangle BLAS sizes AS=%llu scratch=%llu\n",
             (unsigned long long)single_blas_sizes.accelerationStructureSize,
             (unsigned long long)single_blas_sizes.buildScratchSize);
      return 1;
   }
   struct test_as single_blas = {
      .size = single_blas_sizes.accelerationStructureSize,
   };
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, single_blas.size,
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &single_blas.storage),
      "Create single-triangle BLAS storage");
   as_create_info.buffer = single_blas.storage.buffer;
   as_create_info.size = single_blas.size;
   CK(vkCreateAccelerationStructureKHR(device, &as_create_info, NULL,
                                       &single_blas.handle),
      "Create single-triangle BLAS");

   VkAccelerationStructureBuildGeometryInfoKHR single_blas_build = blas_build;
   single_blas_build.dstAccelerationStructure = single_blas.handle;
   VkAccelerationStructureGeometryKHR single_blas_geometry = blas_geometry;
   single_blas_geometry.flags = 0;
   single_blas_build.pGeometries = &single_blas_geometry;
   VkAccelerationStructureBuildRangeInfoKHR single_blas_range = {
      .primitiveCount = 1,
   };
   const VkAccelerationStructureBuildRangeInfoKHR *single_blas_ranges[] = {
      &single_blas_range,
   };
   VkResult single_build_result = submit_as_build(
      device, command_pool, queue, vkAllocateCommandBuffers,
      vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
      vkCmdPipelineBarrier, vkCmdBuildAccelerationStructuresKHR, vkQueueSubmit,
      vkQueueWaitIdle, &single_blas_build, single_blas_ranges);
   if (single_build_result != VK_SUCCESS) {
      printf("FAIL single-triangle BLAS build result=%d\n", single_build_result);
      return 1;
   }

   struct test_buffer single_blas_readback = {0};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    single_blas.size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &single_blas_readback),
      "Create single-triangle BLAS readback");
   CK(copy_as_to_readback(device, command_pool, queue, vkAllocateCommandBuffers,
                          vkFreeCommandBuffers, vkBeginCommandBuffer,
                          vkEndCommandBuffer, vkCmdPipelineBarrier,
                          vkCmdCopyBuffer, vkQueueSubmit, vkQueueWaitIdle,
                          single_blas.storage.buffer,
                          single_blas_readback.buffer, single_blas.size),
      "Read back single-triangle BLAS");
   void *mapped_single_blas = NULL;
   CK(vkMapMemory(device, single_blas_readback.memory, 0,
                  single_blas_readback.allocation_size, 0, &mapped_single_blas),
      "Map single-triangle BLAS readback");
   if (!(single_blas_readback.memory_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = single_blas_readback.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &range),
         "Invalidate single-triangle BLAS readback");
   }
   struct ir_header_prefix single_blas_header;
   float single_blas_bounds[6];
   struct ir_box_node_layout single_blas_root;
   memcpy(&single_blas_header, mapped_single_blas, sizeof(single_blas_header));
   memcpy(single_blas_bounds, (uint8_t *)mapped_single_blas + node_offset,
          sizeof(single_blas_bounds));
   memcpy(&single_blas_root,
          (uint8_t *)mapped_single_blas + node_offset +
             sizeof(struct ir_triangle_node_layout),
          sizeof(single_blas_root));
   vkUnmapMemory(device, single_blas_readback.memory);
   const float expected_single_bounds[] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
   if (single_blas_header.active_leaf_count != 1) {
      printf("FAIL single-triangle BLAS active leaves=%u expected=1\n",
             single_blas_header.active_leaf_count);
      return 1;
   }
   if (single_blas_header.internal_node_count != 1 ||
       single_blas_root.children[0] != 0 ||
       single_blas_root.children[1] != 0xffffffffu) {
      printf("FAIL single-triangle BLAS root count=%u children={0x%x,0x%x}\n",
             single_blas_header.internal_node_count,
             single_blas_root.children[0], single_blas_root.children[1]);
      return 1;
   }
   for (uint32_t i = 0; i < 6; i++) {
      if (!close_enough(single_blas_bounds[i], expected_single_bounds[i])) {
         printf("FAIL single-triangle BLAS bound[%u]=%g expected=%g\n", i,
                single_blas_bounds[i], expected_single_bounds[i]);
         return 1;
      }
   }
   printf("PASS single-triangle BLAS build/readback\n");

   VkAccelerationStructureDeviceAddressInfoKHR query_blas_address_info = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = single_blas.handle,
   };
   VkDeviceAddress query_blas_address =
      vkGetAccelerationStructureDeviceAddressKHR(device,
                                                 &query_blas_address_info);
   VkAccelerationStructureInstanceKHR query_instance = {
      .transform =
         {
            .matrix =
               {
                  {0.0f, -2.0f, 0.0f, 3.0f},
                  {0.5f, 0.0f, 0.0f, 0.0f},
                  {0.0f, 0.0f, 1.0f, 0.0f},
               },
         },
      .instanceCustomIndex = 0x13579,
      .mask = 0x7f,
      .instanceShaderBindingTableRecordOffset = 0x23456,
      .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
      .accelerationStructureReference = query_blas_address,
   };
   struct test_buffer query_instance_buffer = {0};
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, sizeof(query_instance),
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &query_instance_buffer),
      "Create one-instance query TLAS input");
   void *mapped_query_instance = NULL;
   CK(vkMapMemory(device, query_instance_buffer.memory, 0,
                  query_instance_buffer.allocation_size, 0,
                  &mapped_query_instance),
      "Map one-instance query TLAS input");
   memcpy(mapped_query_instance, &query_instance, sizeof(query_instance));
   if (!(query_instance_buffer.memory_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = query_instance_buffer.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkFlushMappedMemoryRanges(device, 1, &range),
         "Flush one-instance query TLAS input");
   }
   vkUnmapMemory(device, query_instance_buffer.memory);

   VkAccelerationStructureGeometryKHR query_tlas_geometry = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
      .geometry.instances =
         {
            .sType =
               VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data.deviceAddress = query_instance_buffer.address,
         },
   };
   VkAccelerationStructureBuildGeometryInfoKHR query_tlas_build = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
      .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1,
      .pGeometries = &query_tlas_geometry,
   };
   uint32_t query_tlas_primitive_count = 1;
   VkAccelerationStructureBuildSizesInfoKHR query_tlas_sizes = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
   };
   vkGetAccelerationStructureBuildSizesKHR(
      device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &query_tlas_build, &query_tlas_primitive_count, &query_tlas_sizes);
   if (!query_tlas_sizes.accelerationStructureSize ||
       !query_tlas_sizes.buildScratchSize) {
      printf("FAIL invalid one-instance query TLAS sizes AS=%llu scratch=%llu\n",
             (unsigned long long)query_tlas_sizes.accelerationStructureSize,
             (unsigned long long)query_tlas_sizes.buildScratchSize);
      return 1;
   }
   struct test_as query_tlas = {
      .size = query_tlas_sizes.accelerationStructureSize,
   };
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, query_tlas.size,
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &query_tlas.storage),
      "Create one-instance query TLAS storage");
   as_create_info.buffer = query_tlas.storage.buffer;
   as_create_info.size = query_tlas.size;
   as_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
   CK(vkCreateAccelerationStructureKHR(device, &as_create_info, NULL,
                                       &query_tlas.handle),
      "Create one-instance query TLAS");
   struct test_buffer query_tlas_scratch = {0};
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device,
         query_tlas_sizes.buildScratchSize + scratch_alignment - 1,
         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
         0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &query_tlas_scratch),
      "Create one-instance query TLAS scratch");
   query_tlas_build.dstAccelerationStructure = query_tlas.handle;
   query_tlas_build.scratchData.deviceAddress =
      (query_tlas_scratch.address + scratch_alignment - 1) &
      ~(scratch_alignment - 1);
   VkAccelerationStructureBuildRangeInfoKHR query_tlas_range = {
      .primitiveCount = 1,
   };
   const VkAccelerationStructureBuildRangeInfoKHR *query_tlas_ranges[] = {
      &query_tlas_range,
   };
   VkResult query_tlas_build_result = submit_as_build(
      device, command_pool, queue, vkAllocateCommandBuffers,
      vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
      vkCmdPipelineBarrier, vkCmdBuildAccelerationStructuresKHR, vkQueueSubmit,
      vkQueueWaitIdle, &query_tlas_build, query_tlas_ranges);
   if (query_tlas_build_result != VK_SUCCESS) {
      printf("FAIL one-instance query TLAS build result=%d\n",
             query_tlas_build_result);
      return 1;
   }
   printf("PASS one-instance query TLAS build: translated non-opaque BLAS\n");

   VkAccelerationStructureGeometryKHR multi_geometries[] = {
      blas_geometry,
      blas_geometry,
   };
   multi_geometries[1].flags = 0;
   VkAccelerationStructureBuildGeometryInfoKHR multi_blas_build = blas_build;
   multi_blas_build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
   multi_blas_build.geometryCount = 2;
   multi_blas_build.pGeometries = multi_geometries;
   uint32_t multi_max_primitive_counts[] = {1, 1};
   VkAccelerationStructureBuildSizesInfoKHR multi_blas_sizes = {
      .sType =
         VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
   };
   vkGetAccelerationStructureBuildSizesKHR(
      device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &multi_blas_build, multi_max_primitive_counts, &multi_blas_sizes);
   if (!multi_blas_sizes.accelerationStructureSize ||
       !multi_blas_sizes.buildScratchSize) {
      printf("FAIL invalid two-geometry BLAS sizes AS=%llu scratch=%llu\n",
             (unsigned long long)multi_blas_sizes.accelerationStructureSize,
             (unsigned long long)multi_blas_sizes.buildScratchSize);
      return 1;
   }

   struct test_as multi_blas = {
      .size = multi_blas_sizes.accelerationStructureSize,
   };
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, multi_blas.size,
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &multi_blas.storage),
      "Create two-geometry BLAS storage");
   as_create_info.buffer = multi_blas.storage.buffer;
   as_create_info.size = multi_blas.size;
   CK(vkCreateAccelerationStructureKHR(device, &as_create_info, NULL,
                                       &multi_blas.handle),
      "Create two-geometry BLAS");

   struct test_buffer multi_blas_scratch = {0};
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, multi_blas_sizes.buildScratchSize + scratch_alignment - 1,
         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
         0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &multi_blas_scratch),
      "Create two-geometry BLAS scratch");
   multi_blas_build.dstAccelerationStructure = multi_blas.handle;
   multi_blas_build.scratchData.deviceAddress =
      (multi_blas_scratch.address + scratch_alignment - 1) &
      ~(scratch_alignment - 1);
   VkAccelerationStructureBuildRangeInfoKHR multi_ranges_data[] = {
      {.primitiveCount = 1, .firstVertex = 0},
      {.primitiveCount = 1,
       .firstVertex = (TRIANGLE_COUNT - 1) * 3},
   };
   const VkAccelerationStructureBuildRangeInfoKHR *multi_ranges[] = {
      &multi_ranges_data[0],
      &multi_ranges_data[1],
   };
   VkResult multi_build_result = submit_as_build(
      device, command_pool, queue, vkAllocateCommandBuffers,
      vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
      vkCmdPipelineBarrier, vkCmdBuildAccelerationStructuresKHR, vkQueueSubmit,
      vkQueueWaitIdle, &multi_blas_build, multi_ranges);
   if (multi_build_result != VK_SUCCESS) {
      printf("FAIL two-geometry BLAS build result=%d\n", multi_build_result);
      return 1;
   }

   struct test_buffer multi_blas_readback = {0};
   CK(create_buffer(
         vkCreateBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
         vkBindBufferMemory, vkGetBufferDeviceAddress, &memory_properties,
         device, multi_blas.size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &multi_blas_readback),
      "Create two-geometry BLAS readback");
   CK(copy_as_to_readback(
         device, command_pool, queue, vkAllocateCommandBuffers,
         vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
         vkCmdPipelineBarrier, vkCmdCopyBuffer, vkQueueSubmit,
         vkQueueWaitIdle, multi_blas.storage.buffer,
         multi_blas_readback.buffer, multi_blas.size),
      "Read back two-geometry BLAS");
   void *mapped_multi_blas = NULL;
   CK(vkMapMemory(device, multi_blas_readback.memory, 0,
                  multi_blas_readback.allocation_size, 0, &mapped_multi_blas),
      "Map two-geometry BLAS readback");
   if (!(multi_blas_readback.memory_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = multi_blas_readback.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &range),
         "Invalidate two-geometry BLAS readback");
   }
   struct ir_header_prefix multi_blas_header;
   struct ir_triangle_node_layout multi_blas_nodes[2];
   memcpy(&multi_blas_header, mapped_multi_blas,
          sizeof(multi_blas_header));
   memcpy(multi_blas_nodes,
          (uint8_t *)mapped_multi_blas + node_offset,
          sizeof(multi_blas_nodes));
   vkUnmapMemory(device, multi_blas_readback.memory);
   const float expected_multi_bounds[][6] = {
      {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
      {512.0f, 0.0f, 0.0f, 513.0f, 1.0f, 1.0f},
   };
   const uint32_t expected_multi_triangle_ids[] = {0, 0};
   const float expected_multi_aggregate_min[] = {0.0f, 0.0f, 0.0f};
   const float expected_multi_aggregate_max[] = {513.0f, 1.0f, 1.0f};
   if (multi_blas_header.active_leaf_count != 2) {
      printf("FAIL two-geometry BLAS active leaves=%u expected=2\n",
             multi_blas_header.active_leaf_count);
      return 1;
   }
   for (uint32_t axis = 0; axis < 3; axis++) {
      float min_bound =
         decode_emulated_float(multi_blas_header.min_bounds[axis]);
      float max_bound =
         decode_emulated_float(multi_blas_header.max_bounds[axis]);
      if (!close_enough(min_bound, expected_multi_aggregate_min[axis]) ||
          !close_enough(max_bound, expected_multi_aggregate_max[axis])) {
         printf("FAIL two-geometry BLAS aggregate bounds axis=%u "
                "got=[%g,%g] expected=[%g,%g]\n",
                axis, min_bound, max_bound,
                expected_multi_aggregate_min[axis],
                expected_multi_aggregate_max[axis]);
         return 1;
      }
   }
   for (uint32_t geometry = 0; geometry < 2; geometry++) {
      const struct ir_triangle_node_layout *node = &multi_blas_nodes[geometry];
      const float actual_bounds[] = {
         node->bounds.min_bounds[0],
         node->bounds.min_bounds[1],
         node->bounds.min_bounds[2],
         node->bounds.max_bounds[0],
         node->bounds.max_bounds[1],
         node->bounds.max_bounds[2],
      };
      for (uint32_t component = 0; component < 6; component++) {
         if (!close_enough(actual_bounds[component],
                           expected_multi_bounds[geometry][component])) {
            printf("FAIL two-geometry BLAS leaf=%u bound[%u]=%g expected=%g\n",
                   geometry, component, actual_bounds[component],
                   expected_multi_bounds[geometry][component]);
            return 1;
         }
      }
      const uint32_t expected_geometry_flags =
         geometry == 0 ? 0x80000000u : 0;
      if (node->triangle_id != expected_multi_triangle_ids[geometry] ||
          node->geometry_id_and_flags !=
             (expected_geometry_flags | geometry)) {
         printf("FAIL two-geometry BLAS leaf=%u triangle=%u geometry=0x%x\n",
                geometry, node->triangle_id, node->geometry_id_and_flags);
         return 1;
      }
   }
   printf("PASS two-geometry BLAS build/readback\n");

   VkAccelerationStructureBuildGeometryInfoKHR allow_update_build =
      multi_blas_build;
   allow_update_build.flags =
      VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
   VkResult allow_update_result = submit_as_build(
      device, command_pool, queue, vkAllocateCommandBuffers,
      vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
      vkCmdPipelineBarrier, vkCmdBuildAccelerationStructuresKHR, vkQueueSubmit,
      vkQueueWaitIdle, &allow_update_build, multi_ranges);
   if (allow_update_result != VK_ERROR_FEATURE_NOT_PRESENT) {
      printf("FAIL allow-update BLAS build result=%d expected=%d\n",
             allow_update_result, VK_ERROR_FEATURE_NOT_PRESENT);
      return 1;
   }

   VkAccelerationStructureBuildGeometryInfoKHR allow_compaction_build =
      multi_blas_build;
   allow_compaction_build.flags =
      VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
   VkResult allow_compaction_result = submit_as_build(
      device, command_pool, queue, vkAllocateCommandBuffers,
      vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
      vkCmdPipelineBarrier, vkCmdBuildAccelerationStructuresKHR, vkQueueSubmit,
      vkQueueWaitIdle, &allow_compaction_build, multi_ranges);
   if (allow_compaction_result != VK_ERROR_FEATURE_NOT_PRESENT) {
      printf("FAIL allow-compaction BLAS build result=%d expected=%d\n",
             allow_compaction_result, VK_ERROR_FEATURE_NOT_PRESENT);
      return 1;
   }

   VkAccelerationStructureBuildGeometryInfoKHR update_build = multi_blas_build;
   update_build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
   update_build.srcAccelerationStructure = multi_blas.handle;
   VkResult update_result = submit_as_build(
      device, command_pool, queue, vkAllocateCommandBuffers,
      vkFreeCommandBuffers, vkBeginCommandBuffer, vkEndCommandBuffer,
      vkCmdPipelineBarrier, vkCmdBuildAccelerationStructuresKHR, vkQueueSubmit,
      vkQueueWaitIdle, &update_build, multi_ranges);
   if (update_result != VK_ERROR_FEATURE_NOT_PRESENT) {
      printf("FAIL BLAS update result=%d expected=%d\n", update_result,
             VK_ERROR_FEATURE_NOT_PRESENT);
      return 1;
   }
   printf("PASS unsupported AS update/compaction requests rejected explicitly\n");

   VkDescriptorSetLayoutBinding as_bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
         .descriptorCount = 2,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
   };
   VkDescriptorSetLayoutCreateInfo as_set_layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = sizeof(as_bindings) / sizeof(as_bindings[0]),
      .pBindings = as_bindings,
   };
   VkDescriptorSetLayout as_set_layout;
   CK(vkCreateDescriptorSetLayout(device, &as_set_layout_info, NULL,
                                  &as_set_layout),
      "Create AS descriptor set layout");

   VkDescriptorPoolSize as_pool_sizes[] = {
      {
         .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
         .descriptorCount = 4,
      },
      {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 2,
      },
   };
   VkDescriptorPoolCreateInfo as_pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 2,
      .poolSizeCount = sizeof(as_pool_sizes) / sizeof(as_pool_sizes[0]),
      .pPoolSizes = as_pool_sizes,
   };
   VkDescriptorPool as_pool;
   CK(vkCreateDescriptorPool(device, &as_pool_info, NULL, &as_pool),
      "Create AS descriptor pool");

   VkDescriptorSetLayout as_set_layouts[] = {
      as_set_layout,
      as_set_layout,
   };
   VkDescriptorSetAllocateInfo as_set_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = as_pool,
      .descriptorSetCount = 2,
      .pSetLayouts = as_set_layouts,
   };
   VkDescriptorSet as_sets[2];
   CK(vkAllocateDescriptorSets(device, &as_set_alloc_info, as_sets),
      "Allocate AS descriptor sets");

   VkAccelerationStructureKHR descriptor_as[] = {
      multi_blas.handle,
      blas.handle,
   };
   VkWriteDescriptorSetAccelerationStructureKHR as_write_info = {
      .sType =
         VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
      .accelerationStructureCount = 2,
      .pAccelerationStructures = descriptor_as,
   };
   VkWriteDescriptorSet as_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = &as_write_info,
      .dstSet = as_sets[0],
      .dstBinding = 0,
      .descriptorCount = 2,
      .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
   };
   vkUpdateDescriptorSets(device, 1, &as_write, 0, NULL);

   VkCopyDescriptorSet as_copy = {
      .sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
      .srcSet = as_sets[0],
      .srcBinding = 0,
      .dstSet = as_sets[1],
      .dstBinding = 0,
      .descriptorCount = 2,
   };
   vkUpdateDescriptorSets(device, 0, NULL, 1, &as_copy);

   VkDescriptorUpdateTemplateEntry as_template_entry = {
      .dstBinding = 0,
      .descriptorCount = 2,
      .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
      .offset = 0,
      .stride = sizeof(descriptor_as[0]),
   };
   VkDescriptorUpdateTemplateCreateInfo as_template_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
      .descriptorUpdateEntryCount = 1,
      .pDescriptorUpdateEntries = &as_template_entry,
      .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
      .descriptorSetLayout = as_set_layout,
   };
   VkDescriptorUpdateTemplate as_template;
   CK(vkCreateDescriptorUpdateTemplate(device, &as_template_info, NULL,
                                       &as_template),
      "Create AS descriptor update template");
   vkUpdateDescriptorSetWithTemplate(device, as_sets[1], as_template,
                                     descriptor_as);
   vkDestroyDescriptorUpdateTemplate(device, as_template, NULL);

   VkWriteDescriptorSetAccelerationStructureKHR query_blas_descriptor_info = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
      .accelerationStructureCount = 1,
      .pAccelerationStructures = &query_tlas.handle,
   };
   VkWriteDescriptorSet query_blas_descriptor_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = &query_blas_descriptor_info,
      .dstSet = as_sets[0],
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
   };
   vkUpdateDescriptorSets(device, 1, &query_blas_descriptor_write, 0, NULL);

   struct test_buffer ray_query_results = {0};
   CK(create_buffer(vkCreateBuffer, vkGetBufferMemoryRequirements,
                    vkAllocateMemory, vkBindBufferMemory,
                    vkGetBufferDeviceAddress, &memory_properties, device,
                    600 * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ray_query_results),
      "Create ray-query descriptor probe result buffer");
   VkDescriptorBufferInfo ray_query_result_buffer = {
      .buffer = ray_query_results.buffer,
      .offset = 0,
      .range = 600 * sizeof(uint32_t),
   };
   VkWriteDescriptorSet ray_query_result_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = as_sets[0],
      .dstBinding = 1,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &ray_query_result_buffer,
   };
   vkUpdateDescriptorSets(device, 1, &ray_query_result_write, 0, NULL);

   VkShaderModuleCreateInfo ray_query_shader_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(as_ray_query_probe_spv),
      .pCode = as_ray_query_probe_spv,
   };
   VkShaderModule ray_query_shader;
   VkResult ray_query_shader_result = vkCreateShaderModule(
      device, &ray_query_shader_info, NULL, &ray_query_shader);
   if (ray_query_shader_result != VK_SUCCESS) {
      printf("FAIL ray-query shader module creation result=%d\n",
             ray_query_shader_result);
      return 1;
   }
   printf("PASS ray-query shader module accepted for single-triangle query test\n");

   VkPipelineLayoutCreateInfo ray_query_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &as_set_layout,
   };
   VkPipelineLayout ray_query_layout;
   CK(vkCreatePipelineLayout(device, &ray_query_layout_info, NULL,
                             &ray_query_layout),
      "Create ray-query probe pipeline layout");
   VkComputePipelineCreateInfo ray_query_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = ray_query_shader,
         .pName = "main",
      },
      .layout = ray_query_layout,
   };
   VkPipeline ray_query_pipeline = VK_NULL_HANDLE;
   VkResult ray_query_pipeline_result = vkCreateComputePipelines(
      device, VK_NULL_HANDLE, 1, &ray_query_pipeline_info, NULL,
      &ray_query_pipeline);
   if (ray_query_pipeline_result != VK_SUCCESS) {
      printf("FAIL ray-query descriptor probe pipeline result=%d expected=%d\n",
             ray_query_pipeline_result, VK_SUCCESS);
      return 1;
   }
   printf("PASS test-only single-triangle Ray Query lowering compiled\n");

   void *ray_query_output = NULL;
   CK(vkMapMemory(device, ray_query_results.memory, 0,
                  ray_query_results.allocation_size, 0, &ray_query_output),
      "Map ray-query descriptor probe result buffer");
   for (uint32_t i = 0; i < 600; i++)
      ((uint32_t *)ray_query_output)[i] = UINT32_MAX;
   if (!(ray_query_results.memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = ray_query_results.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkFlushMappedMemoryRanges(device, 1, &range),
         "Flush ray-query descriptor probe sentinel");
   }
   vkUnmapMemory(device, ray_query_results.memory);

   VkBufferMemoryBarrier ray_query_as_to_compute = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = query_tlas.storage.buffer,
      .offset = 0,
      .size = query_tlas.size,
   };
   VkBufferMemoryBarrier ray_query_compute_to_host = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = ray_query_results.buffer,
      .offset = 0,
      .size = 600 * sizeof(uint32_t),
   };
   CK(vkAllocateCommandBuffers(device, &command_alloc_info, &command_buffer),
      "Allocate ray-query descriptor probe command buffer");
   CK(vkBeginCommandBuffer(command_buffer, &begin_info),
      "Begin ray-query descriptor probe command buffer");
   vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1,
                        &ray_query_as_to_compute, 0, NULL);
   vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                     ray_query_pipeline);
   vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           ray_query_layout, 0, 1, &as_sets[0], 0, NULL);
   vkCmdDispatch(command_buffer, 6, 1, 1);
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                        &ray_query_compute_to_host, 0, NULL);
   CK(vkEndCommandBuffer(command_buffer),
      "End ray-query descriptor probe command buffer");
   submit_info.pCommandBuffers = &command_buffer;
   CK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
      "Submit ray-query descriptor probe");
   CK(vkQueueWaitIdle(queue), "Wait for ray-query descriptor probe");
   vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

   CK(vkMapMemory(device, ray_query_results.memory, 0,
                  ray_query_results.allocation_size, 0, &ray_query_output),
      "Map ray-query descriptor probe result");
   if (!(ray_query_results.memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      VkMappedMemoryRange range = {
         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory = ray_query_results.memory,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      CK(vkInvalidateMappedMemoryRanges(device, 1, &range),
         "Invalidate ray-query descriptor probe result");
   }
   uint32_t query_words[600];
   memcpy(query_words, ray_query_output, sizeof(query_words));
   vkUnmapMemory(device, ray_query_results.memory);
   const float expected_object_to_world[4][3] = {
      {0.0f, 0.5f, 0.0f},
      {-2.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
      {3.0f, 0.0f, 0.0f},
   };
   const float expected_world_to_object[4][3] = {
      {0.0f, -0.5f, 0.0f},
      {2.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 1.5f, 0.0f},
   };
   const float zero_matrix[4][3] = {0};
   for (uint32_t ray = 0; ray < 6; ray++) {
      const uint32_t *candidate = &query_words[ray * 4];
      const uint32_t *committed = &query_words[(6 + ray) * 4];
      const uint32_t *candidate_instance = &query_words[(12 + ray) * 4];
      const uint32_t *committed_instance = &query_words[(18 + ray) * 4];
      const uint32_t *candidate_object_origin = &query_words[(24 + ray) * 4];
      const uint32_t *candidate_object_direction = &query_words[(30 + ray) * 4];
      const uint32_t *committed_object_origin = &query_words[(36 + ray) * 4];
      const uint32_t *committed_object_direction = &query_words[(42 + ray) * 4];
      const uint32_t *ray_params = &query_words[(48 + ray) * 4];
      const uint32_t *candidate_object_to_world =
         &query_words[(54 + ray * 4) * 4];
      const uint32_t *candidate_world_to_object =
         &query_words[(78 + ray * 4) * 4];
      const uint32_t *committed_object_to_world =
         &query_words[(102 + ray * 4) * 4];
      const uint32_t *committed_world_to_object =
         &query_words[(126 + ray * 4) * 4];
      const float expected_tmin = ray == 2 ? 1.3f : 0.0f;
      if (ray_params[0] != 0 ||
          !close_enough(float_from_bits(ray_params[1]), expected_tmin)) {
         printf("FAIL shader Ray Query ray params ray=%u flags=0x%x tMin=%g\n",
                ray, ray_params[0], float_from_bits(ray_params[1]));
         return 1;
      }
      if (ray == 0) {
         if (candidate[0] != 0 || candidate[1] != 0 ||
             !close_enough(float_from_bits(candidate[2]), 1.2f) ||
             !close_enough(float_from_bits(candidate[3]), 0.2f) ||
             committed[0] != 1 || committed[1] != 0 ||
             !close_enough(float_from_bits(committed[2]), 1.2f) ||
             !close_enough(float_from_bits(committed[3]), 0.2f)) {
            printf("FAIL shader Ray Query hit candidate={%u,%u,%g,%g} "
                   "committed={%u,%u,%g,%g}\n",
                   candidate[0], candidate[1],
                   float_from_bits(candidate[2]), float_from_bits(candidate[3]),
                   committed[0], committed[1],
                   float_from_bits(committed[2]),
                   float_from_bits(committed[3]));
            return 1;
         }
         if (candidate_instance[0] != 0 ||
             candidate_instance[1] != 0x13579 ||
             candidate_instance[2] != 0x23456 ||
             committed_instance[0] != 0 ||
             committed_instance[1] != 0x13579 ||
             committed_instance[2] != 0x23456) {
            printf("FAIL shader Ray Query instance candidate={%u,0x%x,0x%x} "
                   "committed={%u,0x%x,0x%x}\n",
                   candidate_instance[0], candidate_instance[1],
                   candidate_instance[2], committed_instance[0],
                   committed_instance[1], committed_instance[2]);
            return 1;
         }
         if (!close_enough(float_from_bits(candidate_object_origin[0]), 0.2f) ||
             !close_enough(float_from_bits(candidate_object_origin[1]), 0.2f) ||
             !close_enough(float_from_bits(candidate_object_origin[2]), -1.0f) ||
             !close_enough(float_from_bits(candidate_object_direction[0]), 0.0f) ||
             !close_enough(float_from_bits(candidate_object_direction[1]), 0.0f) ||
             !close_enough(float_from_bits(candidate_object_direction[2]), 1.0f) ||
             !close_enough(float_from_bits(committed_object_origin[0]), 0.2f) ||
             !close_enough(float_from_bits(committed_object_origin[1]), 0.2f) ||
             !close_enough(float_from_bits(committed_object_origin[2]), -1.0f) ||
             !close_enough(float_from_bits(committed_object_direction[0]), 0.0f) ||
             !close_enough(float_from_bits(committed_object_direction[1]), 0.0f) ||
             !close_enough(float_from_bits(committed_object_direction[2]), 1.0f)) {
            printf("FAIL shader Ray Query object ray origin={%g,%g,%g} "
                   "direction={%g,%g,%g}\n",
                   float_from_bits(candidate_object_origin[0]),
                   float_from_bits(candidate_object_origin[1]),
                   float_from_bits(candidate_object_origin[2]),
                   float_from_bits(candidate_object_direction[0]),
                   float_from_bits(candidate_object_direction[1]),
                   float_from_bits(candidate_object_direction[2]));
            return 1;
         }
         if (!ray_query_matrix_matches(candidate_object_to_world,
                                       expected_object_to_world) ||
             !ray_query_matrix_matches(candidate_world_to_object,
                                       expected_world_to_object) ||
             !ray_query_matrix_matches(committed_object_to_world,
                                       expected_object_to_world) ||
             !ray_query_matrix_matches(committed_world_to_object,
                                       expected_world_to_object)) {
            printf("FAIL shader Ray Query hit object/world transform matrices\n");
            return 1;
         }
      } else if (ray == 5) {
         if (candidate[0] != 0 || candidate[1] != 0 ||
             !close_enough(float_from_bits(candidate[2]), 1.2f) ||
             !close_enough(float_from_bits(candidate[3]), 0.2f) ||
             committed[0] != 0 || committed[1] != UINT32_MAX ||
             committed[2] != 0 || committed[3] != 0) {
            printf("FAIL shader Ray Query discarded candidate={%u,%u,%g,%g} "
                   "committed={%u,%u,0x%x,0x%x}\n",
                   candidate[0], candidate[1],
                   float_from_bits(candidate[2]), float_from_bits(candidate[3]),
                   committed[0], committed[1], committed[2], committed[3]);
            return 1;
         }
         if (candidate_instance[0] != 0 ||
             candidate_instance[1] != 0x13579 ||
             candidate_instance[2] != 0x23456) {
            printf("FAIL shader Ray Query discarded candidate instance="
                   "{%u,0x%x,0x%x}\n",
                   candidate_instance[0], candidate_instance[1],
                   candidate_instance[2]);
            return 1;
         }
         if (!close_enough(float_from_bits(candidate_object_origin[0]), 0.2f) ||
             !close_enough(float_from_bits(candidate_object_origin[1]), 0.2f) ||
             !close_enough(float_from_bits(candidate_object_origin[2]), -1.0f) ||
             !close_enough(float_from_bits(candidate_object_direction[0]), 0.0f) ||
             !close_enough(float_from_bits(candidate_object_direction[1]), 0.0f) ||
             !close_enough(float_from_bits(candidate_object_direction[2]), 1.0f)) {
            printf("FAIL shader Ray Query discarded object ray "
                   "origin={%g,%g,%g} direction={%g,%g,%g}\n",
                   float_from_bits(candidate_object_origin[0]),
                   float_from_bits(candidate_object_origin[1]),
                   float_from_bits(candidate_object_origin[2]),
                   float_from_bits(candidate_object_direction[0]),
                   float_from_bits(candidate_object_direction[1]),
                   float_from_bits(candidate_object_direction[2]));
            return 1;
         }
         if (!ray_query_matrix_matches(candidate_object_to_world,
                                       expected_object_to_world) ||
             !ray_query_matrix_matches(candidate_world_to_object,
                                       expected_world_to_object) ||
             !ray_query_matrix_matches(committed_object_to_world,
                                       zero_matrix) ||
             !ray_query_matrix_matches(committed_world_to_object,
                                       zero_matrix)) {
            printf("FAIL shader Ray Query discarded candidate transform "
                   "matrices or unexpected committed transform\n");
            return 1;
         }
      } else if (candidate[0] != UINT32_MAX ||
                 candidate[1] != UINT32_MAX ||
                 candidate[2] != UINT32_MAX ||
                 candidate[3] != UINT32_MAX ||
                 committed[0] != 0 || committed[1] != UINT32_MAX ||
                 committed[2] != 0 || committed[3] != 0) {
         printf("FAIL shader Ray Query miss ray=%u candidate={%u,%u,0x%x,0x%x} "
                "committed={%u,%u,0x%x,0x%x}\n",
                ray, candidate[0], candidate[1], candidate[2], candidate[3],
                committed[0], committed[1], committed[2], committed[3]);
         return 1;
      }
   }
   printf("PASS GPU Ray Query intrinsic lowering: TLAS-to-BLAS transformed "
          "hit/confirm, candidate discard, instance IDs, object-space rays, "
          "geometric/mask/tMin/tMax misses\n");
   vkDestroyPipeline(device, ray_query_pipeline, NULL);
   vkDestroyPipelineLayout(device, ray_query_layout, NULL);
   vkDestroyShaderModule(device, ray_query_shader, NULL);

   vkDestroyDescriptorPool(device, as_pool, NULL);
   vkDestroyDescriptorSetLayout(device, as_set_layout, NULL);
   printf("PASS AS descriptor write/copy/update-template\n");

   vkDestroyCommandPool(device, command_pool, NULL);
   vkDestroyAccelerationStructureKHR(device, tlas.handle, NULL);
   vkDestroyAccelerationStructureKHR(device, query_tlas.handle, NULL);
   vkDestroyAccelerationStructureKHR(device, multi_blas.handle, NULL);
   vkDestroyAccelerationStructureKHR(device, single_blas.handle, NULL);
   vkDestroyAccelerationStructureKHR(device, empty_blas.handle, NULL);
   vkDestroyAccelerationStructureKHR(device, blas.handle, NULL);
   vkDestroyBuffer(device, tlas_scratch.buffer, NULL);
   vkFreeMemory(device, tlas_scratch.memory, NULL);
   vkDestroyBuffer(device, query_tlas_scratch.buffer, NULL);
   vkFreeMemory(device, query_tlas_scratch.memory, NULL);
   vkDestroyBuffer(device, query_instance_buffer.buffer, NULL);
   vkFreeMemory(device, query_instance_buffer.memory, NULL);
   vkDestroyBuffer(device, query_tlas.storage.buffer, NULL);
   vkFreeMemory(device, query_tlas.storage.memory, NULL);
   vkDestroyBuffer(device, ray_query_results.buffer, NULL);
   vkFreeMemory(device, ray_query_results.memory, NULL);
   vkDestroyBuffer(device, blas_scratch.buffer, NULL);
   vkFreeMemory(device, blas_scratch.memory, NULL);
   vkDestroyBuffer(device, instance_buffer.buffer, NULL);
   vkFreeMemory(device, instance_buffer.memory, NULL);
   vkDestroyBuffer(device, tlas_readback.buffer, NULL);
   vkFreeMemory(device, tlas_readback.memory, NULL);
   vkDestroyBuffer(device, single_blas_readback.buffer, NULL);
   vkFreeMemory(device, single_blas_readback.memory, NULL);
   vkDestroyBuffer(device, single_blas.storage.buffer, NULL);
   vkFreeMemory(device, single_blas.storage.memory, NULL);
   vkDestroyBuffer(device, multi_blas_readback.buffer, NULL);
   vkFreeMemory(device, multi_blas_readback.memory, NULL);
   vkDestroyBuffer(device, multi_blas_scratch.buffer, NULL);
   vkFreeMemory(device, multi_blas_scratch.memory, NULL);
   vkDestroyBuffer(device, multi_blas.storage.buffer, NULL);
   vkFreeMemory(device, multi_blas.storage.memory, NULL);
   vkDestroyBuffer(device, empty_blas_readback.buffer, NULL);
   vkFreeMemory(device, empty_blas_readback.memory, NULL);
   vkDestroyBuffer(device, empty_blas.storage.buffer, NULL);
   vkFreeMemory(device, empty_blas.storage.memory, NULL);
   vkDestroyBuffer(device, tlas.storage.buffer, NULL);
   vkFreeMemory(device, tlas.storage.memory, NULL);
   vkDestroyBuffer(device, blas.storage.buffer, NULL);
   vkFreeMemory(device, blas.storage.memory, NULL);
   vkDestroyBuffer(device, blas_readback.buffer, NULL);
   vkFreeMemory(device, blas_readback.memory, NULL);
   vkDestroyBuffer(device, vertex_buffer.buffer, NULL);
   vkFreeMemory(device, vertex_buffer.memory, NULL);
   vkDestroyDevice(device, NULL);
   PFN_vkDestroyInstance vkDestroyInstance =
      (PFN_vkDestroyInstance)gipa(instance, "vkDestroyInstance");
   if (vkDestroyInstance)
      vkDestroyInstance(instance, NULL);
   printf("PASS acceleration structure build API test\n");
   return 0;
}
