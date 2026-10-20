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

inline constexpr Shader draw_depthstencil_path_webgpu_fixedcolor_frag = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}@id(8) override yj:bool=true;@id(9) override zj:bool=true;@id(16) override Gj:bool=false;@id(17) override Hj:bool=false;@id(2) override sj:bool=true;@id(4) override uj:bool=true;var<private>S0_1:f32;var<private>N0_1:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;var<private>O0_1:vec3<f32>;@group(1)@binding(11) var RB:texture_2d<f32>;@group(1)@binding(13) var v4_:sampler;var<private>_I:vec4<f32>;var<private>Yi:vec4<f32>;@group(3)@binding(9) var eb:sampler;@group(0)@binding(9) var AD:texture_2d<f32>;fn _ad(){var _j:f32;var _x:f32;var _l:vec2<f32>;var _f:bool;var _w:vec4<f32>;var _u:vec4<f32>;var _m:vec4<f32>;var _k:bool;var _q:f32;var _v:vec4<f32>;var _r:vec4<f32>;var _y:vec3<f32>;var _h:bool;var _d:f32;var _p:vec4<f32>;let _ab=S0_1;let _O=(sj&&(u32(_ab)!=0u));let _C=N0_1[3u];if (_C>=0f){let _6=N0_1;_m=_6;}else{let _a=N0_1;let _ac=j.Q8_;let _W=j.R8_;let _J=abs(_a.z);let _D=floor(_J);let _H=(((_J-_D)*8f)+0.0009765625f);let _L=((_D*_ac)+_W);let _G=floor(_H);switch bitcast<i32>(0u){default:{if (zj&&(_G==4f)){let _M=normalize(vec2<f32>(_a.x,-(_a.y)));let _F=acos(clamp(_M.x,-1f,1f));if (_M.y>=0f){_x=_F;}else{_x=-(_F);}let _5=_x;_l=vec2<f32>((_5*0.15915494f),_L);break;}else{if (_G==2f){_j=_a.x;}else{_j=length(_a.xy);}let _Z=_j;_l=vec2<f32>(((clamp(_Z,0f,1f)*select(0.001953125f,0.9980469f,(_a.z<0f)))+_H),_L);break;}}}let _P=_l;let _b=textureSampleLevel(ZC,S8_,_P,0f);switch bitcast<i32>(0u){default:{if Gj{_f=true;break;}if Hj{_f=true;break;}_f=false;break;}}let _2=_f;_w=_b;if _2{_w=vec4<f32>(_b.x,_b.y,_b.z,(_b.w*((fract(_a.w)*-2f)+1.5f)));}let _g=_w;_u=_g;if!(_O){let _A=(_g.xyz*_g.w);_u=vec4<f32>(_A.x,_A.y,_A.z,(_g.w*(ceil(_a.w)*-0.003921569f)));}let _V=_u;_m=_V;}let _N=_m;_k=uj;if uj{let _1=O0_1[2u];_k=(_1>0f);}let _Q=_k;_r=_N;if _Q{let _ae=O0_1[2u];let _9=O0_1;let _c=textureSampleLevel(RB,v4_,_9.xy,(_ae-1f));_v=_c;if _O{if (_c.w!=0f){_q=(1f/_c.w);}else{_q=0f;}let _U=_q;let _s=(_c.xyz*_U);_v=vec4<f32>(_s.x,_s.y,_s.z,_c.w);}let _0=_v;_r=(_N*_0);}let _e=_r;let _E=_e.xyz;let _B=_I;let _7=j.H3_;let _Y=j.I3_;if (yj&&(_e.w!=0f)){_y=(vec3(((fract((52.982918f*fract(((0.06711056f*_B.x)+(0.00583715f*_B.y)))))*_7)+_Y))+_E);}else{_y=_E;}let _z=_y;let _n=vec4<f32>(_z.x,_e.y,_e.z,_e.w);let _t=vec4<f32>(_n.x,_z.y,_n.z,_n.w);_h=uj;if uj{let _R=O0_1[2u];_h=(_R<0f);}let _aa=_h;_p=vec4<f32>(_t.x,_t.y,_z.z,_t.w);if _aa{let _4=O0_1;let _i=textureSampleLevel(RB,v4_,_4.xy,0f);let _T=O0_1[2u];let _o=u32((-1f-_T));switch bitcast<i32>(0u){default:{let _K=dot(_i.xyz,vec3<f32>(0.3f,0.59f,0.11f));if (_o==0u){_d=_i.w;break;}if (_o==1u){_d=(1f-_i.w);break;}if (_o==2u){_d=_K;break;}_d=(1f-_K);break;}}let _X=_d;_p=vec4(mix(1f,_X,_C));}let _8=_p;Yi=_8;return;}@fragment fn main(@location(1)@interpolate(flat,either) S0_:f32,@location(0) N0_:vec4<f32>,@location(2) O0_:vec3<f32>,@builtin(position) _3:vec4<f32>)->@location(0) vec4<f32>{S0_1=S0_;N0_1=N0_;O0_1=O0_;_I=_3;_ad();let _S=Yi;return _S;})WGSL",
    .usedOverrides = {{false, false, true, false, true, false, false, false, true, true, false, false, false, false, false, false, true, true}},
    .label = "draw_depthstencil_path.webgpu_fixedcolor_frag",
};
} // namespace wgsl
