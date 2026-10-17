# Changelog

## G925 v0.18.68-dev-kbase-trace-unmapped-guard (2026-10-17)
- Keep full CS binary and hardware-trace decoding enabled while validating
  resource-list and texture-plane GPU address ranges before pandecode fetches.
  The latest G925 log reached `decode.c:660` with an unmapped resource address
  and `decode.c:491` with an unmapped plane address; those malformed or
  unavailable ranges are now reported and skipped rather than dereferenced.
  This prevents the decoder-side access violation without hiding or disabling
  the rest of the trace. G925 Android/API 35 build and focused source regression
  tests pass; runtime tracing still needs device validation.
  Test ADPKG:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.68-dev-kbase-trace-unmapped-guard-20261017-5a07217f.adpkg.zip`
  (ICD SHA-256 `8c7c224d9f10e168c04e5aefecb2e050a6c80d63313628db81874ef5bbb77875`,
  archive SHA-256 `fc677f923a8f91c2e6569f90d55566b45777d3584c98b35999a0474beb181ae5`).
  Reproducible decoder source: [G925 v13 patch 172](patches/csf-v13/172-kbase-pandecode-mapped-range-guards.patch).

## Unreleased

- Restore command-stream and hardware trace decoding on Kbase. The temporary
  v0.18.66 diagnostic bypass is removed; do not use that package for full
  tracing. Treat a null texture-surface base as an absent plane list and report
  it instead of calculating and fetching bogus per-plane addresses. Null
  sample-location and frame-shader DCD addresses are also reported without
  dereferencing them. Command-stream binaries and hardware trace records remain
  decoded on Kbase and other backends. Device validation is still required.
  Test ADPKG:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.67-dev-kbase-trace-full-decode-20261017-5a07217f.adpkg.zip`
  (ICD SHA-256 `49a2dcc46ac2bd8dfeb67a74fdde1e836c4cb7a74a384c92467494be58f04652`,
  archive SHA-256 `1d5cf148219fc603b1b64ebc8bc4b918ec816502e748041912540b08d66752fe`).
- Map CSF command-stream trace buffers through Kbase `AUTO_VA` instead of
  reserving userspace GPU virtual addresses, which Kbase cannot honor. Avoid
  releasing those kernel-assigned addresses to PanVK's userspace VA allocator.
  The Android G925 ICD compiles and links with this change; runtime tracing
  still needs device validation.
- Keep BC compute decoding as the default and make the fragment decoder opt-in
  via `PANVK_DEBUG=bc_gfx`. The current image setup selected fragment decoding
  for nearly every 2D BC image, despite the v0.18.26/v0.18.27 policy and the
  documented requirement to verify the shadow modifier first. This restores
  the established compute path for normal game startup; device validation is
  still required.
- Avoid DRM syncobj payload copying for Kbase devices. The v0.18.57 log showed
  that `QueueSubmit2 failed` was a misleading label: PanVK had selected the
  CPU-side `copy_sync_payloads` path, while Kbase queue submission already
  resolves semaphore waits/signals through its Kbase-aware path. Kbase now
  leaves that DRM callback unset, so Android release-image synchronization
  uses the normal queue-submit path. Add deeper syncobj operation/errno logs
  for diagnosis if the failure persists. Android arm64/API 35 ICD build and
  five focused Android WSI/native-buffer tests pass; G925 runtime validation
  is still required, so the black-screen fix is not yet confirmed. Test ADPKG:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.58-dev-kbase-release-sync-20261016-5a07217f.adpkg.zip`
  (ICD SHA-256 `51c1c1987ee5050e8caf748a47081926e12d42913e15a7abbe12737a356ec9a8`,
  archive SHA-256 `3e39ead479c79d7d9f989396c5a6d90bac4e50dd66f808b11a09c66bedb10cb4`).
- Add direct `mesa_loge` output for each failure stage in
  `vkQueueSignalReleaseImageANDROID`, complementing the existing
  `vk_errorf` diagnostics. Android arm64/API 35 ICD compile/link and both
  focused native-buffer FD regression tests pass. This remains diagnostic
  instrumentation, not a black-screen fix; G925 device retest is required.
  Diagnostic ADPKG:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.57-dev-android-release-sync-logcat-20261016-5a07217f.adpkg.zip`
  (ICD SHA-256 `04b201f1950c238ad1be470076f6c96ff042d20256bf626bf49fa210c03d899f`,
  archive SHA-256 `ab535529796aa126ae2e9ed18cc195f87e3ab8c310e0d54cfe13f271e72ebea5`).
- Scan all native-buffer handle FDs for an importable DMA-BUF when importing
  `VkNativeBufferANDROID`, and share the validated FD lookup with the AHardwareBuffer
  properties query. Android arm64/API 35 ICD build/link and two focused source
  regression tests pass; G925 renderer behavior and the black-screen fix remain
  unverified on device. Test package:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.55-dev-ahb-import-fd-scan-20261016-5a07217f.adpkg.zip`
  (ICD SHA-256 `ee9712ffde186b964c9cb71cba3a75fa2b754de70afee4c3d116a320b19dc655`,
  archive SHA-256 `66438f2422d5a90980c63fdd1d679a880388f02bd57b49d3d6293cdfea246075`).
- Add stage-specific errors to `vkQueueSignalReleaseImageANDROID` so a failed
  `QueueSubmit2` can be distinguished from failed sync-file export. The
  v0.18.55 device log shows this extension returning `VK_ERROR_UNKNOWN` (-13)
  repeatedly, followed by renderer swapchain `OUT_OF_DATE`; the earlier
  `GetMemoryFdPropertiesKHR` error is absent during the test interval.
  Android arm64/API 35 ICD rebuild passes. This is diagnostic only; the
  release-image sync failure and black screen still require a runtime fix.
  Test package:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.56-dev-android-release-sync-diag-20261016-5a07217f.adpkg.zip`
  (ICD SHA-256 `a6be97782c17c9208d651e2a797e9f68da2f859c48165812b02e541ea79d51d6`,
  archive SHA-256 `398c636c57344a16cef56df6c07b27bd085c9d3a0871544cc12de935f89c3998`).
- Enable `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT` on PanVK command
  pools so applications can reset/recycle individual buffers without
  destroying and recreating them. Keep Vulkan barrier stage/access semantics
  intact; the CSF path already filters dependencies by subqueue and has a
  framebuffer-specific `BY_REGION` path. Descriptor-pool storage is already
  suballocated from a GPU-addressable BO per Vulkan descriptor pool.
