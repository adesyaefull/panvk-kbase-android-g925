#!/usr/bin/env python3
import pathlib
import re
import unittest

from _mesa_tree import mesa_root

ROOT = pathlib.Path(__file__).resolve().parents[2]
PHYSICAL = mesa_root() / "src/panfrost/vulkan/panvk_vX_physical_device.c"
PATCH = ROOT / "patches/kbase-common/018-buffer-device-address-capture-replay-kbase.patch"


class KbaseCaptureReplayTest(unittest.TestCase):
    def test_capture_replay_requires_panthor_and_extended_va(self):
        expression = re.compile(
            r"\.bufferDeviceAddressCaptureReplay\s*=\s*"
            r"\(PAN_ARCH\s*>=\s*10\)\s*&&\s*"
            r"!device->kbase_node_path\[0\]\s*&&\s*"
            r"\(device->memory\.max_supported_va\s*>\s*\(1ull\s*<<\s*32\)\)"
        )
        self.assertRegex(PHYSICAL.read_text(), expression)
        self.assertIn("Kbase cannot preserve captured VAs", PATCH.read_text())


if __name__ == "__main__":
    unittest.main()
