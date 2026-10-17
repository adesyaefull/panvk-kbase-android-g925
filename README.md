# PanVK for Mali-G925

This project builds and tests Mesa's PanVK Vulkan driver for Android devices
using the Mali Kbase kernel interface. Its primary target is the Mali-G925
(`PAN_ARCH=13`) with the Command Stream Frontend (CSF). PanVK is experimental;
it is not a Vulkan-conformant driver, and support varies by GPU, kernel, app,
and feature.

For version-by-version changes, fixes, experiments, package details, build
identifiers, test results, and checksums, see the
[project changelog](CHANGELOG.md).

## Project layout

- `work/mesa/` — Mesa source tree used by the local build.
- `patches/` — patch series applied by the repository build workflow.
- `profiles/` — per-device and per-generation build configuration.
- `apps/panvk-test/` — PanProbe Android app for capability reporting and
  on-device tests.
- `apps/panvk-launcher/` — PanPlay launcher and supporting tools.
- `tests/` — driver, Vulkan, DXVK, and vkd3d-proton tests.
- `scripts/` — source preparation, host-tool bootstrap, build, packaging, and
  validation workflows.
- `validation/` — feature implementation status and available test evidence.

## Target and build environment

| Item | G925 target |
|---|---|
| GPU | Mali-G925; reported GPU ID `0xd8300010` |
| GPU architecture | PAN_ARCH 13 |
| Frontend | CSF |
| Kernel interface | Mali Kbase |
| Android ABI/API | AArch64 / API 35 minimum |
| Android NDK | r29 |

Build the Android ICD with the repository script and a compatible NDK:

```sh
./scripts/build-android.sh --profile g925-v13-csf --ndk /path/to/android-ndk
```

The host environment also needs Mesa's host code generators,
`glslangValidator` 12.2 or newer, and—when building with the experimental
KRAID compiler—Rust 1.85 or newer with the `aarch64-linux-android` target and
bindgen 0.71.1 or newer. PanProbe is built from `apps/panvk-test/`; use it on a
device to query capabilities and run focused driver tests.

## Driver capabilities

The table summarizes the main G925 driver paths. Vulkan extension availability
is device- and build-dependent; applications should query the ICD they load.

| Area | Current support |
|---|---|
| Graphics and compute | PanVK CSF graphics, compute, and transfer workloads. Transfer is not exposed as a separate asynchronous queue family. |
| Android presentation | Android native-window presentation with FIFO, IMMEDIATE, and MAILBOX modes. These modes do not provide distinct queue-scheduling behavior. |
| Descriptors | Descriptor indexing and mutable descriptor support, with restrictions on mutable type combinations. |
| Shader compilation | Mesa shader cache support. The Rust-based KRAID compiler can be selected for supported shader stages on Valhall and newer GPUs. |
| Command buffers | Applications can reset and reuse command buffers; internal command-pool allocations are pooled and reused. |
| Robustness | `VK_KHR_robustness2` is implemented and has focused G925 runtime test evidence; Vulkan CTS coverage has not been established. |
| Scheduling controls | `VK_ARM_scheduling_controls` applies a per-queue shader-core maximum. It does not reserve cores or guarantee utilization. |
| Protected access | `VK_EXT_pipeline_protected_access` feature query and device enable have passed on the tested G925. Protected-memory allocation is unavailable on that tested Kbase kernel. |
| Ray query / acceleration structures | `VK_KHR_acceleration_structure`, `VK_KHR_deferred_host_operations`, and `VK_KHR_ray_query` are exposed on supported architectures with experimental, constrained compute paths. They are not general Vulkan ray tracing or DXR support; unsupported query shapes and stages remain limited. |
| Ray-tracing pipeline | `VK_KHR_ray_tracing_pipeline` is not supported. |

The G925 extension implementation and evidence ledger is maintained in
[validation/g925-v13-csf/extension-implementation-status.json](validation/g925-v13-csf/extension-implementation-status.json).
These results describe the named test cases only and do not imply Vulkan CTS
conformance or universal game compatibility.

## BCn texture emulation

PanVK emulates BCn formats through GPU decoding into a shadow image when the
hardware path cannot sample the compressed format directly. The compressed
source data is retained; the shadow is an additional image payload, so its
size is not the total image allocation.

The compute decoder is the default. Fragment decoding is an opt-in comparison
path for eligible Pan v10+ 2D images (`PANVK_DEBUG=bc_gfx`); compute remains
the fallback for older architectures, 3D images, and configurations that
cannot use the graphics path. The compact compute shadows are intentionally
lower precision than the fragment-path formats.

| BCn source | Compact compute shadow | Fragment-path shadow |
|---|---|---|
| BC1 | RGB565 | RGBA8_UNORM |
| BC2 / BC3 / BC7 | A4R4G4B4 | RGBA8_UNORM |
| BC4 UNORM / SNORM | R8_UNORM / R8_SNORM | R8_UNORM / R8_SNORM |
| BC5 UNORM / SNORM | R8G8_UNORM / R8G8_SNORM | R8G8_UNORM / R8G8_SNORM |
| BC6H UFLOAT | RGB565 | B10G11R11_UFLOAT |
| BC6H SFLOAT | RGB565 | RGBA16_SFLOAT |

For a 1024x1024 base level, a 2-byte-per-texel compute shadow is 2 MiB;
BC4's 1-byte shadow is 1 MiB. The fragment path is 4 MiB for 4-byte formats
and 8 MiB for BC6H SFLOAT's 8-byte format. At 2048x2048, these payloads are
four times larger. These figures exclude the compressed source, row/slice
padding, mip levels, array/depth layers, and modifier/layout overhead.

Fragment-path RGBA8 shadows may use AFRC on supported GPUs and eligible
formats. AFRC can be lossy; BC4/BC5 and floating-point shadows do not use it,
and cube images avoid it because cube sampling has not been validated. BC
visual correctness, per-game compatibility, performance, and measured memory
impact must be assessed on the target device.

The [bcn_layer project](https://github.com/leegao/bcn_layer) is an optional BC
compatibility reference, not a bundled dependency. No bcn_layer source or
binary is vendored or packaged here. See [NOTICE.md](NOTICE.md) and the
relevant source/patch headers for license and provenance details.

## Runtime configuration

The complete PanVK, Kbase, Android WSI, compiler, and shader-cache environment
variable reference is in [envar.md](envar.md). Most options are diagnostic or
experimental; do not combine debug switches indiscriminately for normal game
use.

## Credits and source provenance

- **G925 project:** [panvk-kbase-android-g925](https://github.com/adesyaefull/panvk-kbase-android-g925).
- **Project foundation:** [zenithblue-oss/panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android), including its Kbase CSF beta.8 baseline.
- **Mesa source base:** [Mesa commit `5a07217f034b3e50d8c7c7794f97a2df1742613b`](https://gitlab.freedesktop.org/mesa/mesa/-/tree/5a07217f034b3e50d8c7c7794f97a2df1742613b).
- **Kbase CSF reference:** [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf/tree/d9cbb91e5df720f0a62c4b5010d96c327aec315f).

These references identify the G925 CSF source lineage; they are not all
included in this build. Older-GPU, JM, launcher, and other third-party
provenance remains documented in [NOTICE.md](NOTICE.md) and in the relevant
source and patch headers.