- Always enable automatic VA assignment in the Kbase VM backend. Kbase
  allocates GPU addresses itself and cannot map BOs at PanVK-selected VAs;
  this keeps backend VM flags consistent even when device enumeration did not
  identify the Kbase node. The PES startup explicit-VA failure should be
  retested on device.
- Stop advertising `bufferDeviceAddressCaptureReplay` on Kbase, which cannot
  map allocations at caller-chosen GPU virtual addresses. Preserve the feature
  on Panthor when the device exposes an extended VA range. The Android arm64/
  API 35 ICD builds successfully; PES runtime retest remains pending.
- Enable `VK_EXT_pipeline_protected_access` independently of protected-memory
  support, as allowed by the extension specification. Keep protected-memory
  features gated on Kbase's protected-region verification, and extend PanProbe
  to query and enable pipeline protected access separately. Android arm64/API
  35 build, static ICD validation, and G925 query/device-enable test pass;
  protected-memory allocation remains unavailable on the tested Kbase kernel.
- Port the upstream BC graphics-decode path for Pan v10+ 2D images, keeping
  compute fallback for older architectures, 3D images, and debug overrides.
  Use compact renderable shadow formats and tiled modifiers; select AFRC for
  RGBA8 shadows when supported, with upstream per-format CU defaults and the
  `PANVK_BC_AFRC` override. Regenerate compute and fragment SPIR-V and update
  the BC verifier and documentation. Android build and static ICD validation
  pass; BC correctness/performance on device remain untested.

### BCn decode and shadow implementation details

PanVK emulates BCn sampling with GPU decoders that populate a shadow image
while preserving the compressed source blocks. Compute decoding is the default
path. Fragment decoding is opt-in for eligible Pan v10+ 2D images; older
architectures, 3D images, and ineligible layouts use compute decoding.

| BCn source | Compact compute shadow | Fragment-path shadow |
|---|---|---|
| BC1 | RGB565 | RGBA8_UNORM |
| BC2 / BC3 / BC7 | A4R4G4B4 | RGBA8_UNORM |
| BC4 UNORM / SNORM | R8_UNORM / R8_SNORM | R8_UNORM / R8_SNORM |
| BC5 UNORM / SNORM | R8G8_UNORM / R8G8_SNORM | R8G8_UNORM / R8G8_SNORM |
| BC6H UFLOAT | RGB565 | B10G11R11_UFLOAT |
| BC6H SFLOAT | RGB565 | RGBA16_SFLOAT |

The compact compute formats are intentionally lower precision. Fragment-path
RGBA8 shadows may use supported AFRC coding units; BC4/BC5 and floating-point
shadows do not use AFRC, and cube images avoid AFRC because cube sampling has
not been validated. `PANVK_BC_AFRC` can override AFRC selection for eligible
shadows. `PANVK_DEBUG=bc_gfx` selects the fragment path for comparison, while
`bc_compute` forces compute. The linear compute-shadow layout aligns rows to
at least 64 bytes; actual image allocation also includes source data, padding,
mips, layers, and layout alignment. Payload-size examples and the complete
runtime-option reference are in [envar.md](envar.md) and the BCn section of
[README.md](README.md).

The `bcn_layer` project is an optional compatibility reference only. No
bcn_layer source or binary is vendored or packaged in this repository; see
[NOTICE.md](NOTICE.md) for attribution and licensing.

### Experimental acceleration-structure and Ray Query implementation notes

The G925 CSF path contains experimental acceleration-structure build and
compute Ray Query work. PanVK can create and build tested BLAS/TLAS shapes,
write acceleration-structure device addresses to descriptors, and lower a
constrained Ray Query compute-shader shape. G925 device tests have exercised
zero-, one-, and multi-geometry BLAS builds, a four-instance TLAS, tree
traversal, descriptor address consumption, and candidate/committed-hit
results. A tested shader exercised query initialization, proceed, getters,
confirmation, and termination for the supported scene/query shape.

This is not general Vulkan ray tracing or DXR support. The implementation is
limited to tested query forms and compute stages; general query shapes,
arbitrary scene structures, full update/compaction/serialization/copy/host
operations, and Vulkan CTS coverage are incomplete. The ray-tracing-pipeline
extension remains disabled. Do not infer broad conformance from the focused
PanProbe tests.

## G925 v0.18.54-dev-android-wsi-cpu-present (2026-10-16)

- Request CPU-write usage before locking Android native-window buffers for
  PanVK's CPU present copy. Advertise only the RGBA8 format shared by the
  AHardwareBuffer and native-window copy path.
- Propagate device-idle, buffer-lock/unlock, and window-lock/post errors rather
  than reporting a successful present when no visible frame was delivered.
- Android arm64/API 35 PanVK WSI source compile and ICD link pass with NDK r29
  (Build ID `4715349f41a72431947b8ca5e13135f0febc3862`); six focused regression
  tests pass. G925 game-renderer validation remains pending.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.54-dev-android-wsi-cpu-present-20261016-5a07217f.adpkg.zip`
  (ICD SHA-256 `69e1cccba229fcf11315d698f0674fe34b152ae3fa2b94cbb618e2d6fd0d080f`,
  archive SHA-256 `10f6479077f0db87ff668228ae368660b8d87a8cff06ed851b3e9fbf3c330f4c`).

## G925 v0.18.51-dev-kbase-auto-va (2026-10-15)

- Always enable `AUTO_VA` when initializing a Kbase VM. This makes the VM
  contract match Kbase's BO allocation behavior even if device enumeration
  omitted the Kbase node path; caller-selected BO mappings cannot be honored.
- Android arm64/API 35 incremental Ninja compile/link passes with NDK r29
  (Build ID `e5c8273100532f197db409a1076bab972a9035b5`, ICD SHA-256
  `6a7dc50e52099057aae6302de54db290fe6c5f012c3fb849e3c5add016afee28`).
  Focused Kbase AUTO_VA and capture/replay regression tests pass. Device-side
  PES startup retest remains pending.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.51-dev-kbase-auto-va-20261015-5a07217f.adpkg.zip`
  (archive SHA-256 `21f061785d85defbe8c08db92540b8d83f43ddd6e6dda1be190d66057bd8a05f`).

