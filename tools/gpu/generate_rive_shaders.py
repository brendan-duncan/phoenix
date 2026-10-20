"""Generates the WGSL shader headers the Rive renderer's WebGPU backend
includes, and copies them into third_party/rive_shaders.

It does what rive-runtime's renderer/src/shaders/Makefile does for the
"wgsl" target (and the minified exports the renderer core includes), without
GNU make:

  1. minify.py over every .glsl/.vert/.frag in the folder, in one batch
     (the renamed identifiers have to agree across files);
  2. glslangValidator to SPIR-V for every webgpu_* variant, with the defines
     and flags the Makefile's spirv_list_rule gives it, then spirv-opt with
     the same optimizer settings;
  3. naga --keep-coordinate-space from SPIR-V to WGSL;
  4. wgsl_to_header.py to embed each as a C++ header.

Usage:
  python tools/gpu/generate_rive_shaders.py --rive <rive-runtime checkout>

The tools are found on PATH or in the Vulkan SDK (VULKAN_SDK); ply 3.11,
which minify.py needs, is cloned into the work folder unless --ply names one.
"""

import argparse
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
import tempfile

PHOENIX = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The Makefile's input lists, as far as the WGSL outputs need them.
SPIRV_WEBGPU_INPUTS = [
    "spirv/tessellate.main",
    "spirv/render_atlas.vert",
    "spirv/render_atlas_fill.frag",
    "spirv/render_atlas_stroke.frag",
    "spirv/blit_texture_as_draw_filtered.main",
    "spirv/atomic_resolve_coalesced.main",
]
SPIRV_DRAW_ATOMIC_INPUTS = [
    "spirv/atomic_draw_image_mesh.main",
    "spirv/atomic_draw_image_rect.main",
    "spirv/atomic_draw_interior_triangles.main",
    "spirv/atomic_draw_atlas_blit.main",
    "spirv/atomic_draw_path.main",
    "spirv/atomic_resolve.main",
    "spirv/atomic_init.main",
]
SPIRV_DRAW_MSAA_INPUTS = [
    "spirv/draw_depthstencil_path.main",
    "spirv/draw_depthstencil_atlas_blit.main",
    "spirv/draw_depthstencil_image_mesh.main",
    "spirv/draw_depthstencil_triangles_nocolor.main",
]
WEBGPU_NOSSBO_NOCLIPDISTANCE_INPUTS = [
    "spirv/draw_depthstencil_path.main",
    "spirv/draw_depthstencil_atlas_blit.main",
]
WEBGPU_NOSSBO_INPUTS = WEBGPU_NOSSBO_NOCLIPDISTANCE_INPUTS + [
    "spirv/tessellate.main",
    "spirv/render_atlas.vert",
]
SPIRV_COMMON_WEBGPU_PARAMS = ["-DTARGET_WGSL", "-DUSE_WEBGPU_SAMPLERS"]

# The standard (non-webgpu) variants the Makefile also turns into WGSL.
STANDARD_WGSL = [
    ("spirv/draw_depthstencil_triangles_nocolor.main", "vert"),
    ("spirv/draw_depthstencil_triangles_nocolor.main", "frag"),
    ("spirv/color_ramp.main", "vert"),
    ("spirv/color_ramp.main", "frag"),
]

# spirv_frag_opt_params for atomic shaders that are not webgpu ones.
ATOMIC_FRAG_OPT = [
    "--preserve-bindings", "--preserve-interface", "--wrap-opkill", "--simplify-instructions",
    "--eliminate-dead-branches", "--merge-return", "--inline-entry-points-exhaustive",
    "--eliminate-dead-inserts", "--eliminate-dead-members", "--merge-blocks", "--redundancy-elimination",
    "--cfg-cleanup", "--eliminate-dead-const", "--eliminate-dead-variables", "--eliminate-dead-functions",
    "--eliminate-dead-code-aggressive",
]
# spirv_frag_opt_params for every other fragment shader that is not a webgpu one.
STANDARD_FRAG_OPT = [
    "--wrap-opkill", "--eliminate-dead-branches", "--merge-return", "--inline-entry-points-exhaustive",
    "--eliminate-dead-functions", "--eliminate-dead-code-aggressive", "--private-to-local",
    "--eliminate-local-single-block", "--eliminate-local-single-store", "--eliminate-dead-code-aggressive",
    "--scalar-replacement=100", "--convert-local-access-chains", "--eliminate-local-single-block",
    "--eliminate-local-single-store", "--eliminate-dead-code-aggressive", "--ssa-rewrite",
    "--eliminate-dead-code-aggressive", "--ccp", "--eliminate-dead-code-aggressive", "--loop-unroll",
    "--eliminate-dead-branches", "--redundancy-elimination", "--combine-access-chains",
    "--scalar-replacement=100", "--convert-local-access-chains", "--eliminate-local-single-block",
    "--eliminate-local-single-store", "--eliminate-dead-code-aggressive", "--ssa-rewrite",
    "--eliminate-dead-code-aggressive", "--vector-dce", "--eliminate-dead-inserts", "--eliminate-dead-branches",
    "--if-conversion", "--copy-propagate-arrays", "--reduce-load-size", "--eliminate-dead-code-aggressive",
    "--merge-blocks", "--redundancy-elimination", "--eliminate-dead-branches", "--merge-blocks",
]


