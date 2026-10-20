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

inline constexpr Shader draw_depthstencil_atlas_blit_webgpu_fixedcolor_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}@id(8) override yj:bool=true;@id(9) override zj:bool=true;@id(2) override sj:bool=true;@id(4) override uj:bool=true;@group(0)@binding(0) var<uniform>j:WB;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;@group(1)@binding(11) var RB:texture_2d<f32>;@group(1)@binding(13) var v4_:sampler;@group(0)@binding(10) var JD:texture_2d<f32>;@group(3)@binding(10) var Ta:sampler;var<private>U2_1:vec2<f32>;var<private>O0_1:vec3<f32>;var<private>S0_1:f32;var<private>N0_1:vec4<f32>;var<private>_A:vec4<f32>;var<private>Yi:vec4<f32>;@group(3)@binding(9) var eb:sampler;@group(0)@binding(9) var AD:texture_2d<f32>;var<private>f4_1:f32;fn _S(){var _m:f32;var _r:f32;var _g:vec2<f32>;var _l:vec4<f32>;var _h:vec4<f32>;var _k:bool;var _s:bool;var _u:f32;var _t:vec4<f32>;var _f:vec4<f32>;var _n:vec4<f32>;var _o:vec3<f32>;let _T=U2_1;let _P=textureSampleLevel(JD,Ta,_T,0f);let _N=S0_1;let _b=O0_1;let _a=N0_1;switch bitcast<i32>(0u){default:{let _E=(sj&&(u32(_N)!=0u));if (_a.w>=0f){_h=_a;}else{let _J=j.Q8_;let _K=j.R8_;let _B=abs(_a.z);let _x=floor(_B);let _y=(((_B-_x)*8f)+0.0009765625f);let _C=((_x*_J)+_K);let _F=floor(_y);switch bitcast<i32>(0u){default:{if (zj&&(_F==4f)){let _D=normalize(vec2<f32>(_a.x,-(_a.y)));let _G=acos(clamp(_D.x,-1f,1f));if (_D.y>=0f){_r=_G;}else{_r=-(_G);}let _U=_r;_g=vec2<f32>((_U*0.15915494f),_C);break;}else{if (_F==2f){_m=_a.x;}else{_m=length(_a.xy);}let _2=_m;_g=vec2<f32>(((clamp(_2,0f,1f)*select(0.001953125f,0.9980469f,(_a.z<0f)))+_y),_C);break;}}}let _1=_g;let _e=textureSampleLevel(ZC,S8_,_1,0f);_l=_e;if!(_E){let _i=(_e.xyz*_e.w);_l=vec4<f32>(_i.x,_i.y,_i.z,(_e.w*(ceil(_a.w)*-0.003921569f)));}let _W=_l;_h=_W;}let _w=_h;_k=uj;if uj{_k=(_b.z<0f);}let _Z=_k;if _Z{let _L=textureSampleLevel(RB,v4_,_b.xy,0f);_n=_L;break;}_s=uj;if uj{_s=(_b.z>0f);}let _R=_s;_f=_w;if _R{let _d=textureSampleLevel(RB,v4_,_b.xy,(_b.z-1f));_t=_d;if _E{if (_d.w!=0f){_u=(1f/_d.w);}else{_u=0f;}let _V=_u;let _j=(_d.xyz*_V);_t=vec4<f32>(_j.x,_j.y,_j.z,_d.w);}let _Y=_t;_f=(_w*_Y);}let _M=_f;_n=_M;break;}}let _I=_n;let _c=(_I*clamp(_P.x,0f,1f));let _H=_c.xyz;let _z=_A;let _Q=j.H3_;let _X=j.I3_;if (yj&&(_c.w!=0f)){_o=(vec3(((fract((52.982918f*fract(((0.06711056f*_z.x)+(0.00583715f*_z.y)))))*_Q)+_X))+_H);}else{_o=_H;}let _q=_o;let _v=vec4<f32>(_q.x,_c.y,_c.z,_c.w);let _p=vec4<f32>(_v.x,_q.y,_v.z,_v.w);Yi=vec4<f32>(_p.x,_p.y,_q.z,_p.w);return;}@fragment fn main(@location(1) U2_:vec2<f32>,@location(9) O0_:vec3<f32>,@location(6)@interpolate(flat,either) S0_:f32,@location(0) N0_:vec4<f32>,@builtin(position) _0:vec4<f32>,@location(4)@interpolate(flat,either) f4_:f32)->@location(0) vec4<f32>{U2_1=U2_;O0_1=O0_;S0_1=S0_;N0_1=N0_;_A=_0;f4_1=f4_;_S();let _O=Yi;return _O;})WGSL",
    .usedOverrides = {{false, false, true, false, true, false, false, false, true, true, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_atlas_blit.webgpu_fixedcolor_frag",
};
} // namespace wgsl