## G925 v0.18.52-dev-command-buffer-reset (2026-10-15)

- Always enable `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT` on PanVK
  command pools so applications can reset individual buffers.
- Android arm64/API 35 changed-source compile and ICD link pass with NDK r29
  (Build ID `fc5767ea7f648329016c84087bfc3003c56343a1`, ICD SHA-256
  `4cbbf6a7af7a2ec039f7a573b548dbeb68a2a06d7aafc527ecf1591492da51b4`).
  Command-pool reset regression test passes; runtime behavior remains
  untested.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.52-dev-command-buffer-reset-20261015-5a07217f.adpkg.zip`
  (archive SHA-256 `14db68cd6544af7df8d93a909601dfafdb400fc3545f8d22300b7cf8fb1f3df3`).

## G925 v0.18.53-dev-deferred-descriptor-flush (2026-10-15)

- Defer userspace cache-maintenance operations for write-back-mapped BOs until
  the existing sync/submit boundary, and coalesce overlapping or adjacent
  ranges for the same BO and operation. Preserve flush-on-BO-destruction so
  pending operations never outlive their CPU mapping.
- Android arm64/API 35 PanVK and Kmod source compile/link pass with NDK r29
  (Build ID `a4582d304e40626ff8da4ccbe26029938bc33e04`, ICD SHA-256
  `ce65b626e56b304fe5785d13aa467c3e568f3e6864a052ddfb671436c6946930`).
  Four focused source regression tests pass. Device performance validation
  for descriptor-heavy loading remains pending.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.53-dev-deferred-descriptor-flush-20261015-5a07217f.adpkg.zip`
  (archive SHA-256 `a7c404fe53262570691577f98d7f1efe54858f93fb09981fbf461512045e6ef8`).

## G925 v0.18.49-dev-pipeline-protected-access (2026-10-15)

- Enable the pipeline protected-access feature independently from protected
  memory; PanVK accepts the pipeline-access declaration flags without changing
  generated binaries. Protected-memory support remains gated on successful
  Kbase protected-region verification.
- Extend the PanProbe protected-memory test to enable and report the pipeline
  feature even when the device cannot provide protected allocations.
- Android arm64/API 35 ICD build and static validation pass (Build ID
  `6d024e6a1b66076aeadd502408f5f4b033c0c9f2`, SHA-256
  `f373e28df2a2f0a35192032e5602211cc9a23eafb3a70f8fbb93e6ec73353e37`).
- On the OnePlus 2602BPC18G (Mali-G925), PanProbe reports
  `PIPELINE_PROTECTED_ACCESS PASS feature-enable`. Protected-memory allocation
  is separately skipped because the Kbase probe returned flags `0x280c`,
  without `KBASE_REG_PROTECTED`.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.49-dev-pipeline-protected-access-20261015-5a07217f.adpkg.zip`
  (SHA-256 `1420845a952ac978ee2e481b85f012d07502a48c7c14d92188cc8b36f6c36467`).

## G925 v0.18.41-dev-bc-rgba4444 (2026-10-14)

- Keep BC1/BC6H decoded shadows as RGB565 with identity component mapping.
  Switch BC2/BC3/BC7 shadows to A4R4G4B4 to retain four bits per color and
  alpha channel; preserve BC4/BC5 formats. Update shader packing, verifier, and
  mapping documentation. Android arm64/API 35 build and static ICD validation
  pass (Build ID `ff75dbae4228d9cb2ffdc538d8b9cec6ad230b18`, ICD SHA-256
  `0938960779b1387164affa3194c7d4ab40c965a8149bdc95ef59bb427351ff00`).
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.41-dev-bc-rgba4444-20261014-5a07217f.adpkg.zip`
  (SHA-256 `5408ed6be8d983caa85d45fb8c52be5b45741884366f19ea01c6bfeb4c7cfedc`).
- Device-side BC visuals and game runtime remain untested.

## G925 v0.18.39-dev-bc-linear-layout (2026-10-14)

- Force decoded BC shadows to use the linear DRM modifier and matching layout
  handler, rather than inheriting the source image modifier. PanVK's linear
  layout aligns each mip row pitch to at least 64 bytes and derives slice,
  mip, and allocation sizes from that pitch.
- Match the requested BC mapping: BC1/BC6H RGB565; BC2/BC3/BC7 A1R5G5B5;
  BC4/BC5 retain their UNORM/SNORM variants. Update shader packing, verifier,
  and mapping/layout documentation. Android arm64/API 35 ICD build and static
  validation pass (Build ID `171d33d41762fcb66f8aa630f7ad075926bc0fb1`,
  SHA-256 `f51e5430af21ef47e3d56568697f8d24b9b42100f035975cfcf9f1c37a29d6c7`).
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.39-dev-bc-linear-layout-20261014-5a07217f.adpkg.zip`
  (SHA-256 `304494e427fbddf1fc866f2dc087d6433e2aca122386f3877da0e40752ea353f`).
- BC visual correctness and runtime remain untested on-device.

## G925 v0.18.38-dev-bc-alpha4 (2026-10-14)

- Restore BC1 RGB shadows to RGB565 and map BC2/BC3/BC7 shadows to RGBA4444,
  preserving 16 levels of alpha for translucent content. Keep BC6H RGB555 and
  BC4/BC5 UNORM/SNORM mappings unchanged.
- Update the S3TC/BC7 compute decoders, embedded SPIR-V, format mapping,
  verifier, and BC mapping documentation. Android arm64/API 35 ICD build and
  static validation pass (Build ID `538c5461ca1538ad12b95209c3f504b899f0e9f9`,
  SHA-256 `a720ec5953154fea80ae1fb9514ba7f3fd76428da57a69658fcbf82fdf2c29e8`).
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.38-dev-bc-alpha4-20261014-5a07217f.adpkg.zip`
  (SHA-256 `261624b19776fa325ff63564180d397d4269fa8d6c1b91256dcc98970a75ba64`).
- Device BC visual correctness/performance and game runtime remain untested.

## G925 v0.18.37-dev-bc-rgb555 (2026-10-14)

- Apply round-to-nearest 5-bit quantization to every RGB channel in 16-bit
  compact color shadows, using A1R5G5B5 storage. BC1/BC6H set alpha to one;
  BC2/BC3/BC7 quantize source alpha to one bit.
