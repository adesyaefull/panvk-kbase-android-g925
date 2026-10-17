#!/usr/bin/env python3
import pathlib
import re
import unittest

from _mesa_tree import mesa_root

ROOT = pathlib.Path(__file__).resolve().parents[2]
KBASE = mesa_root() / "src/panfrost/lib/kmod/kbase_kmod.c"
PATCH = ROOT / "patches/kbase-common/019-kbase-vm-always-auto-va.patch"


class KbaseVmAutoVaTest(unittest.TestCase):
    def test_kbase_vm_always_enables_auto_va(self):
        source = KBASE.read_text()
        self.assertRegex(
            source,
            re.compile(
                r"pan_kmod_vm_init\(&kbase_vm->base,\s*dev,\s*"
                r"0\s*/\* no kernel handle \*/,\s*"
                r"flags\s*\|\s*PAN_KMOD_VM_FLAG_AUTO_VA\)"
            ),
        )
        self.assertIn("flags | PAN_KMOD_VM_FLAG_AUTO_VA", PATCH.read_text())


if __name__ == "__main__":
    unittest.main()
