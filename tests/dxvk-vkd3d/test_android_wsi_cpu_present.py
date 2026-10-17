#!/usr/bin/env python3
import pathlib
import unittest

from _mesa_tree import mesa_root

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = mesa_root() / "src/panfrost/vulkan/panvk_wsi.c"
DEVICE_SOURCE = mesa_root() / "src/panfrost/vulkan/panvk_vX_device.c"
PATCH = ROOT / "patches/wsi/017-android-wsi-cpu-present-usage.patch"


class AndroidWsiCpuPresentTest(unittest.TestCase):
    def test_cpu_writable_window_and_matching_format_are_required(self):
        source = SOURCE.read_text()
        usage = source.index("ANativeWindow_setUsage(")
        geometry = source.index("ANativeWindow_setBuffersGeometry(", usage)

        self.assertLess(usage, geometry)
        self.assertIn("AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN", source[usage:geometry])
        self.assertIn("pCreateInfo->imageFormat != VK_FORMAT_R8G8B8A8_UNORM", source)
        self.assertNotIn("VK_FORMAT_B8G8R8A8_UNORM", source)
        self.assertIn("WINDOW_FORMAT_RGBA_8888", source)

    def test_present_does_not_hide_native_buffer_failures(self):
        source = SOURCE.read_text()
        present_start = source.index("panvk_android_swapchain_queue_present(")
        present_end = source.index("\nstatic VkResult\npanvk_android_swapchain_destroy", present_start)
        present = source[present_start:present_end]

        self.assertIn("DeviceWaitIdle(swapchain->device)", present)
        self.assertIn("if (window_lock_result != 0)", present)
        self.assertIn("if (ahb_lock_result != 0)", present)
        self.assertIn("AHardwareBuffer_unlock", present)
        self.assertIn("ANativeWindow_unlockAndPost", present)
        self.assertIn("VK_ERROR_SURFACE_LOST_KHR", present)
        self.assertIn("window_post_result != 0", present)
        self.assertIn("ANativeWindow_setUsage", PATCH.read_text())

    def test_kbase_uses_queue_submit_for_semaphore_payloads(self):
        source = DEVICE_SOURCE.read_text()
        self.assertRegex(
            source,
            r"#ifdef HAVE_PAN_KMOD_KBASE\s+"
            r"if \(!physical_device->kbase_node_path\[0\]\)\s+"
            r"#endif\s+"
            r"device->vk\.copy_sync_payloads = vk_drm_syncobj_copy_payloads;",
        )

    def test_kbase_wsi_forces_fifo_present_mode(self):
        source = SOURCE.read_text()
        wsi_init = source[source.index("panvk_wsi_init("):]
        self.assertIn(
            "physical_device->wsi_device.override_present_mode =\n"
            "         VK_PRESENT_MODE_FIFO_KHR;",
            wsi_init,
        )


if __name__ == "__main__":
    unittest.main()