- Update the BC compute decoders, embedded SPIR-V generation, format mapping,
  host verifier, and BC mapping documentation. Changed compute and fragment
  SPIR-V passes `spirv-val` for Vulkan 1.1. Android arm64/API 35 ICD build and
  static validation pass (Build ID `0c56452e07f4c0d6fec8feb2855235a74af91ef1`,
  SHA-256 `b474534b44e6a139c07bd9a9eef8a49c36ad8b0f136595078972f1c97e826732`).
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.37-dev-bc-rgb555-20261014-5a07217f.adpkg.zip`
  (SHA-256 `de5f0678ee644b8a0833658c9c9a503105320b0d67d491a124ee5e695c8ea40c`).
- BC visuals/performance have not been tested on device. The user confirmed
  RE2 MSAA works on v0.18.36; it has not been retested on this build.

## G925 v0.18.36-dev-msaa-restored (2026-10-14)

- Restore PanVK's prior architecture/format-dependent MSAA sample counts
  (1x/4x, 2x on arch 12+, and 8x/16x when supported), and restore compiler
  `max_samples` to 16. This addresses the RE2 D3D11 texture-creation failure
  reported for a 4x resource.
- Keep the Android swapchain maximum at three images and the command-pool idle
  cache cap unchanged.
- Build the Android arm64/API 35 ICD and pass static Android ICD validation.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.36-dev-msaa-restored-20261014-5a07217f.adpkg.zip`
  (SHA-256 `0450dc9d4b25cdf26bee87a7614ac858a977b383e7637286b855fc4dfc9ec1b8`).
- Device runtime and confirmation that RE2's 4x texture now creates remain
  pending.

## G925 v0.18.35-dev-bc-snorm (2026-10-14)

- Preserve BC4/BC5 signed-normalized values in the decoded shadow by using
  R8_SNORM/R8G8_SNORM storage and signed-normalized packing in the compute
  decoder. Keep UNORM mappings unchanged and preserve BC5 R/G channel order.
- Keep alpha-capable shadow formats for BC2/BC3/BC7; do not force RGB565
  because Vulkan format metadata cannot establish whether the game uses alpha.
- Update the host BC verifier to compare signed-normalized decoded values.
- Build the Android arm64/API 35 ICD and pass static Android ICD validation.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.35-dev-bc-snorm-20261014-5a07217f.adpkg.zip`
  (SHA-256 `5403cb1b3fde81b7a8c2c8e4107357209c48288e0fc4881e676bcf3b623f78f8`).
- Device runtime, BC visual correctness, and allocation validation remain
  pending.

## G925 v0.18.34-dev-resource-caps (2026-10-13)

- Initially limited reported framebuffer and sampled-image MSAA support to
  1x/2x and capped shader compiler sample counts at 2. Reverted in v0.18.36
  after a game failed to create a 4x MSAA texture.
- Limit Android swapchains to at most three images, report that limit through
  surface capabilities, and reject create requests outside the reported
  2–3-image range instead of silently returning fewer images than requested.
- Cap cached idle command-pool BOs at 16 MiB total across each command pool's
  CS, descriptor, varying, and TLS caches. New idle BOs over the budget are
  released immediately; reused BOs are removed from the cached-byte total.
- This intentionally reduces exposed MSAA/swapchain capacity. Build and
  device-runtime validation are pending.

## G925 v0.18.33-dev-memory-pool (2026-10-13)

- Add 32 MiB backing BO pools for device-local, non-host-visible Vulkan memory
  allocations up to 16 MiB. Allocations are suballocated at 64 KiB minimum
  alignment and returned to the pool when `vkFreeMemory` is called.
- Keep one empty chunk per memory type for reuse and release additional empty
  chunks. Pool backing allocations are reflected in the device-local heap
  accounting.
- Apply each suballocation's BO offset to sparse bindings, CPU image copies,
  image CRC initialization, mapping, and cache synchronization.
- Keep imported/exportable, dedicated, host-visible, protected, lazily
  allocated, capture/replay, memory-report, and debug-decoder allocations on
  the existing per-allocation path. Allocation failure in the pool falls back
  to the existing path.
- The pool is generic: Vulkan allocation calls do not identify whether memory
  will be used for vertex/index data, so this does not selectively pool only
  geometry buffers. Build and device runtime validation are pending.

## G925 v0.18.32-dev-bc-runtime-log (2026-10-13)

- Add `PANVK_DEBUG=bc_expansion` logging for each created BC-emulated image:
  source/shadow formats, extent, mip/layer counts, decode path, modifier, raw
  and shadow layout sizes, gap, row pitch, and total allocation size.
- Review confirmed the generic layout already rounds source plane sizes to
  4 KiB. A prior attempt to reduce the following shadow's offset alignment
  did not reduce allocation size and was removed; no RAM savings are claimed
  from that alignment change.
- Stop advertising `VK_PRESENT_MODE_MAILBOX_KHR` and
  `VK_PRESENT_MODE_IMMEDIATE_KHR` from the Android WSI. Its current present
  path waits for device idle and copies through `ANativeWindow`; it does not
  implement either mode's queue semantics. FIFO remains available and
  applications retain control over present-mode selection.
- The remaining architecture strategies in the shared table are already
  partly implemented or cannot be safely imposed by the driver: PanVK pools
  internal BO allocations and reuses command-buffer resources; coherent host
  memory, descriptor indexing, and the disk shader cache are already exposed.
  No asynchronous transfer queue is added, and sync stage masks are not
  weakened.

## G925 v0.18.43-dev-present-modes (2026-10-15)

- Re-advertise Android `VK_PRESENT_MODE_FIFO_KHR`,
  `VK_PRESENT_MODE_IMMEDIATE_KHR`, and `VK_PRESENT_MODE_MAILBOX_KHR` as
  requested. The current `ANativeWindow` presentation path remains synchronous
  and does not provide distinct FIFO, immediate, or mailbox queue scheduling
  semantics; verify behavior on the target device.

## G925 v0.18.44-dev-scheduling-controls (2026-10-15)

- Apply `VK_ARM_scheduling_controls` shader-core limits to kbase CSF queue
  groups. Each group now uses the device's full reported shader-core count by
  default and honors a non-zero per-queue `shaderCoreCount` request, capped at
  the physically reported count. This sets the kbase group's compute/fragment
  endpoint maxima; it does not reserve cores or guarantee full GPU utilization.

## G925 v0.18.46-dev-mutable-combined (2026-10-15)

- Add `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` to supported mutable
  descriptor lists. Reserve two slots per mutable descriptor so the texture
  remains at slot 0 and its sampler at slot 1, including standalone sampler
  descriptors. Update layout sizing, descriptor pool accounting, set-layout
  hashing, descriptor writes/copies, and NIR descriptor indexing accordingly.
- Focused mutable/DX12 regression tests pass. Android arm64/API 35 build and
  static ICD validation pass (Build ID `64e6407af6b097d99e69cc799adf1888a83960a5`,
  ICD SHA-256 `1a969810bf95db9b1cd8cd3749d45ac3528e5122735f9a637f12a5109a423466`).
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.46-dev-mutable-combined-20261015-5a07217f.adpkg.zip`
  (ADPKG SHA-256 `d25be4f2f07dfbc525dde83814cd5c6df0ce73eb2ea72f710defe445088e6fe8`).
