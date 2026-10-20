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

inline constexpr Shader atomic_draw_atlas_blit_webgpu_vert = {
    .source = R"WGSL(
struct yi{v2_:array<vec4<u32>>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _d{@builtin(position) _b:vec4<f32>,_s:f32,_t:array<f32,1>,_u:array<f32,1>,}struct ug{v2_:array<vec2<u32>>,}struct vg{v2_:array<vec4<f32>>,}struct zi{v2_:array<vec4<u32>>,}struct VertexOutput{@location(0) member:vec2<f32>,@location(1)@interpolate(flat,either) member_1:u32,@builtin(position) _b:vec4<f32>,}@group(0)@binding(2) var<storage>KB:yi;@group(0)@binding(0) var<uniform>j:WB;var<private>_m:i32;var<private>_p:i32;var<private>LB_1:vec3<f32>;var<private>U2_:vec2<f32>;var<private>I0_:u32;var<private>_e:_d=_d(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(3) var<storage>XC:ug;@group(0)@binding(4) var<storage>JB:vg;@group(0)@binding(5) var<storage>ED:zi;@group(3)@binding(9) var eb:sampler;fn _j(){let _a=LB_1;let _f=(bitcast<u32>(_a.z)&65535u);let _o=KB.v2_[((_f*4u)+2u)];let _g=bitcast<vec3<f32>>(_o.yzw);let _r=j.Ti;U2_=(((_a.xy*_g.x)+_g.yz)*_r);I0_=_f;let _h=j.Sg;let _c=j.Tg;_e._b=vec4<f32>(((_a.x*_h)-1f),((_a.y*_c)-sign(_c)),0f,1f);return;}@vertex fn main(@builtin(vertex_index) _n:u32,@builtin(instance_index) _i:u32,@location(0) LB:vec3<f32>)->VertexOutput{_m=i32(_n);_p=i32(_i);LB_1=LB;_j();let _k=U2_;let _l=I0_;let _q=_e._b;return VertexOutput(_k,_l,_q);})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "atomic_draw_atlas_blit.webgpu_vert",
};
} // namespace wgsl
