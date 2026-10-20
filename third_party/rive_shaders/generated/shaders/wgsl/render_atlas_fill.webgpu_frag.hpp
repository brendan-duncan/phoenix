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

inline constexpr Shader render_atlas_fill_webgpu_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}@group(0)@binding(9) var AD:texture_2d<f32>;@group(3)@binding(9) var eb:sampler;var<private>Yi:f32;var<private>T_1:vec4<f32>;var<private>_h:bool;@group(0)@binding(0) var<uniform>j:WB;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(1)@binding(11) var RB:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;@group(1)@binding(13) var v4_:sampler;fn _q(){var _f:f32;var _c:f32;var _d:f32;let _a=T_1;let _p=_h;let _e=max(_a.w,0f);if (_a.z>=0f){let _u=textureSampleLevel(AD,eb,vec2<f32>(_e,0f),0f);_f=_u.x;}else{_f=0f;}let _i=_f;_c=_i;if (abs(_a.z)<1000f){let _l=(-2f-_a.y);let _j=((_l-_e)*0.5984134f);let _k=(vec4(_e)+(vec4<f32>(0.20888568f,0.62665707f,1.0444285f,1.4621998f)*_j));let _b=((_k*-(_a.z))+vec4(((_l*_a.z)+(abs(_a.x)-0.25f))));let _x=textureSampleLevel(AD,eb,vec2<f32>(_b.x,0f),0f);let _n=textureSampleLevel(AD,eb,vec2<f32>(_b.y,0f),0f);let _s=textureSampleLevel(AD,eb,vec2<f32>(_b.z,0f),0f);let _v=textureSampleLevel(AD,eb,vec2<f32>(_b.w,0f),0f);let _m=(_k*5.0959306f);_c=(_i+(dot(vec4<f32>(_x.x,_n.x,_s.x,_v.x),exp2(((vec4<f32>(2.5479653f,2.5479653f,2.5479653f,2.5479653f)-_m)*(_m+vec4<f32>(-2.5479653f,-2.5479653f,-2.5479653f,-2.5479653f)))))*_j));}let _r=_c;let _g=(_r*sign(_a.x));_d=_g;if!(_p){_d=-(_g);}let _o=_d;Yi=_o;return;}@fragment fn main(@location(0) T:vec4<f32>,@builtin(front_facing) _w:bool)->@location(0) f32{T_1=T;_h=_w;_q();let _t=Yi;return _t;})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "render_atlas_fill.webgpu_frag",
};
} // namespace wgsl
