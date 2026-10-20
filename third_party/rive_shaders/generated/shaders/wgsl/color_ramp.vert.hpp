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

inline constexpr Shader color_ramp_vert = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _s{@builtin(position) _c:vec4<f32>,_I:f32,_H:array<f32,1>,_J:array<f32,1>,}struct VertexOutput{@location(0) member:vec4<f32>,@builtin(position) _c:vec4<f32>,}var<private>_l:i32;var<private>KC_1:vec4<u32>;@group(0)@binding(0) var<uniform>j:WB;var<private>z7_:vec4<f32>;var<private>_q:_s=_s(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());fn _F(){var _j:u32;var _d:f32;var _k:f32;var _f:f32;var _e:f32;var _g:f32;var _b:u32;let _n=_l;let _i=(_n>>bitcast<u32>(1i));let _m=(_i<=1i);if _m{let _D=KC_1[0u];_j=(_D&65535u);}else{let _y=KC_1[0u];_j=(_y>>bitcast<u32>(16i));}let _t=_j;let _o=(f32(_t)*0.000015258789f);let _p=select(1f,0f,((_n&1i)==0i));let _h=j.Fd;_d=_p;if (_h<0f){_d=(1f-_p);}let _u=_d;let _a=KC_1[1u];_f=_o;if (((_a&2147483648u)!=0u)&&(_i==0i)){if ((_a&536870912u)!=0u){_k=0f;}else{_k=(_o-0.001953125f);}let _E=_k;_f=_E;}let _r=_f;_g=_r;if (((_a&1073741824u)!=0u)&&(_i==3i)){if ((_a&536870912u)!=0u){_e=1f;}else{_e=(_r+0.001953125f);}let _C=_e;_g=_C;}let _A=_g;if _m{let _x=KC_1[2u];_b=_x;}else{let _G=KC_1[3u];_b=_G;}let _w=_b;z7_=(vec4<f32>(((vec4(_w)>>bitcast<vec4<u32>>(vec4<u32>(16u,8u,0u,24u)))&vec4<u32>(255u,255u,255u,255u)))*vec4<f32>(0.003921569f,0.003921569f,0.003921569f,0.003921569f));_q._c=vec4<f32>(((_A*2f)-1f),(((f32((_a&536870911u))+_u)*_h)-sign(_h)),0f,1f);return;}@vertex fn main(@builtin(vertex_index) _B:u32,@location(0) KC:vec4<u32>)->VertexOutput{_l=i32(_B);KC_1=KC;_F();let _v=z7_;let _z=_q._c;return VertexOutput(_v,_z);})WGSL",
    .usedOverrides = {{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "color_ramp.vert",
};
} // namespace wgsl
