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

inline constexpr Shader draw_depthstencil_image_mesh_webgpu_fixedcolor_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}@id(8) override yj:bool=true;@group(1)@binding(11) var RB:texture_2d<f32>;@group(1)@binding(13) var v4_:sampler;var<private>S5_1:vec2<f32>;@group(0)@binding(0) var<uniform>j:WB;var<private>Y1_1:vec4<f32>;var<private>_f:vec4<f32>;var<private>Yi:vec4<f32>;var<private>f4_1:f32;var<private>N1_1:u32;fn _m(){var _c:vec3<f32>;let _q=S5_1;let _i=j.De;let _n=textureSampleBias(RB,v4_,_q,_i);let _k=Y1_1;let _a=(_n*_k);let _g=_a.xyz;let _h=_f;let _p=j.H3_;let _j=j.I3_;if (yj&&(_a.w!=0f)){_c=(vec3(((fract((52.982918f*fract(((0.06711056f*_h.x)+(0.00583715f*_h.y)))))*_p)+_j))+_g);}else{_c=_g;}let _b=_c;let _d=vec4<f32>(_b.x,_a.y,_a.z,_a.w);let _e=vec4<f32>(_d.x,_b.y,_d.z,_d.w);Yi=vec4<f32>(_e.x,_e.y,_b.z,_e.w);return;}@fragment fn main(@location(0) S5_:vec2<f32>,@location(3)@interpolate(flat,either) Y1_:vec4<f32>,@builtin(position) _l:vec4<f32>,@location(1)@interpolate(flat,either) f4_:f32,@location(4)@interpolate(flat,either) N1_:u32)->@location(0) vec4<f32>{S5_1=S5_;Y1_1=Y1_;_f=_l;f4_1=f4_;N1_1=N1_;_m();let _o=Yi;return _o;})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_image_mesh.webgpu_fixedcolor_frag",
};
} // namespace wgsl
