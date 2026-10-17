#!/usr/bin/env python3
import pathlib
import unittest

from _mesa_tree import mesa_root

ROOT = pathlib.Path(__file__).resolve().parents[2]
MESA = mesa_root()
KMOD = MESA / "src/panfrost/lib/kmod/pan_kmod.c"
KMOD_H = MESA / "src/panfrost/lib/kmod/pan_kmod.h"
BACKEND_H = MESA / "src/panfrost/lib/kmod/pan_kmod_backend.h"
PATCH = ROOT / "patches/common/021-defer-user-cache-bo-syncs.patch"


class DeferredUserCacheSyncTest(unittest.TestCase):
    def test_user_cache_syncs_are_coalesced_and_deferred(self):
        source = KMOD.read_text()
        queue_start = source.index("pan_kmod_queue_bo_map_sync(")
        queue = source[queue_start:]
        user_start = queue.index(
            "if (pan_kmod_can_sync_bo_map_from_userland(dev))"
        )
        user_end = queue.index(
            "\n   simple_mtx_lock(&dev->pending_bo_syncs.lock);", user_start
        )
        user_path = queue[user_start:user_end]

        self.assertIn("user_cache_array", user_path)
        self.assertIn("start <= sync_end && sync->start <= end", user_path)
        self.assertIn("bo->has_pending_deferred_syncs = true", user_path)
        self.assertNotIn("util_flush_range_no_fence", user_path)
        self.assertNotIn("util_flush_inval_range_no_fence", user_path)

        flush_start = source.index("pan_kmod_flush_bo_map_syncs_locked(")
        flush_end = source.index("\nint\npan_kmod_cs_event_signal(", flush_start)
        flush = source[flush_start:flush_end]
        self.assertIn("util_flush_range_no_fence(sync->cpu_ptr, sync->size)", flush)
        self.assertIn("util_flush_inval_range_no_fence(sync->cpu_ptr, sync->size)", flush)
        self.assertIn("util_post_flush_inval_fence()", flush)

        self.assertIn("struct util_dynarray user_cache_array;", KMOD_H.read_text())
        self.assertIn("util_dynarray_init(&dev->pending_bo_syncs.user_cache_array", BACKEND_H.read_text())
        self.assertIn("util_dynarray_fini(&dev->pending_bo_syncs.user_cache_array", BACKEND_H.read_text())
        self.assertIn("user_cache_array", PATCH.read_text())


if __name__ == "__main__":
    unittest.main()
