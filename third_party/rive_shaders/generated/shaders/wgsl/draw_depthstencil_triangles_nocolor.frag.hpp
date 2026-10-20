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

inline constexpr Shader draw_depthstencil_triangles_nocolor_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}var<private>Yi:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;fn _d(){let _b=vec4<f32>(0f,vec4<f32>().y,vec4<f32>().z,vec4<f32>().w);let _c=vec4<f32>(_b.x,0f,_b.z,_b.w);let _a=vec4<f32>(_c.x,_c.y,0f,_c.w);Yi=vec4<f32>(_a.x,_a.y,_a.z,0f);return;}@fragment fn main()->@location(0) vec4<f32>{_d();let _e=Yi;return _e;})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_triangles_nocolor.frag",
};
} // namespace wgsl
