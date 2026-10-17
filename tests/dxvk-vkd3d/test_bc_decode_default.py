#!/usr/bin/env python3
import pathlib
import unittest

from _mesa_tree import mesa_root


SOURCE = mesa_root() / "src/panfrost/vulkan/panvk_image.c"


class BcDecodeDefaultTest(unittest.TestCase):
    def test_compute_is_default_and_fragment_decode_is_opt_in(self):
        source = SOURCE.read_text()
        start = source.index("image->bc_gfx =")
        end = source.index("if (image->bc_emul)", start)
        selection = source[start:end]

        self.assertIn("PANVK_DEBUG(BC_GFX)", selection)
        self.assertIn("!PANVK_DEBUG(BC_COMPUTE)", selection)
        self.assertIn("!PANVK_DEBUG(NO_GS)", selection)


if __name__ == "__main__":
    unittest.main()
