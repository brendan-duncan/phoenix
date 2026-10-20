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

inline constexpr Shader blit_texture_as_draw_filtered_webgpu_vert = {
    .source = R"WGSL(
struct _e{@builtin(position) _a:vec4<f32>,_l:f32,_n:array<f32,1>,_m:array<f32,1>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct VertexOutput{@location(0) member:vec2<f32>,@builtin(position) _a:vec4<f32>,}var<private>_f:i32;var<private>n2_:vec2<f32>;var<private>_b:_e=_e(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());@group(0)@binding(0) var<uniform>j:WB;fn _i(){let _g=_f;let _c=select(1f,-1f,((_g&1i)==0i));let _d=select(1f,-1f,((_g&2i)==0i));n2_[0u]=((_c*0.5f)+0.5f);n2_[1u]=((_d*-0.5f)+0.5f);_b._a=vec4<f32>(_c,_d,0f,1f);return;}@vertex fn main(@builtin(vertex_index) _h:u32)->VertexOutput{_f=i32(_h);_i();let _j=n2_;let _k=_b._a;return VertexOutput(_j,_k);})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "blit_texture_as_draw_filtered.webgpu_vert",
};
} // namespace wgsl
