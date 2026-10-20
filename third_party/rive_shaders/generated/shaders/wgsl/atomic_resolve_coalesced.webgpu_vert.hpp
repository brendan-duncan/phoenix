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

inline constexpr Shader atomic_resolve_coalesced_webgpu_vert = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _d{@builtin(position) _g:vec4<f32>,_w:f32,_x:array<f32,1>,_v:array<f32,1>,}struct yi{v2_:array<vec4<u32>>,}struct ug{v2_:array<vec2<u32>>,}struct vg{v2_:array<vec4<f32>>,}struct zi{v2_:array<vec4<u32>>,}var<private>_f:i32;var<private>_j:i32;@group(0)@binding(0) var<uniform>j:WB;var<private>_h:_d=_d(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(2) var<storage>KB:yi;@group(0)@binding(3) var<storage>XC:ug;@group(0)@binding(4) var<storage>JB:vg;@group(0)@binding(5) var<storage>ED:zi;@group(3)@binding(9) var eb:sampler;fn _m(){var _a:i32;var _b:i32;let _i=_f;if ((_i&1i)==0i){let _r=j.J8_[0u];_a=_r;}else{let _n=j.J8_[2u];_a=_n;}let _u=_a;if ((_i&2i)==0i){let _o=j.J8_[1u];_b=_o;}else{let _t=j.J8_[3u];_b=_t;}let _l=_b;let _c=vec2<f32>(vec2<i32>(_u,_l));let _k=j.Sg;let _e=j.Tg;_h._g=vec4<f32>(((_c.x*_k)-1f),((_c.y*_e)-sign(_e)),0f,1f);return;}@vertex fn main(@builtin(vertex_index) _s:u32,@builtin(instance_index) _p:u32)->@builtin(position) vec4<f32>{_f=i32(_s);_j=i32(_p);_m();let _q=_h._g;return _q;})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "atomic_resolve_coalesced.webgpu_vert",
};
} // namespace wgsl
