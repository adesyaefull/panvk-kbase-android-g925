# PanVK environment variables

This guide lists the environment variables and debug switches recognized by
the current PanVK G925 Android build and the Mesa compiler/cache code it uses.
Options are build- and platform-dependent: for example, X11/Termux WSI options
do not apply to an Android-native surface. Unset options use the defaults
listed below.

## `PANVK_DEBUG`

Set `PANVK_DEBUG` to a comma-separated list of PanVK debug options. For
example:

```sh
PANVK_DEBUG=bc_expansion,heap_stats
```

Debug switches are primarily for diagnostics and controlled comparisons; they
are not general performance presets.

| Option | Effect |
|---|---|
| `startup` | Log the active PanVK debug switches during instance creation. |
| `nir` | Enable NIR-related diagnostics. |
| `trace` | Enable PanVK tracing diagnostics. |
| `sync` | Enable synchronization diagnostics. |
| `noafbc` | Disable AFBC image-modifier selection. |
| `linear` | Request the linear image path where the affected code supports it. |
| `dump` | Enable driver dump diagnostics. |
| `cs` | Enable command-stream diagnostics. |
| `copy_gfx` | Select the graphics path for eligible copy operations. |
| `force_simultaneous` | Force simultaneous queue behavior for diagnostics. |
| `implicit_others_inv` | Enable the implicit-other-invocations behavior used for testing. |
| `force_blackhole` | Force the blackhole path for diagnostics. |
| `wsi_no_afbc` | Disable AFBC for WSI images. |
| `no_wb_mmap` | Disable write-back mmap behavior. |
| `no_user_mmap_sync` | Disable the userspace mmap synchronization path. |
| `cached_before_coherent` | Enable a cache/coherency diagnostic mode. |
| `no_extended_va_range` | Disable the extended GPU virtual-address range. |
| `hsr_prepass` | Force the HSR prepass on. |
| `no_crc` | Disable the CRC path where supported. |
| `kbase_diag` | Enable kbase diagnostics. |
| `gpu_prerast` | Enable the GPU pre-raster path/diagnostics. |
| `no_bc_emul` | Disable BC format emulation. |
| `no_gs` | Disable the geometry-shader path. |
| `robust_ssbo_vec` | Enable the robust SSBO vector diagnostic path. |
| `rt_test` | Enable experimental ray-tracing test paths; this does not provide general Vulkan RT/DXR support. |
| `bc_compute` | Force compute-shader BC decoding; also takes precedence over `bc_gfx`. |
| `bc_wide` | Use the wider-channel BC graphics decoder where available. |
| `bc_expansion` | Log each BC-emulated image's source/shadow formats, extent, mip/layer counts, decode path, shadow modifier, raw and decoded layout sizes, gap, row pitch, and total allocation size. |
| `heap_stats` | Log kbase tiler-heap renewal decisions. |
| `spoof_nvidia` | Report an NVIDIA RTX 3060 identity for compatibility experiments; does not add NVIDIA features. |
| `spoof_amd` | Report an AMD RX 6600 identity for compatibility experiments; does not add AMD features. If both spoof switches are present, AMD takes precedence. |
| `bc_gfx` | Opt in to fragment/graphics BC decoding for comparison. Compute decoding is the default. Pan v9, 3D images, and `no_gs` remain on compute; if both `bc_gfx` and `bc_compute` are set, compute wins. |

BC decoding options can affect image handling. `no_bc_emul` disables the
emulation path and can make BC resources unusable when the hardware path is
unavailable. Identity spoofing changes reported properties only; it does not
turn PanVK into an AMD or NVIDIA driver.

## PanVK BC, KRAID, and CSF options