- Mutable descriptor storage is twice the previous per-descriptor size.
  Descriptor update/copy behavior and BC sampling still require device runtime
  validation.

## G925 v0.18.47-dev-mutable-combined-rollback (2026-10-15)

- Disable mutable `COMBINED_IMAGE_SAMPLER` support and restore the prior
  single-slot mutable descriptor layout after the v0.18.46 build was followed
  by a RE2 `VK_ERROR_DEVICE_LOST` report. Keep the other descriptor-indexing,
  mutable descriptor, BC, and scheduling changes unchanged.
- The rebuilt ICD matches the previously recorded v0.18.45 binary SHA-256
  `cc47c3df3edfd852b7cb50f45c889880197530b367bcaab1d156634a2d04a510`.
  Android arm64/API 35 static validation and rollback regression tests pass.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.47-dev-mutable-combined-rollback-20261015-5a07217f.adpkg.zip`
  (ADPKG SHA-256 `a705e73d3dfe18ebf6559e527f5154a693515c33d7067524e9f5dc38a23e21fe`).
- RE2 device testing confirmed the rollback resolves the black screen/freeze
  seen with v0.18.46. Mutable combined support remains disabled; the precise
  internal fault mechanism is not isolated.

## G925 v0.18.48-dev-mutable-combined-fixed (2026-10-15)

- Re-enable mutable combined image-sampler descriptors with padded descriptor
  slots, matching texture/sampler array offsets, maximum YCbCr plane capacity,
  and per-element type tracking for descriptor copies. Mutable descriptor
  lists that also allow uniform/storage buffers are rejected because PanVK's
  buffer resource-index format assumes a unit stride.
- Focused mutable descriptor regressions pass. Android arm64/API 35 build and
  static ICD validation pass (Build ID
  `2b894246945b4e3051f1218fd32b587042689bc0`, ICD SHA-256
  `d2d82f27de3cb36af7abb7e5b67b084cb22f0ad3e72edba3673168d8be254ba5`).
- User testing confirms the RE2 black-screen/freeze is resolved on this build.
  The v0.18.47 rollback remains available.
- Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.48-dev-mutable-combined-fixed-20261015-5a07217f.adpkg.zip`
  (ADPKG SHA-256
  `5552439d7f30c77c816f249655a99c7ccd167e141c8751effa8cb73267d3f12b`).

## G925 v0.18.45-dev-dx12-descriptor-features (2026-10-15)

- Verify and keep enabled the Pan v9+ descriptor-indexing, mutable-descriptor,
  64-bit integer atomic, and 32-bit float atomic extension features used by
  DX12 bindless/resource heaps.
- Extend `VK_EXT_mutable_descriptor_type` layout-list support to sampler and
  input-attachment descriptors, which use the existing single-slot descriptor
  path. Combined image-sampler mutable lists remain unsupported because they
  require a multi-slot layout. Application runtime validation is pending.
- Android ICD build succeeded at
  `build/android-bionic/src/panfrost/vulkan/libvulkan_panfrost.so` (Build ID
  `5156623b1d1a89931f7cd6e16a76a9992c37a113`, SHA-256
  `6a248d0c572d7f4f2fbde1cc713e785947ff04036aca9fbcd862ef8da99795f5`).
  Packaged as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.32-dev-bc-runtime-log-20261013-5156623b.adpkg.zip`
  (ADPKG SHA-256
  `01028a8aa4aac8b73baed0bdc7ea0fdfb152c538bf15bf67ef4e486c7d4809d8`).
  Device/emulator runtime and BC image
  correctness remain untested.

## G925 v0.18.30-dev-bc-compact (2026-10-13)

- Change the GPU-decoded BC shadow storage formats to reduce the uncompressed
  shadow payload:

  | Source | Shadow format | Bytes/texel |
  |---|---|---:|
  | BC1 RGB/RGBA, UNORM/sRGB | R5G6B5_UNORM | 2 |
  | BC2 UNORM/sRGB | A4R4G4B4_UNORM | 2 |
  | BC3 UNORM/sRGB | A1R5G5B5_UNORM | 2 |
  | BC4 UNORM/SNORM | R8_UNORM | 1 |
  | BC5 UNORM/SNORM | R8G8_UNORM | 2 |
  | BC6H UFLOAT/SFLOAT | R5G6B5_UNORM | 2 |
  | BC7 UNORM/sRGB | A4R4G4B4_UNORM | 2 |

- For a 1024x1024 image, shadow payload is 2 MiB for every listed format
  except BC4 (1 MiB). At 2048x2048 these are 8 MiB and 4 MiB respectively.
  These figures exclude the retained compressed source plane, layout
  alignment, and row/slice padding.
- Pack two 16-bit texels or four BC4 texels per 32-bit compute-shader store to
  avoid overlapping partial-word writes. Match dispatch dimensions to each
  shader's local workgroup size, including odd image extents.
- Update the host BC verifier to compare against the corresponding quantized
  shadow values. Regenerate and embed the compute decoder SPIR-V.
- The output is intentionally lossy: BC1 alpha is discarded; BC2/BC7 channels
  are quantized to 4 bits; BC3 alpha to 1 bit; BC6H HDR and negative values are
  clamped to UNORM; and negative BC4/BC5 SNORM values are clamped to zero.
  sRGB input is converted to linear before writing to the UNORM shadow.
- Rebuild the Android arm64/API 35 G925 ICD (Build ID
  `22a93aa933ec4e17d47db745793fda8ca51d8f97`, SHA-256
  `5fdd4e8ea76bedf9e5d0fad3a954a2165a5f9d8602ff7070f3dd6cd2902e5a67`).
  Build succeeded. Package as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.30-dev-bc-compact-20261012-5a07217f.adpkg.zip`
  (SHA-256
  `aee8a00a23a9bd4b92801b96821ce179a2aeff7e25af874aced112aee9fc1c35`).
  Archive entries, embedded ICD checksum, metadata checksum, and Build ID
  were verified. Device/emulator runtime, image correctness, performance, and
  measured allocation savings have not been tested.

