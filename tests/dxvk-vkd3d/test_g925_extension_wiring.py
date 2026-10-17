import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MESA = ROOT / "work/mesa/src/panfrost/vulkan"


class G925ExtensionWiringTest(unittest.TestCase):
    def test_android_wsi_advertises_requested_present_modes(self):
        wsi = (MESA / "panvk_wsi.c").read_text()
        self.assertIn("VK_PRESENT_MODE_FIFO_KHR,", wsi)
        self.assertIn("VK_PRESENT_MODE_IMMEDIATE_KHR,", wsi)
        self.assertIn("VK_PRESENT_MODE_MAILBOX_KHR,", wsi)

    def test_kbase_scheduling_controls_use_reported_shader_core_capacity(self):
        kmod = (ROOT / "work/mesa/src/panfrost/lib/kmod/kbase_kmod.c").read_text()
        queue = (MESA / "csf/panvk_vX_gpu_queue.c").read_text()
        self.assertIn(
            "kbase_create_group(\n"
            "         queue, core_ctrl ? core_ctrl->shaderCoreCount : 0)",
            queue,
        )
        self.assertIn("util_bitcount64(dev->props.shader_present)", kmod)
        self.assertIn(
            "shader_core_count ? shader_core_count : available_shader_cores",
            kmod,
        )
        self.assertIn(".fragment_max = max_shader_cores,", kmod)
        self.assertIn(".compute_max = max_shader_cores,", kmod)

    def test_dx12_descriptor_and_atomic_features_are_enabled(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(".EXT_descriptor_indexing = PAN_ARCH >= 9,", physical)
        self.assertIn(".EXT_mutable_descriptor_type = PAN_ARCH >= 9,", physical)
        self.assertIn(".KHR_shader_atomic_int64 = PAN_ARCH >= 9,", physical)
        self.assertIn(".EXT_shader_atomic_float = true,", physical)
        for feature in (
            ".shaderSampledImageArrayDynamicIndexing = true,",
            ".shaderBufferInt64Atomics = PAN_ARCH >= 9,",
            ".shaderSharedInt64Atomics = PAN_ARCH >= 9,",
            ".descriptorBindingPartiallyBound = PAN_ARCH >= 9,",
            ".runtimeDescriptorArray = true,",
            ".mutableDescriptorType = PAN_ARCH >= 9,",
            ".shaderBufferFloat32Atomics = true,",
            ".shaderSharedFloat32Atomics = true,",
        ):
            self.assertIn(feature, physical)

    def test_mutable_combined_descriptors_use_padded_layout(self):
        layout = (MESA / "panvk_vX_descriptor_set_layout.c").read_text()
        mutable_type_list = layout.split("is_supported_mutable_type(", 1)[1].split(
            "\n}", 1
        )[0]
        for descriptor_type in (
            "VK_DESCRIPTOR_TYPE_SAMPLER",
            "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE",
            "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER",
            "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
            "VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT",
            "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER",
            "VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER",
            "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER",
            "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER",
        ):
            self.assertIn(f"case {descriptor_type}:", mutable_type_list)
        layout_header = (MESA / "panvk_descriptor_set_layout.h").read_text()
        self.assertIn("layout->mutable_has_combined", layout_header)
        self.assertIn("layout->textures_per_desc + layout->samplers_per_desc", layout_header)
        self.assertIn("binding_layout->textures_per_desc = PANVK_MAX_PLANES;", layout)
        self.assertIn(
            "binding_layout->samplers_per_desc = PANVK_MAX_DESCS_PER_SAMPLER;",
            layout,
        )
        self.assertIn("has_combined && (PAN_ARCH < 9 || has_buffer)", layout)

    def test_mutable_combined_shader_indices_follow_paired_sampler_slot(self):
        nir = (MESA / "panvk_vX_nir_lower_descriptors.c").read_text()
        self.assertIn("same_descriptor_element(sampler_deref, texture_deref)", nir)
        self.assertIn("SAMPLER_SUBDESC(plane)", nir)
        self.assertIn("nir_imul_imm(b, index_ssa, panvk_get_desc_stride(bind_layout))", nir)

    def test_mutable_descriptor_copies_track_combined_type(self):
        descriptors = (MESA / "panvk_vX_descriptor_set.c").read_text()
        self.assertIn("set_mutable_descriptor_type", descriptors)
        self.assertIn("VK_DESCRIPTOR_TYPE_MAX_ENUM", descriptors)
        self.assertIn("stride * PANVK_DESCRIPTOR_SIZE", descriptors)

    def test_extension_status_counts_match_unique_entries(self):
        status = json.loads(
            (ROOT / "validation/g925-v13-csf/extension-implementation-status.json").read_text()
        )
        extensions = status["requestedExtensions"]
        names = [extension["name"] for extension in extensions]
        self.assertEqual(len(names), len(set(names)))
        implemented = sum(
            extension["status"].startswith("implemented") for extension in extensions
        )
        unimplemented = sum(
            extension["status"] == "not-implemented" for extension in extensions
        )
        enumerated_feature_disabled = sum(
            extension["status"] == "extension-enumerated-feature-disabled"
            for extension in extensions
        )
        self.assertEqual(status["implementedRequestedExtensionCount"], implemented)
        self.assertEqual(status["unimplementedRequestedExtensionCount"], unimplemented)
        self.assertEqual(
            status["enumeratedFeatureDisabledRequestedExtensionCount"],
            enumerated_feature_disabled,
        )
        self.assertEqual(
            len(names),
            implemented + unimplemented + enumerated_feature_disabled,
        )
        self.assertEqual(status["requestedNonRayGpuInfoExtensionCount"], 11)
        self.assertEqual(status["implementedNonRayGpuInfoExtensionCount"], 5)
        self.assertEqual(status["unimplementedNonRayGpuInfoExtensionCount"], 6)
        self.assertEqual(status["enumeratedFeatureDisabledRequestedExtensionCount"], 0)

    def test_g925_identity_is_returned_by_driver_properties(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(
            "const uint32_t gpu_id = device->kmod.dev->props.gpu_id;\n"
            "   const bool is_g925_device = gpu_id == 0xd8300015 || "
            "gpu_id == 0xd8300010;",
            physical,
        )
        self.assertIn(".deviceID = spoof_amd ? 0x73ff", physical)
        self.assertRegex(physical, r"spoof_nvidia\s*\?\s*0x2504")
        self.assertIn("is_g925_device ? 0xd8300010 : gpu_id", physical)
        self.assertRegex(
            physical,
            r'is_g925_device\s*\?\s*"PanVK-Mali-G925"\s*:\s*device->name',
        )

    def test_g925_image_limits_match_device_report(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        for limit in (
            "maxImageDimension1D",
            "maxImageDimension2D",
            "maxImageDimension3D",
            "maxImageDimensionCube",
        ):
            self.assertIn(
                f".{limit} = is_g925_device ? 16384 : (1 << 16),",
                physical,
            )
        self.assertIn(
            ".maxImageArrayLayers = is_g925_device ? 4096 : (1 << 16),",
            physical,
        )

    def test_image_format_queries_respect_advertised_image_limits(self):
        physical = (MESA / "panvk_physical_device.c").read_text()
        self.assertIn(
            "MIN2(1 << 16, physical_device->vk.properties.maxImageDimension1D)",
            physical,
        )
        self.assertIn(
            "physical_device->vk.properties.maxImageDimension2D",
            physical,
        )
        self.assertIn(
            "physical_device->vk.properties.maxImageDimensionCube",
            physical,
        )
        self.assertIn(
            "MIN2(1 << 16, physical_device->vk.properties.maxImageDimension3D)",
            physical,
        )
        self.assertIn(
            "MIN2(1 << 16, physical_device->vk.properties.maxImageArrayLayers)",
            physical,
        )
        self.assertIn(
            "util_logbase2(MAX2(maxExtent.width,\n"
            "                         MAX2(maxExtent.height, maxExtent.depth))) +\n"
            "      1;",
            physical,
        )

    def test_g925_max_allocation_matches_device_report(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(
            ".maxMemoryAllocationSize =\n"
            "         is_g925_device ? (8ull << 30) : UINT32_MAX,",
            physical,
        )

    def test_g925_uniform_buffer_range_matches_device_report(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(
            ".maxUniformBufferRange =\n"
            "         is_g925_device ? (1u << 31) : (1 << 20),",
            physical,
        )

    def test_g925_memory_heaps_and_types_match_device_report(self):
        physical = (MESA / "panvk_physical_device.c").read_text()
        self.assertIn(
            "is_g925 ? 11759542272ull : detected_heap_size;",
            physical,
        )
        self.assertIn(
            "if (is_g925) {\n"
            "      device->memory.heap_count = 2;\n"
            "      device->memory.heaps[1] = (VkMemoryHeap){\n"
            "         .size = 104857600ull,",
            physical,
        )
        self.assertIn(
            "if (device->kmod.dev->props.supported_bo_flags &\n"
            "          PAN_KMOD_BO_FLAG_PROTECTED) {\n"
            "         device->memory.types[device->memory.type_count++] = "
            "(VkMemoryType) {\n"
            "            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |\n"
            "                             VK_MEMORY_PROPERTY_PROTECTED_BIT,\n"
            "            .heapIndex = 1,",
            physical,
        )
        self.assertIn("VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |\n"
                      "                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,", physical)
        self.assertIn("VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |\n"
                      "                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT,", physical)
        self.assertIn("VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,", physical)
        self.assertIn(".heapIndex = 1,", physical)

        memory = (MESA / "panvk_physical_device.h").read_text()
        self.assertIn("VkMemoryHeap heaps[VK_MAX_MEMORY_HEAPS];", memory)
        self.assertIn("heap_used[VK_MAX_MEMORY_HEAPS];", memory)

    def test_g925_memory_budget_matches_reported_heap_size(self):
        physical = (MESA / "panvk_physical_device.c").read_text()
        self.assertIn("if (is_g925) {", physical)
        self.assertIn("Match the system driver's reported budget for G925.", physical)
        self.assertIn(
            "p->heapBudget[i] = heap_size;",
            physical,
        )

    def test_protected_memory_requires_kernel_region_verification(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(
            ".EXT_pipeline_protected_access = true,",
            physical,
        )
        self.assertIn(
            ".protectedMemory =\n"
            "         device->kmod.dev->props.supported_bo_flags &\n"
            "         PAN_KMOD_BO_FLAG_PROTECTED,",
            physical,
        )
        self.assertIn(
            ".pipelineProtectedAccess = true,",
            physical,
        )
        self.assertIn(
            ".protectedMemory =\n"
            "         device->kmod.dev->props.supported_bo_flags &\n"
            "         PAN_KMOD_BO_FLAG_PROTECTED,",
            physical,
        )
        protected_test = (
            ROOT
            / "apps/panvk-test/app/src/main/cpp/device/protected-memory.c"
        ).read_text()
        self.assertIn("VkPhysicalDevicePipelineProtectedAccessFeatures", protected_test)
        self.assertIn(
            "PIPELINE_PROTECTED_ACCESS PASS feature-enable",
            protected_test,
        )
        self.assertIn("VK_EXT_PIPELINE_PROTECTED_ACCESS_EXTENSION_NAME", protected_test)

        common_physical = (MESA / "panvk_physical_device.c").read_text()
        self.assertIn("? VK_QUEUE_PROTECTED_BIT", common_physical)

        kmod = (ROOT / "work/mesa/src/panfrost/lib/kmod/kbase_kmod.c").read_text()
        self.assertIn("query.out.value & KBASE_REG_PROTECTED", kmod)
        self.assertNotIn("forcing protected-memory capability on G925", kmod)
        self.assertNotIn("0xd8300015 ||", kmod)

        memory = (MESA / "panvk_device_memory.c").read_text()
        self.assertIn("PAN_KMOD_BO_FLAG_PROTECTED | PAN_KMOD_BO_FLAG_NO_MMAP", memory)
        self.assertIn("protected Vulkan memory cannot be mapped", memory)

    def test_frame_boundary_is_exposed_and_retained_across_queue_submit(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(".EXT_frame_boundary = true,", physical)
        self.assertIn(".frameBoundary = true,", physical)

        queue_header = (ROOT / "work/mesa/src/vulkan/runtime/vk_queue.h").read_text()
        queue_runtime = (ROOT / "work/mesa/src/vulkan/runtime/vk_queue.c").read_text()
        synchronization = (
            ROOT / "work/mesa/src/vulkan/runtime/vk_synchronization.c"
        ).read_text()
        self.assertIn("VkFrameBoundaryEXT frame_boundary;", queue_header)
        self.assertIn("vk_find_struct_const(info->pNext, FRAME_BOUNDARY_EXT)", queue_runtime)
        self.assertIn("memcpy(submit->frame_boundary_tag", queue_runtime)
        self.assertIn(
            "vk_find_struct_const(pSubmits[s].pNext, FRAME_BOUNDARY_EXT)",
            synchronization,
        )
        self.assertIn("frame_boundary_info[s].pNext = NULL;", synchronization)

    def test_device_fault_is_backed_by_queue_error_capture(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        device = (MESA / "panvk_vX_device.c").read_text()
        queue = (MESA / "csf/panvk_vX_gpu_queue.c").read_text()
        self.assertIn(".EXT_device_fault = PAN_ARCH >= 10,", physical)
        self.assertIn(".deviceFaultEXT = PAN_ARCH >= 10,", physical)
        self.assertIn(".deviceFaultVendorBinaryEXT = false,", physical)
        self.assertIn("panvk_per_arch(GetDeviceFaultInfoEXT)", device)
        self.assertIn("pFaultCounts->addressInfoCount = 0;", device)
        self.assertIn("pFaultCounts->vendorBinarySize = 0;", device)
        self.assertIn("vk_outarray_append_typed(VkDeviceFaultVendorInfoEXT", device)
        self.assertIn("kbase_kmod_csf_has_error(dev->kmod.dev)", queue)
        self.assertIn("panvk_device_record_fault(dev, subq_ctx->last_error", queue)
        self.assertIn("panvk_device_record_fault(dev, cs_fault", queue)

    def test_swapchain_compression_is_android_wsi_only(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(
            "#ifdef VK_USE_PLATFORM_ANDROID_KHR\n"
            "      .EXT_image_compression_control_swapchain = true,\n"
            "#endif",
            physical,
        )
        self.assertIn(
            "#ifdef VK_USE_PLATFORM_ANDROID_KHR\n"
            "      .imageCompressionControlSwapchain = true,\n"
            "#endif",
            physical,
        )

    def test_swapchain_compression_request_reaches_images(self):
        wsi = (MESA / "panvk_wsi.c").read_text()
        self.assertIn(
            "vk_find_struct_const(pCreateInfo->pNext, IMAGE_COMPRESSION_CONTROL_EXT)",
            wsi,
        )
        self.assertIn(".flags = swapchain_compression ? swapchain_compression->flags", wsi)
        self.assertIn(
            "swapchain_compression->compressionControlPlaneCount",
            wsi,
        )
        self.assertIn(
            "swapchain_compression ? swapchain_compression->pFixedRateFlags : NULL",
            wsi,
        )
        self.assertIn("emici.pNext = &image_compression;", wsi)
        self.assertIn(
            "VK_IMAGE_COMPRESSION_FIXED_RATE_DEFAULT_EXT |\n"
            "          VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT",
            wsi,
        )
        self.assertIn("return VK_ERROR_FORMAT_NOT_SUPPORTED;", wsi)

    def test_panprobe_swapchain_lifecycle_exercises_optional_extensions(self):
        swapchain = (
            ROOT / "apps/panvk-test/app/src/main/cpp/swapchain.c"
        ).read_text()
        self.assertIn("VK_EXT_FRAME_BOUNDARY_EXTENSION_NAME", swapchain)
        self.assertIn("frame_boundary_query.frameBoundary", swapchain)
        self.assertIn("si.pNext = &frame_boundary;", swapchain)
        self.assertIn(".pImages = &image,", swapchain)
        self.assertIn(".pTag = &frame_tag,", swapchain)
        self.assertIn("VK_EXT_IMAGE_COMPRESSION_CONTROL_EXTENSION_NAME", swapchain)
        self.assertIn("VK_EXT_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_EXTENSION_NAME", swapchain)
        self.assertIn("swapchain_compression_query.imageCompressionControlSwapchain",
                      swapchain)
        self.assertIn(".flags = VK_IMAGE_COMPRESSION_DEFAULT_EXT,", swapchain)
        self.assertIn("sci.pNext = &compression;", swapchain)

    def test_subpass_merge_feedback_reports_unmerged_subpasses(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(".EXT_subpass_merge_feedback = true,", physical)
        self.assertIn(".subpassMergeFeedback = true,", physical)

        render_pass = (
            ROOT / "work/mesa/src/vulkan/runtime/vk_render_pass.c"
        ).read_text()
        self.assertIn("device->enabled_features.subpassMergeFeedback", render_pass)
        self.assertIn(
            "VK_SUBPASS_MERGE_STATUS_NOT_MERGED_SINGLE_SUBPASS_EXT",
            render_pass,
        )
        self.assertIn(
            "VK_SUBPASS_MERGE_STATUS_NOT_MERGED_UNSPECIFIED_EXT",
            render_pass,
        )
        self.assertIn("postMergeSubpassCount =", render_pass)

        panprobe = (
            ROOT / "apps/panvk-test/app/src/main/cpp/swapchain.c"
        ).read_text()
        self.assertIn("test_subpass_merge_feedback", panprobe)
        self.assertIn("VK_EXT_SUBPASS_MERGE_FEEDBACK_EXTENSION_NAME", panprobe)
        self.assertIn("PASS VK_EXT_subpass_merge_feedback", panprobe)

    def test_g925_astc_shared_exponent_decode_mode_is_wired(self):
        physical = (MESA / "panvk_vX_physical_device.c").read_text()
        self.assertIn(".EXT_astc_decode_mode = PAN_ARCH >= 7,", physical)
        self.assertIn(".decodeModeSharedExponent = PAN_ARCH >= 13,", physical)

        image_view = (MESA / "panvk_vX_image_view.c").read_text()
        self.assertIn(
            "astc_decode->decodeMode == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32",
            image_view,
        )
        self.assertIn(
            ".astc.shared_exponent =\n"
            "         astc_decode &&\n"
            "         astc_decode->decodeMode == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,",
            image_view,
        )
        self.assertIn(
            "astc_decode->decodeMode == VK_FORMAT_R8G8B8A8_UNORM",
            image_view,
        )

        image = (ROOT / "work/mesa/src/panfrost/lib/pan_image.h").read_text()
        self.assertIn("unsigned shared_exponent;", image)
        texture = (ROOT / "work/mesa/src/panfrost/lib/pan_texture.c").read_text()
        self.assertIn("!iview->astc.shared_exponent", texture)
        self.assertIn(
            "cfg.decode_hdr = iview->astc.hdr || iview->astc.shared_exponent;",
            texture,
        )

        panprobe = (
            ROOT / "apps/panvk-test/app/src/main/cpp/swapchain.c"
        ).read_text()
        self.assertIn("test_astc_shared_exponent", panprobe)
        self.assertIn("astc_decode_query.decodeModeSharedExponent", panprobe)
        self.assertIn("VK_FORMAT_E5B9G9R9_UFLOAT_PACK32", panprobe)
        self.assertIn("PASS VK_EXT_astc_decode_mode decodeMode=E5B9G9R9", panprobe)
