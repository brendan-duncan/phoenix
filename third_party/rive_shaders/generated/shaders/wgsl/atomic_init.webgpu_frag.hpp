#pragma once

#include <array>

#include "shaders/constants.glsl"

namespace rive::gpu::wgsl
{
#ifndef RIVE_WGSL_SHADER_DEFINED
#define RIVE_WGSL_SHADER_DEFINED
struct Shader
{
    const char* source;
    std::array<bool, SPECIALIZATION_COUNT> usedOverrides;
    const char* label;
};
#endif

inline constexpr Shader atomic_init_webgpu_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct n0sf{v2_:array<u32>,}struct W4sf{v2_:array<u32>,}struct m0sf{v2_:array<u32>,}struct ug{v2_:array<vec2<u32>>,}struct vg{v2_:array<vec4<f32>>,}@id(13) override Dj:bool=false;@id(14) override Ej:bool=false;@id(0) override qj:bool=true;var<private>_d:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;@group(2)@binding(0) var<storage,read_write>n0_:n0sf;@group(1)@binding(11) var RB:texture_2d<f32>;@group(2)@binding(3) var<storage,read_write>W4_:W4sf;@group(2)@binding(1) var<storage,read_write>m0_:m0sf;@group(3)@binding(9) var eb:sampler;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;@group(1)@binding(13) var v4_:sampler;@group(0)@binding(3) var<storage>XC:ug;@group(0)@binding(4) var<storage>JB:vg;fn _j(){let _k=_d;let _c=vec2<i32>(floor(_k.xy));let _a=bitcast<vec2<u32>>(_c);let _i=j.W6_;let _b=bitcast<i32>((((((_a.y>>bitcast<u32>(5u))*(((_i+31u)&4294967264u)<<bitcast<u32>(5u)))+((_a.x>>bitcast<u32>(5u))<<bitcast<u32>(10u)))+(((_a.x&28u)<<bitcast<u32>(5u))+((_a.y&28u)<<bitcast<u32>(2i))))+(((_a.y&3u)<<bitcast<u32>(2i))+(_a.x&3u))));if Dj{let _g=j.Eg;n0_.v2_[_b]=pack4x8unorm(unpack4x8unorm(_g));}if Ej{let _h=textureLoad(RB,_c,0i);n0_.v2_[_b]=pack4x8unorm(_h);}let _e=j.Fg;W4_.v2_[_b]=_e;if qj{m0_.v2_[_b]=0u;}return;}@fragment fn main(@builtin(position) _f:vec4<f32>){_d=_f;_j();})WGSL",
    .usedOverrides = {{true, false, false, false, false, false, false, false, false, false, false, false, false, true, true, false, false, false}},
    .label = "atomic_init.webgpu_frag",
};
} // namespace wgsl
