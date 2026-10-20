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

inline constexpr Shader render_atlas_stroke_webgpu_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}@group(0)@binding(9) var AD:texture_2d<f32>;@group(3)@binding(9) var eb:sampler;var<private>Yi:f32;var<private>T_1:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(1)@binding(11) var RB:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;@group(1)@binding(13) var v4_:sampler;fn _b(){let _a=T_1;let _c=textureSampleLevel(AD,eb,vec2<f32>((3f+_a.x),0f),0f);let _e=textureSampleLevel(AD,eb,vec2<f32>((1f-_a.y),0f),0f);Yi=((1f-_c.x)-_e.x);return;}@fragment fn main(@location(0) T:vec4<f32>)->@location(0) f32{T_1=T;_b();let _d=Yi;return _d;})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "render_atlas_stroke.webgpu_frag",
};
} // namespace wgsl
