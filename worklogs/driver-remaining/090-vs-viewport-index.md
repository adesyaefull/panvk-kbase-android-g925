# 090 VS/TES ViewportIndex

Status: **verified on Mali-G615 v11 and Mali-G925 v13** with PanProbe. Not run under CTS. The v13 test build passed the full 17-test suite on 2026-10-06.

Patch: `patches/csf-v11/090-select-viewports-for-vs-and-tes-viewportindex-writes.patch`. `shaderOutputViewportIndex` is on for v10, v11, and v13 (`PAN_ARCH >= 10 && (PAN_ARCH < 12 || PAN_ARCH == 13)`). It remains off on v12 and v14 pending validation of viewport run splitting on those architectures.

## Approach

- A VS or TES (last pre-raster stage, no GS) that writes ViewportIndex gets the gpu_prerast variants. The new `panvk_gpu_prerast_vs_vp_passthrough_nir` (`panvk_vX_shader.c`) reads the lowered-VS records, applies the selected viewport transform, and stores the scissor planes in a free clip slot (`vp_scissor_loc`). The FS discards on those planes, as for the GS path.
- When static depth clipping is enabled without depth clamp, the passthrough also emits depth-clip planes. The FS tests those planes so clipping uses the original clip-space depth, even after the viewport transform.
- The draw is split into ordered viewport runs with the 089 kernel `panlib_vp_runs` (`libpan/vp_runs.cl`), now instance aware (`per_instance`, records of instance k at k*n). Each run gets its viewport's LOW/HIGH_DEPTH_CLAMP (`csf/panvk_vX_cmd_draw.c`, `gpu_prerast_vp_prepare` and `gpu_prerast_draw_generated`). Tess-fed draws build the run list inside the compute loop (nested prerast).
- Arena: the gpu_prerast arena was allocated only for fillModeNonSolid, geometryShader, xfb or vertexPipelineStoresAndAtomics. A device with only shaderOutputViewportIndex had no arena and the first draw hung the GPU. `panvk_vX_device.c` now also allocates it for shaderOutputViewportIndex.
- Fixed in the same patch: the push-uniform dirty mask used the GS passthrough variant index for a VS/TES shader (`panvk_vX_cmd_draw.c`).
- Review (codex gpt-6.1-sol): fixed the record-capacity check for instanced runs and gated the feature to v10/v11.

## G925 v13 follow-up (2026-10-06)

- The initial G925 run correctly failed because v13 did not advertise `shaderOutputViewportIndex`; the v10/v11 implementation had been deliberately gated while run splitting was being validated.
- Enabled the feature for v13 only and completed the depth-clip passthrough plumbing required by the shared VS/TES and GS paths.
- G925 `vs_viewport_index`: all six VS/TES cases passed, including clamp, clip, no-clamp, and instanced cases; all measured depth/color mismatches were zero.
- `VK_EXT_shader_viewport_index_layer` is enumerated on G925 v13 alongside the enabled core `shaderOutputViewportIndex` feature; the six-case workload passed with the extension present.
- Full PanProbe autorun: 17/17 passed, including `vertex_stores`, `gs_viewport_depth`, `vs_viewport_index`, and `swapchain_lifecycle`.
- `swapchain_lifecycle` also passed four consecutive focused runs.

## G925 GS viewport-depth race follow-up (2026-10-06)

- Reproduced intermittent `gs_viewport_depth` failures where a case produced only the clear buffer. The viewport-run kernel consumes VS/GS records and indices written by earlier compute dispatches.
- Added an explicit wait for outstanding compute scoreboard slots before launching `panlib_vp_runs`. The existing CSF sync remains in place to notify the vertex/tiler queue after the run list is produced.
- Rebuilt and installed the Android ICD and PanProbe APK. `gs_viewport_depth` passed 28 consecutive clean autoruns on the G925, each with `mismatch=0`.
- Full PanProbe autorun passed 17/17, including `gs_viewport_depth`, `vs_viewport_index`, and `swapchain_lifecycle`.

## Limits

- `gl_PrimitiveID` in the FS restarts at each viewport run, and instances are flattened in the run list. Not covered by the test.
- Polygon-mode draws use the union depth range (no run list).
- v12 and v14: feature remains off pending device validation.
- Not CTS tested (dynamic_state/draw shader_viewport_index not run).