## G925 v0.18.27-dev-bc-afbc-auto (2026-10-12)

- Automatically select the fragment BC decoder only when a probe image with
  the matching shadow format, extent, and usage selects AFBC. This avoids
  choosing the graphics path when its decoded shadow would be linear; the
  compute decoder remains the fallback. `PANVK_DEBUG=bc_compute` forces
  compute, and `PANVK_DEBUG=bc_gfx` forces graphics for comparison.
- The probe follows the normal image modifier selection, including AFRC
  overrides and per-image format/usage support checks. It does not change
  Vulkan image residency or use sparse binding.
- Rebuilt the Android arm64/API 35 G925 ICD (Build ID
  `767b741c0fdeb23b98820787a5ab7a1a2c8a1c0b`, SHA-256
  `aab95b31c56eb7a497184a9873f8131878dbc044cb96bad935eb768e8a212f4a`).
  Android static ELF/ICD validation passed. Packaged for Switch-emulator
  custom-driver import as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.27-dev-bc-afbc-auto-20261012-5a07217f.adpkg.zip`
  (SHA-256 `88104e56f6a5653de24c46fda1171844f69be7254039bcf714a9cbeb80262064`)
  and outer bundle
  `dist/PanVK-G925-Yuzu-Switch-v0.18.27-dev-bc-afbc-auto-20261012-5a07217f.zip`
  (SHA-256 `45ae24f2abe12fe0c663eb0dbe8bb16848a2c520f55db58aed019368f3f5edad`).
  ZIP integrity, metadata, and embedded checksums passed. Device runtime, BC
  image correctness, memory savings, and performance remain untested.

## G925 v0.18.26-dev-bc-compute-tuning (2026-10-12)

- Optimize S3TC, RGTC, BC6H, and BC7 compute decoding: each 16x4 workgroup
  shares compressed-block payload reads across the invocations that decode
  the same 4x4 block. Update dispatch sizing for the new workgroup geometry.
  The compute decoder is the default; `PANVK_DEBUG=bc_gfx` selects the
  fragment path for comparison. This reduces redundant payload loads in the
  shader, but no device-side speedup or image-correctness result has been
  measured.
- Give GPU virtual-address allocations of 4 MiB or larger a minimum 64 KiB
  alignment. This is an address-placement heuristic only; it does not enable
  huge pages or change the kernel's physical page size.
- Report `maxMemoryAllocationCount = 4096` to bound independently allocated
  BOs. Descriptor update-after-bind limits remain architecture-defined; they
  are not hardcoded to 500,000.
- Add PanVK-specific persistent disk-cache creation while retaining Mesa's
  privileged-process safeguards and GPU-specific cache identity.
- Include the 256 KiB CSF ring buffer and the descriptor-copy batching
  introduced in v0.18.25. These are implementation changes, not measured
  performance guarantees.
- Carry forward the v0.18.23/.24 identity-only switches, versioned shader
  cache keys, the architecture-13 HSR prepass default, and proactive tiler-heap
  renewal with batched CSF ring publication. The BC change does not add a
  lossy quality mode.
- Rebuilt the Android arm64/API 35 ICD (Build ID
  `0886d6d3b39a770e3a1d9edc6238dacbf4ac02a2`, SHA-256
  `e109d0bd63fb4c9498efb688d70c8f7a1cf61f8b0bfe0ee192fa6cc28ec62442`).
- Packaged for Switch-emulator custom-driver import as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.26-dev-bc-compute-tuning-20261012-5a07217f.adpkg.zip`
  (SHA-256 `c03c7b87a2a1ca9a38316f74fba9464b5f511e5c11927d44e0a909554384556b`)
  and outer bundle
  `dist/PanVK-G925-Yuzu-Switch-v0.18.26-dev-bc-compute-tuning-20261012-5a07217f.zip`
  (SHA-256 `e0ec6c09f2e93a3cd47ed88b72bb0d59c4de09759ed88409be34f14463e7fa48`).
  Android ELF metadata, archive entries, ZIP integrity, and embedded checksums
  passed validation. Emulator/device runtime, BC image correctness, FPS, and
  memory impact remain untested.

## G925 v0.18.25-dev-descriptor-copy (2026-10-12)

- Batch copies of contiguous fixed-layout descriptors; mutable descriptor
  copies retain the existing conservative per-element path.
- Preserve the optional identity-only experiments from v0.18.23 and v0.18.24;
  neither spoof adds vendor hardware features or changes the default Mali
  identity.
- Android ICD and Switch-emulator archive validation passed. Emulator import,
  runtime behavior, and game performance were not tested.

## Optional NVIDIA identity spoof for compatibility testing (2026-10-11)

- Add `PANVK_DEBUG=spoof_nvidia` to report vendor `0x10de`, device `0x2504`,
  name `NVIDIA GeForce RTX 3060`, and discrete-GPU type for compatibility
  experiments. Normal G925 identity remains the default.
- This changes reported identity only; it does not advertise NVIDIA features,
  alter Vulkan capabilities, or turn PanVK into an NVIDIA driver. D3D12/game
  compatibility and runtime stability remain unverified.

## Optional AMD identity spoof for compatibility testing (2026-10-11)

- Add `PANVK_DEBUG=spoof_amd` to report AMD vendor `0x1002`, device `0x73ff`,
  name `AMD Radeon RX 6600`, and discrete-GPU type for compatibility
  experiments. Normal G925 identity remains the default.
- This changes reported identity only; it does not advertise AMD features,
  alter Vulkan capabilities, or turn PanVK into an AMD driver. When both AMD
  and NVIDIA spoof flags are present, AMD takes precedence. Runtime and game
  compatibility remain unverified.

## Shader cache key hardening (2026-10-11)

