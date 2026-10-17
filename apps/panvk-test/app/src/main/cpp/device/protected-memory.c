#define VK_NO_PROTOTYPES
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define LOAD(instance, name)                                                   \
   PFN_vk##name vk##name =                                                     \
      (PFN_vk##name)gipa(instance, "vk" #name);                                \
   if (!vk##name) {                                                            \
      printf("FAIL missing vk" #name "\n");                                   \
      return 1;                                                                \
   }

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("usage: %s <icd>\n", argv[0]);
      return 2;
   }

   void *icd = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!icd) {
      printf("FAIL dlopen %s\n", dlerror());
      return 1;
   }

   PFN_vkGetInstanceProcAddr gipa =
      (PFN_vkGetInstanceProcAddr)dlsym(icd, "vk_icdGetInstanceProcAddr");
   if (!gipa) {
      printf("FAIL missing vk_icdGetInstanceProcAddr\n");
      return 1;
   }

   PFN_vkCreateInstance create_instance =
      (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   if (!create_instance) {
      printf("FAIL missing vkCreateInstance\n");
      return 1;
   }

   VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = VK_API_VERSION_1_1,
   };
   VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
   };
   VkInstance instance;
   VkResult result = create_instance(&instance_info, NULL, &instance);
   if (result != VK_SUCCESS) {
      printf("FAIL CreateInstance r=%d\n", result);
      return 1;
   }

   LOAD(instance, EnumeratePhysicalDevices)
   LOAD(instance, GetPhysicalDeviceFeatures2)
   LOAD(instance, GetPhysicalDeviceQueueFamilyProperties)
   LOAD(instance, GetPhysicalDeviceMemoryProperties)
   LOAD(instance, CreateDevice)

   uint32_t physical_count = 0;
   result = vkEnumeratePhysicalDevices(instance, &physical_count, NULL);
   if (result != VK_SUCCESS || physical_count == 0) {
      printf("FAIL EnumeratePhysicalDevices r=%d count=%u\n", result,
             physical_count);
      return 1;
   }

   VkPhysicalDevice physical_devices[8];
   physical_count = physical_count < 8 ? physical_count : 8;
   result = vkEnumeratePhysicalDevices(instance, &physical_count,
                                       physical_devices);
   if (result != VK_SUCCESS) {
      printf("FAIL EnumeratePhysicalDevices data r=%d\n", result);
      return 1;
   }

   VkPhysicalDevice physical = physical_devices[0];
   VkPhysicalDeviceProtectedMemoryFeatures protected_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,
   };
   VkPhysicalDevicePipelineProtectedAccessFeatures pipeline_access_features = {
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROTECTED_ACCESS_FEATURES,
   };
   protected_features.pNext = &pipeline_access_features;
   VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &protected_features,
   };
   vkGetPhysicalDeviceFeatures2(physical, &features);

   uint32_t family_count = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
   VkQueueFamilyProperties families[8];
   family_count = family_count < 8 ? family_count : 8;
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
   uint32_t family = 0;
   while (family < family_count && families[family].queueCount == 0)
      family++;
   if (family == family_count) {
      printf("SKIP no queue family available\n");
      return 0;
   }

   LOAD(instance, DestroyDevice)
   LOAD(instance, DestroyInstance)

   if (pipeline_access_features.pipelineProtectedAccess) {
      float pipeline_priority = 1.0f;
      VkDeviceQueueCreateInfo pipeline_queue_info = {
         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = family,
         .queueCount = 1,
         .pQueuePriorities = &pipeline_priority,
      };
      VkDeviceCreateInfo pipeline_device_info = {
         .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
         .queueCreateInfoCount = 1,
         .pQueueCreateInfos = &pipeline_queue_info,
         .enabledExtensionCount = 1,
         .ppEnabledExtensionNames =
            (const char *[]){VK_EXT_PIPELINE_PROTECTED_ACCESS_EXTENSION_NAME},
         .pNext = &pipeline_access_features,
      };
      VkDevice pipeline_device;
      result = vkCreateDevice(physical, &pipeline_device_info, NULL,
                              &pipeline_device);
      if (result != VK_SUCCESS) {
         printf("FAIL CreateDevice pipelineProtectedAccess r=%d\n", result);
         return 1;
      }
      vkDestroyDevice(pipeline_device, NULL);
      printf("PIPELINE_PROTECTED_ACCESS PASS feature-enable\n");
   } else {
      printf("SKIP pipelineProtectedAccess feature is not supported\n");
   }

   if (!protected_features.protectedMemory) {
      printf("SKIP protectedMemory feature is not supported\n");
      vkDestroyInstance(instance, NULL);
      return 0;
   }

   VkPhysicalDeviceMemoryProperties memory_properties;
   vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
   uint32_t memory_type = memory_properties.memoryTypeCount;
   for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
      if (memory_properties.memoryTypes[i].propertyFlags &
          VK_MEMORY_PROPERTY_PROTECTED_BIT) {
         memory_type = i;
         break;
      }
   }
   if (memory_type == memory_properties.memoryTypeCount) {
      printf("SKIP no protected memory type\n");
      return 0;
   }

   float priority = 1.0f;
   VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
   };
   VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .pNext = &protected_features,
   };
   VkDevice device;
   result = vkCreateDevice(physical, &device_info, NULL, &device);
   if (result != VK_SUCCESS) {
      printf("FAIL CreateDevice protected queue r=%d\n", result);
      return 1;
   }

   LOAD(instance, CreateBuffer)
   LOAD(instance, GetBufferMemoryRequirements)
   LOAD(instance, AllocateMemory)
   LOAD(instance, BindBufferMemory)
   LOAD(instance, DestroyBuffer)
   LOAD(instance, FreeMemory)

   VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .flags = VK_BUFFER_CREATE_PROTECTED_BIT,
      .size = 4096,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   VkBuffer buffer;
   result = vkCreateBuffer(device, &buffer_info, NULL, &buffer);
   if (result != VK_SUCCESS) {
      printf("FAIL CreateBuffer protected r=%d\n", result);
      return 1;
   }

   VkMemoryRequirements requirements;
   vkGetBufferMemoryRequirements(device, buffer, &requirements);
   if (!(requirements.memoryTypeBits & (1u << memory_type))) {
      printf("FAIL protected memory type is incompatible with buffer\n");
      return 1;
   }

   VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = requirements.size,
      .memoryTypeIndex = memory_type,
   };
   VkDeviceMemory memory;
   result = vkAllocateMemory(device, &memory_info, NULL, &memory);
   if (result != VK_SUCCESS) {
      printf("FAIL AllocateMemory protected r=%d\n", result);
      return 1;
   }
   result = vkBindBufferMemory(device, buffer, memory, 0);
   if (result != VK_SUCCESS) {
      printf("FAIL BindBufferMemory protected r=%d\n", result);
      return 1;
   }

   vkDestroyBuffer(device, buffer, NULL);
   vkFreeMemory(device, memory, NULL);
   vkDestroyDevice(device, NULL);
   vkDestroyInstance(instance, NULL);
   printf("PROTECTED_ALLOC PASS protected-buffer allocate+bind\n");
   return 0;
}