| Variable | Values and default | Description |
|---|---|---|
| `PANVK_BC_SHADOW_AFBC` | `0` (default/automatic), `1`, `2` | Applies only to decoded BC shadow images. `1` requests AFBC when supported and otherwise falls back with a warning. `2` excludes AFBC when another supported modifier is available. |
| `PANVK_BC_AFRC` | `0` (default/off), `16`, `24`, `32` | Requests an AFRC coding-unit size for eligible non-cube RGBA8 BC shadows when supported. AFRC is lossy and has caused a freeze on the tested G925 setup; leave it off outside controlled testing. |
| `PAN_USE_KRAID` | `all` (default), `cs`, `fs`, `vs`, `internal` | Selects KRAID compiler stages: compute, fragment, vertex, and internal shaders. KRAID is used only on supported architecture v9+ builds. |
| `PAN_KRAID_TIMING` | `false` (default); set `1` to enable | Logs KRAID shader compile time and generated binary size. |
| `PANVK_HSR_PREPASS` | `true` (default); set `0` to disable | Enables/disables the architecture-supported HSR prepass. `PANVK_DEBUG=hsr_prepass` forces it on. |
| `PANVK_ZS_PRELOAD_ALWAYS` | `false` (default); set `1` to enable | Forces depth/stencil preload for debugging. |
| `PANVK_KBASE_HEAP_RENEW_INTERVAL` | `128` (default); `0` disables this trigger | Sets the tiler-submission interval for proactive kbase tiler-heap renewal. The work-based trigger remains active unless separately disabled. |
| `PANVK_KBASE_HEAP_RENEW_WORK` | `65536` (default); `0` disables this trigger | Sets the accumulated tiler-work threshold for proactive heap renewal. |
| `PANVK_KBASE_GPU_SEMAPHORE_WAITS` | `false` (default); set `1` to opt in | Enables in-ring GPU semaphore waits where supported. Keep disabled for normal use: this path has had CTS/device-loss regressions. |
| `PANVK_DESC_TRACEBUF_SIZE` | `2 MiB` (default) | Sets the descriptor trace buffer size in bytes when descriptor tracing is active. |
| `PANVK_CS_TRACEBUF_SIZE` | `2 MiB` (default) | Sets the CSF command-stream trace buffer size in bytes. The implementation requires a value greater than 512 KiB. |
| `PANVK_UTRACE_CLONE_MEM_SIZE` | `0xa00000` bytes (10 MiB, default) | Sets the CSF utrace cloned-memory pool size. Increase it only when utrace reports that the pool cannot satisfy an allocation; this reserves additional memory. |

## PanVK kbase and WSI options

| Variable | Values and default | Description |
|---|---|---|
| `PANVK_KBASE_DMA_HEAP` | `/dev/dma_heap/system` (default) | Selects the dma-heap device used for kbase buffer export. The path must exist and be accessible to the application. |
| `PANVK_KBASE_USER_CACHE_SYNC` | Unset (default); `0` selects kernel sync | With `0`, cached BO synchronization uses the kernel ioctl path rather than userspace cache maintenance. |
| `PANVK_KBASE_KCPU_SYNC` | Unset (default); set `1` to opt in | Attempts to create/use a CSF KCPU sync queue when the kernel supports it. |
| `PANVK_KBASE_IMPORT_CLEAR` | Unset; hexadecimal/integer mask | Clears the selected bits from kbase dma-buf import flags. Bring-up/debug option; changing flags can break imports. |
| `PANVK_KBASE_IMPORT_SET` | Unset; hexadecimal/integer mask | Sets the selected bits in kbase dma-buf import flags. Bring-up/debug option; changing flags can break imports. |
| `PANVK_KBASE_DRI3` | Unset; `raw`, `termux`, or another non-`0` value | X11/kbase WSI override. `raw` and `termux` request the raw-FD DRI3 transport; another nonzero value opts into the kbase dma-buf path without selecting that raw transport. |
| `WSI_X11_TERMUX` | Unset; any non-`0` value enables | Termux:X11 compatibility switch. When no explicit `PANVK_KBASE_DRI3` override is set, enables the raw-FD DRI3 path on kbase. X11/Termux only. |
| `MESA_VK_WSI_PRESENT_MODE` | Overridden to `fifo` on Kbase | Kbase devices force FIFO presentation until queued present-mode synchronization is validated; other backends honor this Mesa option as usual. |

## Shader compiler debug options

These are Mesa Panfrost compiler options, not PanVK-only Vulkan controls. They
are useful for shader compiler debugging and can produce substantial logs.

| Variable | Accepted comma-separated options | Description |
|---|---|---|
| `BIFROST_MESA_DEBUG` | `shaders`, `shaderdb`, `verbose`, `internal`, `nosched`, `nopsched`, `inorder`, `novalidate`, `noopt`, `noidvs`, `nosb`, `nopreload`, `spill`, `nossara`, `statsabs`, `statsfull`, `debuginfo` | Bifrost/Valhall compiler diagnostics and testing. Includes shader dumps/statistics, scheduling and validation toggles, and register-spill testing. Some options intentionally alter compiler behavior. |
| `MIDGARD_MESA_DEBUG` | `shaders`, `shaderdb`, `inorder`, `verbose`, `internal` | Midgard compiler shader dumps, statistics, and scheduling diagnostics. |