- Version Vulkan shader-stage BLAKE3 keys and encode each input as a tagged,
  length-delimited field. This prevents distinct variable-length inputs
  (including identifiers and specialization data) from aliasing through
  ambiguous byte concatenation; BLAKE3 remains the content hash.
- Keep Mesa's existing persistent cache path for preprocessed shader data and
  compiled shader objects. Pipeline creation during a game's loading phase and
  a stable, writable cache directory are still required to warm the cache;
  PanVK cannot discover pipelines the application has not submitted.
- Cache behavior and first-use stutter reduction still require G925 runtime
  validation.

## G925 KRAID liveness and hardware HSR prepass review (2026-10-11)

- Enable the architecture-13 hardware HSR prepass by default, while retaining
  PanVK's framebuffer-pipeline eligibility checks. Set `PANVK_HSR_PREPASS=0`
  to disable it; `PANVK_DEBUG=hsr_prepass` continues to force it on.
- Keep discard/coverage and alpha-to-coverage semantics unchanged. PanVK's
  early-ZS and HSR paths already account for discard as a coverage update;
  translating arbitrary discard into alpha-to-coverage or an automatic
  depth-only pass would not preserve Vulkan behavior.
- KRAID already uses last-use liveness when choosing/releasing registers and
  runs a pressure-reducing schedule before register allocation. No speculative
  allocator rewrite was made without shader spill evidence.
- Runtime FPS, overdraw, and correctness impact of enabling HSR by default
  still require G925 device validation.

## G925 proactive tiler allocation and CSF ring batching (2026-10-11)

- Before a work-heavy graphics submission crosses the configured
  `PANVK_KBASE_HEAP_RENEW_WORK` threshold, proactively renew the kbase tiler
  heap and double its initial allocation from 10 to at most 20 chunks (20 MiB
  at the G925 1 MiB chunk size). kbase retains its on-demand growth up to the
  existing maximum. The same policy applies to the first work-heavy
  submission.
- Keep the CPU and GPU mappings of each CS ring persistent for the queue
  lifetime. Ring entries were already published once per subqueue per Vulkan
  submit; defer the full-system barrier until that batch is published, rather
  than issuing it once for every ring entry.
- Build/device performance and heap-memory behavior still require G925 runtime
  validation.

## BC shadow AFBC layout experiment (2026-10-10)

- Add `PANVK_BC_SHADOW_AFBC=1` to request a supported AFBC modifier only for
  decoded BC shadows, and `=2` to exclude AFBC only for those shadows. The
  default `=0` preserves normal modifier selection. The forced-AFBC mode checks
  format, usage, and GPU support first and warns/falls back if unavailable.
- Extend `PANVK_DEBUG=bc_expansion` logs with the decoded shadow format and
  actual modifier, including its numeric DRM modifier value.
- AFRC remains disabled by default; an explicit `PANVK_BC_AFRC` override takes
  precedence for eligible shadows. No device-side performance or image
  correctness results are available yet.
- Built and statically validated Android arm64 ICD (Build ID
  `b1bf4469b759d5e1dab79ac6f358c60441174504`, SHA-256
  `9ed846e08a1b6e92096f34ad86d7af2e066d9fb68bf1c9f70d5d33e0243f6ef7`).
- Packaged for Switch-emulator custom-driver import as
  `dist/PanVK-G925-Yuzu-Switch-v0.18.21-dev-bc-shadow-afbc-20261010-5a07217f.adpkg.zip`
  (SHA-256 `4589eef5e85696625f989251f254280ea1a6093676135d4e5fd645926be67ab9`)
  and outer bundle
  `dist/PanVK-G925-Yuzu-Switch-v0.18.21-dev-bc-shadow-afbc-20261010-5a07217f.zip`
  (SHA-256 `6c4282acbf2f5b088fb824ad734f94d48c1f0d1b7ac0789713701269b93c7e98`).
  Package contents, checksums, and ZIP integrity passed validation; emulator
  import/runtime, image correctness, and game performance remain untested.

## G925 AFRC default-off follow-up (2026-10-10)

- Disable AFRC for BC shadows by default again. The user confirmed that the
  AFRC-enabled ICD froze, while `PANVK_BC_AFRC=0` ran without freezing.
- Keep the AFRC modifier path available only as an explicit
  `PANVK_BC_AFRC=16|24|32` experiment. Normal capability-checked modifier
  selection, including AFBC where supported, remains unchanged.
- Rebuilt separate default-off Android ICD:
  `dist/experiments/afrc-afbc-default-off-20261010/libvulkan_panfrost.so`
  (SHA-256 `edadc5544d2dc480b3217a50c0b95967bfaa41377a34a1b09490d83e244ae0b3`,
  Build ID `57bc3399041de3703991b89d24da6518c237b1e9`).
- Switch-emulator ADPKG:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.20-dev-bc-afrc-off-20261010-5a07217f.adpkg.zip`
  (SHA-256 `93328b6345506c379d54c231d5294e8a644c55c6276330efdc1d08eef79bd411`).
  Outer ZIP:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.20-dev-bc-afrc-off-20261010-5a07217f.zip`
  (SHA-256 `80509ec3457b154a305da5f631c3cfb835157d1a0273d56e9889aa27590a9548`).
- Android static binary validation passed. Switch import/runtime and game
  testing for this rebuilt package are pending; the prior AFRC-on artifact is
  retained separately for comparison.

## G925 AFRC+AFBC experiment (2026-10-10)

- Temporarily enabled AFRC by default for eligible non-cube RGBA8 BC shadow images when the
  GPU reports support: BC1/BC2 use 24-byte coding units and BC3/BC7 use 32-byte
  coding units. `PANVK_BC_AFRC=0` disables the experiment.
- Other images retain capability-checked modifier selection, which prefers
  AFBC where supported. AFRC and AFBC are alternative modifiers for each image;
  this combines their use across different images rather than stacking both on
  a single image.
- Built Android ICD:
  `dist/experiments/afrc-afbc-20261010/libvulkan_panfrost.so`
  (Build ID `2864765845a896ba803aa5ec7d50f4bb3a8382d4`). Static Android binary
  validation passed. Device-side correctness and performance remain untested.
