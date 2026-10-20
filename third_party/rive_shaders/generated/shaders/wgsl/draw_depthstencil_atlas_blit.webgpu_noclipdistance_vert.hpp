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

inline constexpr Shader draw_depthstencil_atlas_blit_webgpu_noclipdistance_vert = {
    .source = R"WGSL(
struct yi{v2_:array<vec4<u32>>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct ug{v2_:array<vec2<u32>>,}struct vg{v2_:array<vec4<f32>>,}struct _C{@builtin(position) _g:vec4<f32>,_X:f32,_V:array<f32,1>,_W:array<f32,1>,}struct zi{v2_:array<vec4<u32>>,}struct VertexOutput{@location(1) member:vec2<f32>,@location(4)@interpolate(flat,either) member_1:f32,@location(6)@interpolate(flat,either) member_2:f32,@location(0) member_3:vec4<f32>,@location(9) member_4:vec3<f32>,@builtin(position) _g:vec4<f32>,}@id(0) override qj:bool=true;@id(2) override sj:bool=true;@id(4) override uj:bool=true;@group(0)@binding(2) var<storage>KB:yi;@group(0)@binding(0) var<uniform>j:WB;var<private>_E:i32;var<private>LB_1:vec3<f32>;var<private>U2_:vec2<f32>;@group(0)@binding(3) var<storage>XC:ug;var<private>f4_:f32;var<private>S0_:f32;@group(0)@binding(4) var<storage>JB:vg;var<private>N0_:vec4<f32>;var<private>O0_:vec3<f32>;var<private>_D:_C=_C(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(5) var<storage>ED:zi;@group(3)@binding(9) var eb:sampler;fn _P(){var _k:u32;var _n:f32;var _h:f32;var _j:f32;var _l:bool;var _o:f32;let _f=LB_1;let _c=(bitcast<u32>(_f.z)&65535u);let _x=KB.v2_[((_c*4u)+2u)];let _p=_f.xy;let _s=bitcast<vec3<f32>>(_x.yzw);let _J=j.Ti;U2_=(((_p*_s.x)+_s.yz)*_J);let _a=XC.v2_[_c];let _m=(_a.x&15u);if qj{let _q=(_m==0u);if _q{_k=_a.y;}else{_k=_a.x;}let _G=_k;let _y=(_G>>bitcast<u32>(16i));let _N=j.z6_;if (_y==0u){_n=0f;}else{_n=unpack2x16float(((_y+1023u)*_N)).x;}let _B=_n;_h=_B;if _q{_h=-(_B);}let _O=_h;f4_=_O;}if sj{S0_=f32(((_a.x>>bitcast<u32>(4i))&15u));}if (_m==1u){N0_=unpack4x8unorm(_a.y);}else{let _r=(_c*8u);let _b=JB.v2_[_r];let _e=JB.v2_[(_r+1u)];let _t=((mat2x2<f32>(vec2<f32>(_b.x,_b.y),vec2<f32>(_b.z,_b.w))*_p)+_e.xy);let _v=((_e.w+(f32(_m)*0.125f))+(max(_e.z,0f)*0.00024414063f));if (_e.z<0f){_j=-(_v);}else{_j=_v;}let _S=_j;N0_=vec4<f32>(_t.x,_t.y,_S,(-0.75f-round((bitcast<f32>(_a.y)*255f))));}_l=uj;if uj{_l=((_a.x&2048u)!=0u);}let _R=_l;if _R{let _A=(_c*8u);let _d=JB.v2_[(_A+4u)];let _w=JB.v2_[(_A+5u)];let _u=((mat2x2<f32>(vec2<f32>(_d.x,_d.y),vec2<f32>(_d.z,_d.w))*_p)+_w.xy);_o=(1f+_w.z);if ((_a.x&4096u)!=0u){_o=(-1f-f32(((_a.x&24576u)>>bitcast<u32>(13u))));}let _F=_o;O0_=vec3<f32>(_u.x,_u.y,_F);}else{O0_=vec3<f32>(0f,0f,0f);}let _K=j.Sg;let _z=j.Tg;let _i=vec4<f32>(((_f.x*_K)-1f),((_f.y*_z)-sign(_z)),0f,1f);_D._g=vec4<f32>(_i.x,_i.y,((f32(((_x.x<<bitcast<u32>(8u))|255u))*0.000000059604645f)+0.000000029802322f),_i.w);return;}@vertex fn main(@builtin(vertex_index) _U:u32,@location(0) LB:vec3<f32>)->VertexOutput{_E=i32(_U);LB_1=LB;_P();let _Q=U2_;let _H=f4_;let _T=S0_;let _L=N0_;let _I=O0_;let _M=_D._g;return VertexOutput(_Q,_H,_T,_L,_I,_M);})WGSL",
    .usedOverrides = {{true, false, true, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_atlas_blit.webgpu_noclipdistance_vert",
};
} // namespace wgsl
