#!/usr/bin/env python3
import pathlib
import unittest

from _mesa_tree import mesa_root


GPU_QUEUE = (
    mesa_root() / "src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c"
)


class KbaseTracebufAutoVaTest(unittest.TestCase):
    def test_kbase_tracebuf_uses_kernel_assigned_va(self):
        source = GPU_QUEUE.read_text()
        start = source.index("static VkResult\ninit_subqueue_tracing(")
        end = source.index("\nstatic void\nfinish_subqueue(", start)
        init = source[start:end]

        self.assertIn("if (gpu_queue_uses_kbase(dev))", init)
        self.assertIn(".start = PAN_KMOD_VM_MAP_AUTO_VA", init)
        self.assertIn("dev_addr = vm_op.va.start;", init)
        self.assertIn("panvk_as_alloc(dev, dev->as.priv_heap", init)

    def test_kbase_tracebuf_cleanup_does_not_free_unreserved_va(self):
        source = GPU_QUEUE.read_text()
        start = source.index("static void\nfinish_subqueue_tracing(")
        end = source.index("\n#ifdef HAVE_PAN_KMOD_KBASE\nstatic VkResult\nkbase_submit_init_subqueues", start)
        finish = source[start:end]

        self.assertIn("if (!gpu_queue_uses_kbase(dev))", finish)
        self.assertIn("panvk_as_free(dev, dev->as.priv_heap", finish)

    def test_trace_decodes_command_streams_and_hardware_trace(self):
        source = GPU_QUEUE.read_text()
        start = source.index("panvk_queue_submit_process_debug(")
        end = source.index("\nVkResult\npanvk_per_arch(gpu_queue_submit)", start)
        debug = source[start:end]

        self.assertIn("pandecode_cs_binary", debug)
        self.assertIn("pandecode_cs_trace", debug)
        self.assertNotIn("if (!gpu_queue_uses_kbase(dev))", debug)

    def test_trace_decodes_init_and_dump_command_streams(self):
        source = GPU_QUEUE.read_text()

        init_call = source.index('"Init subqueue %d binary')
        init_guard = source.rfind("if (!gpu_queue_uses_kbase(dev))", 0, init_call)
        init_trace = source.rfind("if (PANVK_DEBUG(TRACE))", 0, init_call)
        self.assertLess(init_guard, init_trace)

        dump_call = source.index("pandecode_cs_binary(dev->debug.decode_ctx")
        dump_guard = source.rfind("if (!gpu_queue_uses_kbase(dev))", 0, dump_call)
        dump_mode = source.rfind(
            "if (dev->debug.decode_ctx && PANVK_DEBUG(DUMP))", 0, dump_call
        )
        self.assertLess(dump_guard, dump_mode)

        self.assertIn("pandecode_cs_binary(dev->debug.decode_ctx", source)

    def test_decoder_skips_null_sample_locations_address(self):
        source = (mesa_root() / "src/panfrost/genxml/decode.c").read_text()
        start = source.index("GENX(pandecode_sample_locations)(")
        end = source.index("\n}\n#endif /* PAN_ARCH >= 6 */", start)
        decoder = source[start:end]

        null_guard = decoder.index("if (!sample_locations)")
        gpu_fetch = decoder.index("PANDECODE_PTR_VAR")
        self.assertLess(null_guard, gpu_fetch)
        self.assertIn("Sample locations: <null address>", decoder)

    def test_decoder_skips_null_frame_shader_dcd_address(self):
        source = (mesa_root() / "src/panfrost/genxml/decode.c").read_text()
        start = source.index("GENX(pandecode_frame_shader_dcds)(")
        end = source.index("\n}\n\nvoid\nGENX(pandecode_sample_locations)", start)
        decoder = source[start:end]

        null_guard = decoder.index("if (!dcd_pointer)")
        dcd_fetch = decoder.index("PANDECODE_PTR_VAR")
        self.assertLess(null_guard, dcd_fetch)
        self.assertIn("Frame shader DCDs: <null address>", decoder)

    def test_decoder_skips_null_texture_plane_address(self):
        source = (mesa_root() / "src/panfrost/genxml/decode.c").read_text()
        start = source.index("pandecode_tex_plane(")
        end = source.index("\n}\n#endif", start)
        decoder = source[start:end]

        null_guard = decoder.index("if (!u)")
        gpu_fetch = decoder.index("pandecode_fetch_gpu_mem")
        self.assertLess(null_guard, gpu_fetch)
        self.assertIn("Plane %u: <null address>", decoder)

    def test_decoder_skips_unmapped_texture_plane_address(self):
        source = (mesa_root() / "src/panfrost/genxml/decode.c").read_text()
        start = source.index("pandecode_tex_plane(")
        end = source.index("\n}\n#endif", start)
        decoder = source[start:end]

        mapping_guard = decoder.index("pandecode_gpu_mem_range_is_mapped")
        gpu_fetch = decoder.index("pandecode_fetch_gpu_mem")
        self.assertLess(mapping_guard, gpu_fetch)
        self.assertIn("<unmapped address", decoder)

    def test_decoder_skips_unmapped_resource_list(self):
        source = (mesa_root() / "src/panfrost/genxml/decode.c").read_text()
        start = source.index("pandecode_resources(")
        end = source.index("\n}\n\nvoid\nGENX(pandecode_resource_tables)", start)
        decoder = source[start:end]

        mapping_guard = decoder.index("pandecode_gpu_mem_range_is_mapped")
        gpu_fetch = decoder.index("pandecode_fetch_gpu_mem")
        self.assertLess(mapping_guard, gpu_fetch)
        self.assertIn("is not mapped", decoder)

    def test_texture_decoder_skips_null_surface_base_before_plane_walk(self):
        source = (mesa_root() / "src/panfrost/genxml/decode.c").read_text()
        start = source.index("void\nGENX(pandecode_texture)(struct pandecode_context *ctx,\n                        const struct mali_texture_packed")
        end = source.index("\n}\n#endif", start)
        decoder = source[start:end]

        null_guard = decoder.index("if (!temp.surfaces)")
        plane_loop = decoder.index("for (unsigned i = 0; i < plane_count; ++i)")
        self.assertLess(null_guard, plane_loop)
        self.assertIn("Texture planes: <null address>", decoder)


if __name__ == "__main__":
    unittest.main()
