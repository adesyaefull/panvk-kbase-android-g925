/* Gates D+E: device create, queue, mapped buffer, deterministic compute.
 * Default dispatch writes 0x12345678; ray_triangle tests triangle hits;
 * bvh_node tests one leaf; bvh_two_triangles tests two leaves;
 * bvh_four_triangles tests a two-level, four-leaf tree; bvh_variable tests
 * input counts 1-4 against that hierarchy. Ray modes repeat 10 times.
 * Usage: compute <libvulkan_panfrost.so>
 * [nop|ray_triangle|bvh_node|bvh_two_triangles|bvh_four_triangles|bvh_variable]
 */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

#define CK(expr, what)                                                      \
   do {                                                                     \
      VkResult _r = (expr);                                                  \
      if (_r != VK_SUCCESS) {                                                \
         printf("FAIL %s r=%d line=%d\n", what, _r, __LINE__);                \
         return 1;                                                          \
      }                                                                     \
   } while (0)

/* Generated from write_magic.comp via glslangValidator (see below). */
static const uint32_t comp_spv[] = {
#include "write_magic.spv.h"
};

static const uint32_t nop_spv[] = {
#include "nop.spv.h"
};

/* Generated from ray_triangle.comp with glslangValidator -V. */
static const uint32_t ray_triangle_spv[] = {
#include "ray_triangle.spv.h"
};

/* Generated from bvh_node.comp with glslangValidator -V. */
static const uint32_t bvh_node_spv[] = {
#include "bvh_node.spv.h"
};

/* Generated from bvh_two_triangles.comp with glslangValidator -V. */
static const uint32_t bvh_two_triangles_spv[] = {
#include "bvh_two_triangles.spv.h"
};

/* Generated from bvh_four_triangles.comp with glslc -O. */
static const uint32_t bvh_four_triangles_spv[] = {
#include "bvh_four_triangles.spv.h"
};

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("usage: %s <libvulkan_panfrost.so> [nop]\n", argv[0]);
      return 2;
   }
   int ray_mode = argc > 2 && !strcmp(argv[2], "ray_triangle");
   int bvh_mode = argc > 2 && !strcmp(argv[2], "bvh_node");
   int bvh_two_mode = argc > 2 && !strcmp(argv[2], "bvh_two_triangles");
   int bvh_four_mode = argc > 2 && !strcmp(argv[2], "bvh_four_triangles");
   int bvh_variable_mode = argc > 2 && !strcmp(argv[2], "bvh_variable");
   int use_buf = ray_mode || bvh_mode || bvh_two_mode || bvh_four_mode ||
                 bvh_variable_mode ||
                 !(argc > 2 && !strcmp(argv[2], "nop"));
   setvbuf(stdout, NULL, _IONBF, 0);
   const uint32_t *spv = (bvh_four_mode || bvh_variable_mode) ?
                         bvh_four_triangles_spv :
                         bvh_two_mode ? bvh_two_triangles_spv :
                         bvh_mode ? bvh_node_spv :
                         ray_mode ? ray_triangle_spv :
                         use_buf ? comp_spv : nop_spv;
   size_t spv_size = (bvh_four_mode || bvh_variable_mode) ?
                     sizeof(bvh_four_triangles_spv) :
                     bvh_two_mode ? sizeof(bvh_two_triangles_spv) :
                     bvh_mode ? sizeof(bvh_node_spv) :
                     ray_mode ? sizeof(ray_triangle_spv) :
                     use_buf ? sizeof(comp_spv) : sizeof(nop_spv);
   void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      printf("FAIL dlopen %s\n", dlerror());
      return 1;
   }
   icd_gipa_fn gipa = (icd_gipa_fn)dlsym(h, "vk_icdGetInstanceProcAddr");
   if (!gipa) {
      printf("FAIL gipa\n");
      return 1;
   }
#define GIPA(what, name)                                                    \
   PFN_##name name = (PFN_##name)gipa(NULL, "vk" #what);                     \
   if (!name) {                                                             \
      printf("FAIL missing vk" #what "\n");                                  \
      return 1;                                                             \
   }
   GIPA(CreateInstance, vkCreateInstance)

   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .apiVersion = VK_API_VERSION_1_3};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
   VkInstance inst;
   CK(vkCreateInstance(&ici, NULL, &inst), "CreateInstance");
   fprintf(stderr, "T-after-CreateInstance\n");

