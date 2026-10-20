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

inline constexpr Shader blit_texture_as_draw_filtered_webgpu_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}@group(1)@binding(11) var JC:texture_2d<f32>;@group(1)@binding(13) var Pg:sampler;var<private>n2_1:vec2<f32>;var<private>Yi:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;fn _a(){let _d=n2_1;let _b=textureSampleLevel(JC,Pg,_d,0f);Yi=_b;return;}@fragment fn main(@location(0) n2_:vec2<f32>)->@location(0) vec4<f32>{n2_1=n2_;_a();let _c=Yi;return _c;})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "blit_texture_as_draw_filtered.webgpu_frag",
};
} // namespace wgsl
