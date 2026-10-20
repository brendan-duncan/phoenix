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

inline constexpr Shader draw_depthstencil_triangles_nocolor_webgpu_noclipdistance_vert = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _d{@builtin(position) _f:vec4<f32>,_n:f32,_m:array<f32,1>,_o:array<f32,1>,}var<private>_g:i32;var<private>LB_1:vec3<f32>;@group(0)@binding(0) var<uniform>j:WB;var<private>_e:_d=_d(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());fn _l(){let _c=LB_1;let _i=j.Sg;let _b=j.Tg;let _a=vec4<f32>(((_c.x*_i)-1f),((_c.y*_b)-sign(_b)),0f,1f);let _h=LB_1[2u];_e._f=vec4<f32>(_a.x,_a.y,((f32((((bitcast<u32>(_h)&65535u)<<bitcast<u32>(8u))|255u))*0.000000059604645f)+0.000000029802322f),_a.w);return;}@vertex fn main(@builtin(vertex_index) _j:u32,@location(0) LB:vec3<f32>)->@builtin(position) vec4<f32>{_g=i32(_j);LB_1=LB;_l();let _k=_e._f;return _k;})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_triangles_nocolor.webgpu_noclipdistance_vert",
};
} // namespace wgsl