def variants():
    """(input, type, extra params) for every SPIR-V the WGSL outputs come
    from, as the Makefile's make_spirv_rules calls make them."""
    result = []

    def rules(files, types, extra):
        for kind in types:
            skip = ".frag" if "vert" in kind else ".vert"
            for f in files:
                if not f.endswith(skip):
                    result.append((f, kind, extra))

    rules(SPIRV_WEBGPU_INPUTS + SPIRV_DRAW_ATOMIC_INPUTS + SPIRV_DRAW_MSAA_INPUTS, ["webgpu_vert"],
          SPIRV_COMMON_WEBGPU_PARAMS)
    rules(SPIRV_DRAW_MSAA_INPUTS, ["webgpu_noclipdistance_vert"],
          SPIRV_COMMON_WEBGPU_PARAMS + ["-DDISABLE_CLIP_DISTANCE_FOR_UBERSHADERS"])
    rules(WEBGPU_NOSSBO_INPUTS, ["webgpu_nossbo_vert"],
          SPIRV_COMMON_WEBGPU_PARAMS + ["-DDISABLE_SHADER_STORAGE_BUFFERS"])
    rules(WEBGPU_NOSSBO_NOCLIPDISTANCE_INPUTS, ["webgpu_nossbo_noclipdistance_vert"],
          SPIRV_COMMON_WEBGPU_PARAMS + ["-DDISABLE_SHADER_STORAGE_BUFFERS", "-DDISABLE_CLIP_DISTANCE_FOR_UBERSHADERS"])
    rules(SPIRV_WEBGPU_INPUTS + SPIRV_DRAW_ATOMIC_INPUTS + SPIRV_DRAW_MSAA_INPUTS, ["webgpu_frag"],
          SPIRV_COMMON_WEBGPU_PARAMS)
    rules(SPIRV_DRAW_ATOMIC_INPUTS + SPIRV_DRAW_MSAA_INPUTS, ["webgpu_fixedcolor_frag"],
          SPIRV_COMMON_WEBGPU_PARAMS + ["-DFIXED_FUNCTION_COLOR_OUTPUT"])
    for f, kind in STANDARD_WGSL:
        result.append((f, kind, []))
    # The same output can be listed twice (as make would merge it); keep one.
    seen = set()
    unique = []
    for v in result:
        key = (v[0], v[1])
        if key not in seen:
            seen.add(key)
            unique.append(v)
    return unique


def find_tool(name, given):
    if given:
        return given
    found = shutil.which(name)
    if found:
        return found
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        for folder in ("Bin", "bin"):
            candidate = os.path.join(sdk, folder, name + (".exe" if os.name == "nt" else ""))
            if os.path.exists(candidate):
                return candidate
    sys.exit(f"{name} not found; put it on PATH or pass it")


def run(command, cwd=None):
    done = subprocess.run(command, cwd=cwd, capture_output=True, text=True, env=dict(os.environ, TERM="dumb"))
    output = "\n".join(line for line in (done.stdout + done.stderr).splitlines()
                       if "Unknown decoration RelaxedPrecision" not in line)
    if done.returncode != 0:
        raise RuntimeError(f"{' '.join(command)}\n{output}")
    return output


