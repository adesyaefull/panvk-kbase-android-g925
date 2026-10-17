#!/usr/bin/env python3
import os
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MESA = pathlib.Path(os.environ.get("PANVK_MESA", ROOT / "work" / "mesa"))

GPU_QUEUE = (
    MESA / "src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c"
).read_text()


class KbaseCsfErrorHandlingTest(unittest.TestCase):
    def test_queue_error_wait_does_not_decode_untrusted_cs_streams(self):
        start = GPU_QUEUE.rindex(
            "kbase_subqueue_wait_seqno(struct panvk_gpu_queue *queue"
        )
        end = GPU_QUEUE.index("\nstatic ", start)
        wait_body = GPU_QUEUE[start:end]
        self.assertNotIn("pandecode_cs_binary", wait_body)
        self.assertIn("kbase_kmod_csf_has_error(dev->kmod.dev)", wait_body)
        self.assertIn("if (error_type < 0)", wait_body)
        self.assertIn(
            "if (kbase_kmod_csf_has_error(dev->kmod.dev))\n"
            "         return panvk_per_arch(gpu_queue_check_status)",
            wait_body,
        )
        self.assertLess(
            wait_body.index("if (kbase_kmod_csf_has_error(dev->kmod.dev))"),
            wait_body.index("if (cell->seqno >= target_seqno"),
        )
        self.assertIn("strerror(errno)", wait_body)
        self.assertIn("return vk_queue_set_lost(&queue->vk,", wait_body)


if __name__ == "__main__":
    unittest.main()