#undef GIPA
#define GIPA(what, name)                                                    \
   PFN_##name name = (PFN_##name)gipa(inst, "vk" #what);                     \
   if (!name) {                                                             \
      printf("FAIL missing vk" #what "\n");                                  \
      return 1;                                                             \
   }
   GIPA(EnumeratePhysicalDevices, vkEnumeratePhysicalDevices)
   GIPA(GetPhysicalDeviceProperties, vkGetPhysicalDeviceProperties)
   GIPA(GetPhysicalDeviceQueueFamilyProperties,
        vkGetPhysicalDeviceQueueFamilyProperties)
   GIPA(GetPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceMemoryProperties)
   GIPA(CreateDevice, vkCreateDevice)
   GIPA(GetDeviceQueue, vkGetDeviceQueue)
   GIPA(CreateBuffer, vkCreateBuffer)
   GIPA(GetBufferMemoryRequirements, vkGetBufferMemoryRequirements)
   GIPA(AllocateMemory, vkAllocateMemory)
   GIPA(BindBufferMemory, vkBindBufferMemory)
   GIPA(MapMemory, vkMapMemory)
   GIPA(UnmapMemory, vkUnmapMemory)
   GIPA(CreateDescriptorSetLayout, vkCreateDescriptorSetLayout)
   GIPA(CreateDescriptorPool, vkCreateDescriptorPool)
   GIPA(AllocateDescriptorSets, vkAllocateDescriptorSets)
   GIPA(UpdateDescriptorSets, vkUpdateDescriptorSets)
   GIPA(CreateShaderModule, vkCreateShaderModule)
   GIPA(CreatePipelineLayout, vkCreatePipelineLayout)
   GIPA(CreateComputePipelines, vkCreateComputePipelines)
   GIPA(CreateCommandPool, vkCreateCommandPool)
   GIPA(AllocateCommandBuffers, vkAllocateCommandBuffers)
   GIPA(BeginCommandBuffer, vkBeginCommandBuffer)
   GIPA(ResetCommandBuffer, vkResetCommandBuffer)
   GIPA(CmdBindPipeline, vkCmdBindPipeline)
   GIPA(CmdBindDescriptorSets, vkCmdBindDescriptorSets)
   GIPA(CmdPushConstants, vkCmdPushConstants)
   GIPA(CmdDispatch, vkCmdDispatch)
   GIPA(CmdPipelineBarrier, vkCmdPipelineBarrier)
   GIPA(EndCommandBuffer, vkEndCommandBuffer)
   GIPA(CreateFence, vkCreateFence)
   GIPA(QueueSubmit, vkQueueSubmit)
   GIPA(WaitForFences, vkWaitForFences)
   GIPA(ResetFences, vkResetFences)
   GIPA(DestroyFence, vkDestroyFence)
   GIPA(FreeCommandBuffers, vkFreeCommandBuffers)
   GIPA(DestroyCommandPool, vkDestroyCommandPool)
   GIPA(DestroyPipeline, vkDestroyPipeline)
   GIPA(DestroyPipelineLayout, vkDestroyPipelineLayout)
   GIPA(DestroyShaderModule, vkDestroyShaderModule)
   GIPA(DestroyDescriptorPool, vkDestroyDescriptorPool)
   GIPA(DestroyDescriptorSetLayout, vkDestroyDescriptorSetLayout)
   GIPA(FreeMemory, vkFreeMemory)
   GIPA(DestroyBuffer, vkDestroyBuffer)
   GIPA(DestroyDevice, vkDestroyDevice)
   GIPA(DestroyInstance, vkDestroyInstance)

   /* pick Mali */
   uint32_t n = 0;
   CK(vkEnumeratePhysicalDevices(inst, &n, NULL), "count");
   fprintf(stderr, "T-after-count n=%u\n", n);
   VkPhysicalDevice *devs = malloc(n * sizeof(*devs));
   CK(vkEnumeratePhysicalDevices(inst, &n, devs), "enum");
   fprintf(stderr, "T-after-enum\n");
   VkPhysicalDevice phys = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < n; i++) {
      fprintf(stderr, "T-loop %u\n", i);
      VkPhysicalDeviceProperties p;
      vkGetPhysicalDeviceProperties(devs[i], &p);
      fprintf(stderr, "T-gpp %u: %s\n", i, p.deviceName);
      if (strstr(p.deviceName, "Mali"))
         phys = devs[i];
   }
   free(devs);
   if (phys == VK_NULL_HANDLE) {
      printf("FAIL no Mali\n");
      return 1;
   }
   printf("D-OK physical device\n");

   /* queue family with COMPUTE */
   uint32_t qn = 0;
   fprintf(stderr, "T-before-qfams\n");
   vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   fprintf(stderr, "T-after-qfams qn=%u\n", qn);
   VkQueueFamilyProperties *qp = malloc(qn * sizeof(*qp));
   fprintf(stderr, "T-before-qfams2\n");
   vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp);
   fprintf(stderr, "T-after-qfams2\n");
   uint32_t qi = ~0u;
   for (uint32_t i = 0; i < qn; i++)
      if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
         qi = i;
         break;
      }
   free(qp);
   if (qi == ~0u) {
      printf("FAIL no compute queue\n");
      return 1;
   }

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  .queueFamilyIndex = qi,
                                  .queueCount = 1,
                                  .pQueuePriorities = &prio};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci};
   VkDevice dev;
   fprintf(stderr, "T-before-CreateDevice qi=%u\n", qi);
   CK(vkCreateDevice(phys, &dci, NULL, &dev), "CreateDevice");
   fprintf(stderr, "T-after-CreateDevice\n");
   printf("D-OK vkCreateDevice\n");
   VkQueue queue;
   vkGetDeviceQueue(dev, qi, 0, &queue);

   /* storage buffer; argv[2]=="big" uses 16KB like the AHB test */
   VkDeviceSize bufsize = bvh_variable_mode ? 704 :
                          bvh_four_mode ? 640 :
                          bvh_two_mode ? 256 :
                          bvh_mode ? 64 :
                          ray_mode ? 4 * sizeof(uint32_t) :
                          (argc > 2 && !strcmp(argv[2], "big")) ? 16384 : 4;
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             .size = bufsize,
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
   VkBuffer buf;
   CK(vkCreateBuffer(dev, &bci, NULL, &buf), "CreateBuffer");
   VkMemoryRequirements mr;
   vkGetBufferMemoryRequirements(dev, buf, &mr);
   VkPhysicalDeviceMemoryProperties mp;
   fprintf(stderr, "T-before-memprops\n");
   vkGetPhysicalDeviceMemoryProperties(phys, &mp);
   fprintf(stderr, "T-after-memprops types=%u heaps=%u\n", mp.memoryTypeCount,
           mp.memoryHeapCount);
   uint32_t mi = ~0u;
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
      if ((mr.memoryTypeBits & (1u << i)) &&
          (mp.memoryTypes[i].propertyFlags &
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
         mi = i;
         break;
      }
   if (mi == ~0u) {
      printf("FAIL no host-coherent memory\n");
      return 1;
   }
   VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = mr.size,
                               .memoryTypeIndex = mi};
   VkDeviceMemory mem;
   fprintf(stderr, "T-before-AllocMem size=%llu mi=%u\n",
           (unsigned long long)mr.size, mi);
   CK(vkAllocateMemory(dev, &mai, NULL, &mem), "AllocateMemory");
   fprintf(stderr, "T-after-AllocMem\n");
   CK(vkBindBufferMemory(dev, buf, mem, 0), "BindBufferMemory");
   fprintf(stderr, "T-after-Bind\n");
   printf("D-OK memory+bind (Gate D device side done)\n");

   /* descriptor + pipeline (built once) */
   VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
   VkDescriptorPool pool = VK_NULL_HANDLE;
   VkDescriptorSet set = VK_NULL_HANDLE;
   if (use_buf) {
   VkDescriptorSetLayoutBinding bind = {.binding = 0,
                                        .descriptorType =
                                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        .descriptorCount = 1,
                                        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo dlci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &bind};
   CK(vkCreateDescriptorSetLayout(dev, &dlci, NULL, &dsl), "DSL");
   VkDescriptorPoolSize psz = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                               .descriptorCount = 1};
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &psz};
   CK(vkCreateDescriptorPool(dev, &dpci, NULL, &pool), "Pool");
   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &dsl};
   CK(vkAllocateDescriptorSets(dev, &dsai, &set), "AllocSets");
   VkDescriptorBufferInfo dbi = {.buffer = buf, .offset = 0, .range = bufsize};
   VkWriteDescriptorSet wds = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                               .dstSet = set,
                               .dstBinding = 0,
                               .descriptorCount = 1,
                               .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                               .pBufferInfo = &dbi};
   vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);
   } /* use_buf */

   VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                    .codeSize = spv_size,
                                    .pCode = spv};
   VkShaderModule mod;
   fprintf(stderr, "T-before-ShaderModule size=%zu\n", spv_size);
   CK(vkCreateShaderModule(dev, &smci, NULL, &mod), "ShaderModule");
   fprintf(stderr, "T-after-ShaderModule\n");
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = use_buf ? 1 : 0,
      .pSetLayouts = use_buf ? &dsl : NULL};
   VkPushConstantRange push_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(uint32_t),
   };
   if (bvh_mode || bvh_two_mode || bvh_four_mode || bvh_variable_mode) {
      plci.pushConstantRangeCount = 1;
      if (bvh_four_mode || bvh_variable_mode)
         push_range.size = 3 * sizeof(uint32_t);
      plci.pPushConstantRanges = &push_range;
   }
   VkPipelineLayout layout;
   CK(vkCreatePipelineLayout(dev, &plci, NULL, &layout), "Layout");
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = mod,
                .pName = "main"},
      .layout = layout};
   VkPipeline pipe;
   fprintf(stderr, "T-before-ComputePipeline\n");
   CK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe),
      "ComputePipeline");
   fprintf(stderr, "T-after-ComputePipeline\n");

   VkCommandPoolCreateInfo cpoci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = qi};
   VkCommandPool cpool;
   CK(vkCreateCommandPool(dev, &cpoci, NULL, &cpool), "CmdPool");
   VkCommandBufferAllocateInfo cbai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cpool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
   VkCommandBuffer cmd;
   CK(vkAllocateCommandBuffers(dev, &cbai, &cmd), "AllocCmd");
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CK(vkCreateFence(dev, &fci, NULL, &fence), "Fence");

   int pass = 0;
   for (int iter = 0; iter < 10; iter++) {
      void *p;
      CK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &p), "Map");
      fprintf(stderr, "T-mapped p=%p\n", p);
      memset(p, 0, bufsize);
      if (bvh_two_mode) {
         const float vertices[24] = {
            0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 0.0f,
         };
         memcpy((char *)p + 3 * 48, vertices, sizeof(vertices));
      }
      if (bvh_four_mode || bvh_variable_mode) {
         const float vertices[48] = {
            0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            2.0f, 0.0f, 0.0f, 0.0f,
            3.0f, 0.0f, 0.0f, 0.0f,
            2.0f, 1.0f, 0.0f, 0.0f,
            4.0f, 0.0f, 0.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 0.0f,
            4.0f, 1.0f, 0.0f, 0.0f,
            6.0f, 0.0f, 0.0f, 0.0f,
            7.0f, 0.0f, 0.0f, 0.0f,
            6.0f, 1.0f, 0.0f, 0.0f,
         };
         memcpy((char *)p + 7 * 48, vertices, sizeof(vertices));
      }
      vkUnmapMemory(dev, mem);

      VkCommandBufferBeginInfo bbi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
      CK(vkResetCommandBuffer(cmd, 0), "ResetCmd");
      CK(vkBeginCommandBuffer(cmd, &bbi), "Begin");
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      if (use_buf)
         vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                                 0, 1, &set, 0, NULL);
      if (bvh_mode || bvh_two_mode || bvh_four_mode ||
          bvh_variable_mode) {
         uint32_t mode = 0;
         struct {
            uint32_t mode;
            uint32_t primitive_count;
            uint32_t result_offset;
         } push = {
            .mode = 0,
            .primitive_count = bvh_four_mode ? 4 : 2,
            .result_offset = 0,
         };
         if (bvh_four_mode || bvh_variable_mode) {
            push.primitive_count = bvh_four_mode ? 4 : 0;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(push), &push);
         } else {
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(mode), &mode);
         }

         VkMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
         };

         if (bvh_variable_mode) {
            for (uint32_t count = 1; count <= 4; count++) {
               push.mode = 0;
               push.primitive_count = count;
               push.result_offset = (count - 1) * 8;
               vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                  0, sizeof(push), &push);
               vkCmdDispatch(cmd, 1, 1, 1);
               vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                    1, &barrier, 0, NULL, 0, NULL);
               push.mode = 1;
               vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                  0, sizeof(push), &push);
               vkCmdDispatch(cmd, 8, 1, 1);
               if (count < 4)
                  vkCmdPipelineBarrier(cmd,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                     1, &barrier, 0, NULL, 0, NULL);
            }
         } else if (bvh_four_mode) {
            vkCmdDispatch(cmd, 1, 1, 1);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 1, &barrier, 0, NULL, 0, NULL);
            push.mode = 1;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(push), &push);
            vkCmdDispatch(cmd, 8, 1, 1);
         } else {
            vkCmdDispatch(cmd, 1, 1, 1);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 1, &barrier, 0, NULL, 0, NULL);
            mode = 1;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(mode), &mode);
            vkCmdDispatch(cmd, 4, 1, 1);
         }
      } else {
         vkCmdDispatch(cmd, ray_mode ? 4 : 1, 1, 1);
      }
      CK(vkEndCommandBuffer(cmd), "End");
      VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .commandBufferCount = 1,
                         .pCommandBuffers = &cmd};
      CK(vkQueueSubmit(queue, 1, &si, fence), "Submit");
      CK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 30 * 1000 * 1000 * 1000ULL),
         "WaitFences");
      CK(vkResetFences(dev, 1, &fence), "ResetFences");

      CK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &p), "Map2");
      uint32_t v = *(volatile uint32_t *)p;
      uint32_t ray_results[32] = {0};
      if (ray_mode || bvh_mode || bvh_two_mode || bvh_four_mode ||
          bvh_variable_mode)
         memcpy(ray_results, (const uint32_t *)p +
                                 (bvh_variable_mode ? 132 :
                                  bvh_four_mode ? 132 :
                                  bvh_two_mode ? 60 :
                                  bvh_mode ? 12 : 0),
                (bvh_variable_mode ? sizeof(ray_results) :
                 bvh_four_mode ? 8 * sizeof(uint32_t) :
                 4 * sizeof(uint32_t)));
      vkUnmapMemory(dev, mem);
      if (ray_mode || bvh_mode || bvh_two_mode || bvh_four_mode ||
          bvh_variable_mode) {
         const uint32_t ray_expected[4] = {1, 0, 1, 0};
         const uint32_t bvh_two_expected[4] = {1, 1, 0, 1};
         const uint32_t bvh_four_expected[8] = {1, 1, 0, 1, 0, 1, 0, 0};
         const uint32_t *expected = bvh_four_mode ? bvh_four_expected :
                                    bvh_two_mode ? bvh_two_expected :
                                    ray_expected;
         bool matches = true;
         uint32_t ray_count = bvh_four_mode ? 8 :
                              bvh_variable_mode ? 32 : 4;
         for (uint32_t ray = 0; ray < ray_count; ray++) {
            uint32_t expected_value = bvh_variable_mode ? 0 : expected[ray];
            if (bvh_variable_mode) {
               const uint32_t count_expected[4][8] = {
                  {1, 0, 0, 0, 0, 0, 0, 0},
                  {1, 1, 0, 0, 0, 0, 0, 0},
                  {1, 1, 0, 1, 0, 0, 0, 0},
                  {1, 1, 0, 1, 0, 1, 0, 0},
               };
               expected_value = count_expected[ray / 8][ray % 8];
            }
            if (ray_results[ray] != expected_value) {
               printf("E-iter %d ray %u: got %u (want %u)\n", iter, ray,
                      ray_results[ray], expected_value);
               matches = false;
            }
         }
         if (matches)
            pass++;
      } else if (!use_buf) {
         pass++;
      } else if (v == 0x12345678u) {
         pass++;
      } else {
         printf("E-iter %d: got 0x%08x (want 0x12345678)\n", iter, v);
      }
   }
   printf("E-compute %d/10\n", pass);

   vkDestroyFence(dev, fence, NULL);
   vkFreeCommandBuffers(dev, cpool, 1, &cmd);
   vkDestroyCommandPool(dev, cpool, NULL);
   vkDestroyPipeline(dev, pipe, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyShaderModule(dev, mod, NULL);
   if (use_buf) {
      vkDestroyDescriptorPool(dev, pool, NULL);
      vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   }
   vkFreeMemory(dev, mem, NULL);
   vkDestroyBuffer(dev, buf, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);

   if (pass == 10) {
      printf("D+E-PASS\n");
      return 0;
   }
   printf("D+E-FAIL\n");
   return 1;
}
