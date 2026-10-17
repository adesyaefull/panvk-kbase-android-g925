#!/usr/bin/env python3
import pathlib
import unittest

from _mesa_tree import mesa_root


SOURCE = mesa_root() / "src/vulkan/runtime/vk_android.c"


class AndroidNativeBufferImportFdTest(unittest.TestCase):
    def test_native_buffer_import_scans_for_dma_buf_fd(self):
        source = SOURCE.read_text()
        helper_start = source.index("vk_android_find_dma_buf_fd(")
        helper_end = source.index("\nVkResult\nvk_android_import_anb_memory", helper_start)
        helper = source[helper_start:helper_end]

        self.assertIn("for (int i = 0; i < handle->numFds; i++)", helper)
        self.assertIn("GetMemoryFdPropertiesKHR", helper)
        self.assertIn("VK_ERROR_INVALID_EXTERNAL_HANDLE", helper)

        import_start = source.index("vk_android_import_anb_memory(")
        import_end = source.index("\nVkResult\n", import_start + 1)
        native_import = source[import_start:import_end]

        self.assertIn("vk_android_find_dma_buf_fd(", native_import)
        self.assertNotIn("anb->handle->data[0]", native_import)

    def test_ahb_properties_use_the_same_fd_validation(self):
        source = SOURCE.read_text()
        properties_start = source.index(
            "vk_common_GetAndroidHardwareBufferPropertiesANDROID("
        )
        properties = source[properties_start:]

        self.assertIn("vk_android_find_dma_buf_fd(", properties)


if __name__ == "__main__":
    unittest.main()