- Packaged the ICD for Switch-emulator custom-driver import:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.19-dev-afrc-afbc-experiment-20261010-5a07217f.adpkg.zip`
  (SHA-256 `7b3df368b1c5002ac38707418c762c32f82fe3573d1c073f53ee23d56d583ddc`),
  with outer bundle
  `dist/PanVK-G925-Yuzu-Switch-v0.18.19-dev-afrc-afbc-experiment-20261010-5a07217f.zip`
  (SHA-256 `bc8400eaabf96220def6e42424a89d8ac38ca772f7bbe85b0d82a0a818a80771`).

## G925 v0.18.18-dev-command-pool-release-20261009

- Honor `VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT` by first resetting all
  command buffers, then releasing cached command-stream, descriptor, varying,
  and TLS BOs from the command pool.
- This only releases free command-pool cache entries; it does not reclaim
  resources still referenced by live objects, device-wide TLS, or the tiler
  heap.
- Rebuilt the G925 Android ICD and PanProbe APK, then packaged the updated ICD
  in the Switch-emulator ADPKG and combined PanProbe ZIP. The APK's embedded
  ICD has the same Build ID as the standalone driver.
- Static Android ELF/ICD validation and APK signature verification passed.
  Switch-emulator import/runtime, device-side PanProbe execution, and measured
  RAM savings have not been tested.

## G925 v0.18.17-dev-lazy-prerast-20261008

Development build for Mali-G925 / PAN_ARCH 13, Android arm64, API 35.

Mesa source: [commit `5a07217f034b3e50d8c7c7794f97a2df1742613b`](https://gitlab.freedesktop.org/mesa/mesa/-/tree/5a07217f034b3e50d8c7c7794f97a2df1742613b).

GitHub project and implementation-reference credits are listed in the
[README source and reference credits](README.md#source-and-reference-credits);
licensing and vendoring details are in [NOTICE.md](NOTICE.md).

### Driver updates

- Avoid redundant descriptor-state invalidation when an unchanged descriptor
  set is rebound; track updates so real writes and copies still invalidate.
- Skip color-attachment preload only when there is no resolve, previous
  contents are not required, and the result is discarded.
- Add opt-in BC allocation/decode-path reporting with
  `PANVK_DEBUG=bc_expansion`.
- Keep AFBC modifier selection capability-validated; BC-shadow AFRC remains
  off by default.
- Add opt-in KRAID shader compile timing and binary-size reporting through
  `PAN_KRAID_TIMING=1`.
- Keep kbase GPU semaphore waits disabled by default.
- Add opt-in tiler-heap renewal diagnostics through
  `PANVK_DEBUG=heap_stats`.
- Allocate GPU pre-raster scratch arenas on the first draw that needs them,
  rather than at device creation; allocation remains explicit and CPU-side
  before GPU submission for kbase safety.

### Build and packages

- Android driver:
  `dist/android-g925-v13-csf/libvulkan_panfrost.so`
- Switch-emulator driver package:
  `dist/PanVK-G925-Yuzu-Switch-v0.18.17-dev-lazy-prerast-20261008-5a07217f.adpkg.zip`
- Combined emulator and PanProbe bundle:
  `dist/PanVK-G925-Yuzu-Switch-PanProbe-v0.18.17-dev-lazy-prerast-20261008-5a07217f.zip`
- PanProbe APK:
  `apps/panvk-test/app/build/outputs/apk/debug/app-debug.apk`

The driver passed static Android ELF/ICD validation. PanProbe APK signature
verification and package/archive checksum checks passed. Switch-emulator
import/runtime, device-side PanProbe execution, and game performance have not
been validated.

## G925 v0.18.16-dev-fps-driver-panprobe-20261008

- Track descriptor-set updates and avoid redirtying unchanged descriptor
  bindings.
- Skip color-attachment preload only when prior contents are not required,
  there is no resolve, and the result is discarded.
- Add opt-in BC expansion and allocation diagnostics with
  `PANVK_DEBUG=bc_expansion`.
- Add opt-in KRAID compile-time and binary-size diagnostics with
  `PAN_KRAID_TIMING=1`.
- Add opt-in kbase tiler-heap renewal diagnostics with
  `PANVK_DEBUG=heap_stats`.
- Keep AFBC selection capability-checked, decoded-BC AFRC off by default, and
  kbase GPU semaphore waits disabled by default.
- Rebuild and bundle PanProbe with the G925 ICD. Static/package checks passed;
  emulator import, device execution, and game performance were not tested.

## G925 v0.18.15-dev-kraid-default-20261008

- Build the Android driver with the Rust KRAID backend enabled and default
  KRAID shader selection to all supported stages on architecture v9+.
- Add the Rust Android cross target and document the Rust/bindgen build
  prerequisites.
- Build and package the KRAID-enabled G925 driver. Runtime correctness and
  performance were not tested.

## G925 v0.18.14-dev-bc-afrc-off-20261008

- Turn off AFRC by default for decoded BC shadow images after the v0.18.12
  lower-rate experiment regressed reported game performance.
- Keep the normal capability-checked modifier selection; this does not force
  AFBC or disable AFRC on unrelated images.

## G925 v0.18.13-dev-bc-afrc-defaults-20261008

- Revert the v0.18.12 AFRC rate reduction to the previous rates (BC1/2: 24;
  BC3/7: 32) after the experiment made the game heavier and reduced FPS.
- The restored rates were an intermediate release; v0.18.14 subsequently
  disabled AFRC by default for decoded BC shadows.

## G925 v0.18.12-dev-bc-afrc16-20261008

- Experimentally lower the AFRC coding rate for eligible decoded BC shadow
  images to 16, targeting reduced decoded-image storage.
- The user observed worse game performance; the setting was reverted in
  v0.18.13 and later disabled by default in v0.18.14.

## G925 v0.18.11-dev-fps-optimizations-20261008

- Integrate Bifrost register-allocation and compiler improvements, including
  liveness/interference, spill/constraint, and SSA/NIR changes from the
  corresponding upstream patch series.
- Enable Mesa's Android disk shader cache by default. PanPlay supplies a
  writable per-container cache path and, when using DXVK, a configuration
  disabling `dxvk.trackPipelineLifetime`.
- These changes primarily target shader compilation time and first-use
  stutter; no general average-FPS improvement was established.

## G925 v0.18.10-dev-bcn-rework-20261008

- Integrate the BCn shadow rework: add PanVK BC texture shadow/decode paths
  with compact formats and fragment/compute decode handling where applicable.
- Preserve format and usage checks for image modifiers; the release did not
  establish a game-performance result for every BC format or title.
