# Rive renderer shaders (generated)

The WGSL shaders the Rive renderer's WebGPU backend compiles at run time,
embedded as C++ headers, plus the `*.exports.h` name maps of its minified
GLSL that the renderer core includes. The Rive build makes these with GNU
make, glslangValidator, spirv-opt and naga; they are generated once and kept
here so building Phoenix with `-DPHOENIX_GPU=ON` needs none of those tools.

- rive-runtime commit: `d278866b98ed71118da6cf4126751f2a92ccf70c`
  (the same one `cmake/gpu.cmake` fetches; regenerate when that changes)
- glslangValidator and spirv-opt: Vulkan SDK 1.4.304.0
- naga: naga-cli 30.0.1 (`cargo install naga-cli --version 30.0.1 --locked`)
- ply 3.11, for Rive's `minify.py`
- Python 3.12

Made with:

```
python tools/gpu/generate_rive_shaders.py --rive <rive-runtime checkout at that commit>
```

The script follows `renderer/src/shaders/Makefile`'s `wgsl` target: one
batch of `minify.py` over the GLSL sources, each `webgpu_*` variant (and the
four standard ones the Makefile also translates) through glslangValidator
with the Makefile's defines and spirv-opt with its settings, then
`naga --keep-coordinate-space` and `wgsl_to_header.py`. It replaces
`generated/shaders/` here.

Layout: `generated/shaders/wgsl/*.hpp` (56 shaders) and
`generated/shaders/*.exports.h` (36), included by the Rive sources as
`generated/shaders/...` with this folder on the include path. They are
derived from rive-runtime, which is MIT licensed.