## Mesa shader and disk-cache options

On Android, Winlator runs the ICD as an untrusted app. Android denies that
process access to `vendor.mesa.*` properties; the driver therefore does not
probe that property namespace. Mesa still reads option values from the process
environment first, and from `debug.mesa.*` / `mesa.*` properties as fallbacks.
Set these variables in the Winlator container or game environment (before
launching it), not in a separate shell:

| Environment variable | Example | Corresponding debug option |
|---|---|---|
| `MESA_VK_TRACE` | `rmv` | Vulkan trace mode (`rmv` is the mode supported by this Mesa source). |
| `MESA_VK_TRACE_PER_SUBMIT` | `1` | Write Vulkan trace data per queue submission. |
| `MESA_VK_TRACE_FRAME` | `120` | Limit Vulkan tracing to the selected frame. |
| `MESA_VK_TRACE_TRIGGER` | `/path/to/trigger` | Trigger trace capture when the named file exists and is writable; Mesa removes it when tracing is triggered. |
| `PAN_CPU_TRACE` | `vk.csf,lib.kmod` | Enable Panfrost CPU trace categories; `vk.csf` and `lib.kmod` are useful PanVK diagnostics. |
| `PANVK_DEBUG` | `trace` | Enable PanVK trace diagnostics and synchronous submissions. Kbase allocates trace-buffer GPU addresses automatically; command-stream binaries and hardware trace records are decoded as on other backends. The decoder safely reports and skips optional framebuffer/texture fields whose GPU address is null rather than dereferencing address zero. Tracing can add substantial overhead. Unset it for normal play. |
| `GALLIUM_PRINT_OPTIONS` | `1` | Print Gallium option values for diagnostics. |

For example, to request PanVK's CSF trace plus Mesa Vulkan trace capture, add
these to the Winlator environment:

```sh
PANVK_DEBUG=trace
MESA_VK_TRACE=rmv
MESA_VK_TRACE_PER_SUBMIT=1
PAN_CPU_TRACE=vk.csf,lib.kmod
```

`MESA_VK_TRACE_PER_SUBMIT=1` and full PanVK tracing may generate large logs and
slow execution. Enable only the tracing needed for a focused capture. This
change does not grant the app SELinux permission to read `vendor.*`; it avoids
the denied reads and preserves configuration through environment variables.

| Variable | Values and default | Description |
|---|---|---|
| `MESA_SHADER_CACHE_DIR` | Unset (Mesa chooses its normal cache path) | Sets the shader-cache directory. It must be writable by the application. |
| `MESA_SHADER_CACHE_MAX_SIZE` | Mesa default; the launcher may use `2G` | Sets the maximum shader-cache size. Mesa accepts size strings such as `2G`. |
| `MESA_SHADER_CACHE_DISABLE` | Unset | Requests that Mesa disable its shader cache. PanVK's forced disk-cache creation in this build may bypass the normal opt-out; do not assume this variable disables that PanVK cache. |
| `MESA_GLSL_CACHE_DIR` | Deprecated alias for `MESA_SHADER_CACHE_DIR` | Used only when the newer variable is not set. |
| `MESA_GLSL_CACHE_MAX_SIZE` | Deprecated alias for `MESA_SHADER_CACHE_MAX_SIZE` | Mesa may print a deprecation warning; use the `MESA_SHADER_CACHE_*` name instead. |
| `MESA_GLSL_CACHE_DISABLE` | Deprecated alias for `MESA_SHADER_CACHE_DISABLE` | Mesa may print a deprecation warning; use the `MESA_SHADER_CACHE_*` name instead. |
| `MESA_DISK_CACHE_DATABASE_NUM_PARTS` | `50` (default) | Sets the number of parts in Mesa's disk-cache database. This is an advanced cache implementation setting. |
| `MESA_DISK_CACHE_DATABASE_EVICTION_SCORE_2X_PERIOD` | `2592000` seconds (30 days, default) | Sets the period used by Mesa's disk-cache database eviction scoring. Advanced cache setting; changing it can affect cache retention. |

## Example

For BC diagnostics without changing the decoder path:

```sh
PANVK_DEBUG=bc_expansion
```

To compare against the fragment BC path:

```sh
PANVK_DEBUG=bc_expansion,bc_gfx
```

For normal gameplay, leave experimental AFRC, GPU semaphore waits, compiler
debugging, and identity spoof options unset. The environment must be set on the
process that loads the Vulkan ICD; setting it in a separate shell does not
affect an already-running emulator or game.
