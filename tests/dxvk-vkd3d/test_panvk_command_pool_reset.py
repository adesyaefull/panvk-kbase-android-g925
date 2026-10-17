#!/usr/bin/env python3
import pathlib
import re
import unittest

from _mesa_tree import mesa_root

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = mesa_root() / "src/panfrost/vulkan/panvk_cmd_pool.c"
PATCH = ROOT / "patches/common/020-panvk-command-pool-reset-buffer.patch"


class PanvkCommandPoolResetTest(unittest.TestCase):
    def test_command_pools_enable_individual_buffer_reset(self):
        source = SOURCE.read_text()
        self.assertRegex(
            source,
            re.compile(
                r"VkCommandPoolCreateInfo create_info = \*pCreateInfo;\s*"
                r"create_info\.flags \|= "
                r"VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;\s*"
                r"VkResult result =\s*"
                r"vk_command_pool_init\(&device->vk, &pool->vk, "
                r"&create_info, pAllocator\);"
            ),
        )
        self.assertIn(
            "VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT",
            PATCH.read_text(),
        )


if __name__ == "__main__":
    unittest.main()
