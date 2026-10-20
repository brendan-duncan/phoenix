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

inline constexpr Shader atomic_draw_interior_triangles_webgpu_vert = {
    .source = R"WGSL(
struct yi{v2_:array<vec4<u32>>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _d{@builtin(position) _b:vec4<f32>,_u:f32,_v:array<f32,1>,_w:array<f32,1>,}struct ug{v2_:array<vec2<u32>>,}struct vg{v2_:array<vec4<f32>>,}struct zi{v2_:array<vec4<u32>>,}struct VertexOutput{@location(0)@interpolate(flat,either) member:f32,@location(1)@interpolate(flat,either) member_1:u32,@builtin(position) _b:vec4<f32>,}@group(0)@binding(2) var<storage>KB:yi;var<private>_j:i32;var<private>_n:i32;var<private>LB_1:vec3<f32>;var<private>p1_:f32;var<private>I0_:u32;@group(0)@binding(0) var<uniform>j:WB;var<private>_g:_d=_d(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(3) var<storage>XC:ug;@group(0)@binding(4) var<storage>JB:vg;@group(0)@binding(5) var<storage>ED:zi;@group(3)@binding(9) var eb:sampler;fn _k(){let _c=LB_1;let _f=(bitcast<u32>(_c.z)&65535u);let _e=(_f*4u);let _s=KB.v2_[_e];let _a=bitcast<vec4<f32>>(_s);let _m=KB.v2_[(_e+1u)];let _h=((mat2x2<f32>(vec2<f32>(_a.x,_a.y),vec2<f32>(_a.z,_a.w))*_c.xy)+bitcast<vec2<f32>>(_m.xy));p1_=f32((bitcast<i32>(_c.z)>>bitcast<u32>(16i)));I0_=_f;let _o=j.Sg;let _i=j.Tg;_g._b=vec4<f32>(((_h.x*_o)-1f),((_h.y*_i)-sign(_i)),0f,1f);return;}@vertex fn main(@builtin(vertex_index) _t:u32,@builtin(instance_index) _q:u32,@location(0) LB:vec3<f32>)->VertexOutput{_j=i32(_t);_n=i32(_q);LB_1=LB;_k();let _p=p1_;let _l=I0_;let _r=_g._b;return VertexOutput(_p,_l,_r);})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "atomic_draw_interior_triangles.webgpu_vert",
};
} // namespace wgsl