def build(variant, shaders, out, tools):
    source, kind, extra = variant
    base = os.path.splitext(os.path.basename(source))[0]
    name = f"{base}.{kind}"
    stage = kind.split("_")[-1]
    is_vert = "vert" in stage
    is_webgpu = "webgpu" in kind
    is_clockwise = "clockwise" in source and "clockwise_atomic" not in source
    is_atomic = "atomic" in source
    spirv_dir = os.path.join(out, "spirv")
    wgsl_dir = os.path.join(out, "wgsl")
    unoptimized = os.path.join(spirv_dir, name + ".spv.unoptimized")
    spv = os.path.join(spirv_dir, name + ".spv")
    if is_vert:
        stage_params = ["-DVERTEX"]
        opt = ["-O"]
    else:
        stage_params = ["-DFRAGMENT", "-DPLS_IMPL_STORAGE_BUFFER" if is_webgpu
                        else "-DPLS_IMPL_STORAGE_TEXTURE" if is_clockwise else "-DPLS_IMPL_SUBPASS_LOAD"]
        opt = ["-O"] if is_webgpu else ATOMIC_FRAG_OPT if is_atomic else STANDARD_FRAG_OPT
    run([tools["glslangValidator"], "-S", stage, "-DTARGET_SPIRV", *stage_params, "-I" + out, "-V", *extra,
         "-o", unoptimized, source], cwd=shaders)
    run([tools["spirv-opt"], "--preserve-bindings", "--preserve-interface", *opt, unoptimized, "-o", spv])
    os.remove(unoptimized)
    wgsl = os.path.join(wgsl_dir, name + ".wgsl")
    noise = run([tools["naga"], "--keep-coordinate-space", spv, wgsl])
    hpp = os.path.join(wgsl_dir, name + ".hpp")
    run([sys.executable, os.path.join(shaders, "wgsl_to_header.py"), wgsl, hpp, name.replace(".", "_")])
    return name, noise


def copy_lf(source, destination):
    with open(source, "r", encoding="utf-8", newline="") as f:
        text = f.read().replace("\r\n", "\n")
    with open(destination, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rive", required=True, help="a rive-runtime checkout")
    parser.add_argument("--dest", default=os.path.join(PHOENIX, "third_party", "rive_shaders", "generated", "shaders"))
    parser.add_argument("--work", help="the folder to build in (a temporary one when not given)")
    parser.add_argument("--ply", help="a checkout of ply 3.11")
    parser.add_argument("--glslang", help="glslangValidator")
    parser.add_argument("--spirv-opt", help="spirv-opt")
    parser.add_argument("--naga", help="naga (naga-cli)")
    args = parser.parse_args()

    shaders = os.path.join(os.path.abspath(args.rive), "renderer", "src", "shaders")
    work = os.path.abspath(args.work) if args.work else tempfile.mkdtemp(prefix="rive_shaders_")
    out = os.path.join(work, "generated", "shaders")
    for folder in (out, os.path.join(out, "spirv"), os.path.join(out, "wgsl")):
        os.makedirs(folder, exist_ok=True)
    tools = {
        "glslangValidator": find_tool("glslangValidator", args.glslang),
        "spirv-opt": find_tool("spirv-opt", args.spirv_opt),
        "naga": find_tool("naga", args.naga),
    }
    print(run([tools["naga"], "--version"]).strip())

    ply = args.ply
    if not ply:
        ply = os.path.join(work, "ply")
        if not os.path.exists(ply):
            subprocess.run(["git", "clone", "--depth", "1", "--branch", "3.11", "https://github.com/dabeaz/ply", ply],
                           check=True)

    # 1. Minify, all in one batch.
    inputs = sorted(os.path.basename(p) for pattern in ("*.glsl", "*.vert", "*.frag")
                    for p in glob.glob(os.path.join(shaders, pattern)))
    run([sys.executable, "minify.py", "-p", ply, "-o", out, *inputs], cwd=shaders)

    # 2-4. SPIR-V, WGSL, headers.
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        jobs = [pool.submit(build, v, shaders, out, tools) for v in variants()]
        for job in jobs:
            try:
                name, noise = job.result()
                print("wgsl/" + name + ".wgsl")
                if noise.strip():
                    print(noise)
            except RuntimeError as error:
                failed = True
                print(error, file=sys.stderr)
    if failed:
        sys.exit(1)

    # The headers, and the exports of the minified sources the renderer
    # core includes.
    dest = os.path.abspath(args.dest)
    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.makedirs(os.path.join(dest, "wgsl"))
    count = 0
    for hpp in sorted(glob.glob(os.path.join(out, "wgsl", "*.hpp"))):
        copy_lf(hpp, os.path.join(dest, "wgsl", os.path.basename(hpp)))
        count += 1
    for exports in sorted(glob.glob(os.path.join(out, "*.exports.h"))):
        copy_lf(exports, os.path.join(dest, os.path.basename(exports)))
        count += 1
    print(f"{count} headers in {dest}")


if __name__ == "__main__":
    main()
