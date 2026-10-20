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

inline constexpr Shader atomic_draw_atlas_blit_webgpu_fixedcolor_frag = {
    .source = R"WGSL(
struct ug{v2_:array<vec2<u32>>,}struct m0sf{v2_:array<u32>,}struct vg{v2_:array<vec4<f32>>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct W4sf{v2_:array<u32>,}@id(8) override yj:bool=true;@id(9) override zj:bool=true;@id(5) override vj:bool=true;@id(0) override qj:bool=true;@id(1) override rj:bool=true;@id(2) override sj:bool=true;@group(0)@binding(3) var<storage>XC:ug;@group(2)@binding(1) var<storage,read_write>m0_:m0sf;@group(0)@binding(4) var<storage>JB:vg;var<private>_M:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;@group(2)@binding(3) var<storage,read_write>W4_:W4sf;var<private>I0_1:u32;@group(0)@binding(10) var JD:texture_2d<f32>;@group(3)@binding(10) var Ta:sampler;var<private>U2_1:vec2<f32>;var<private>Q1_:vec4<f32>;@group(3)@binding(9) var eb:sampler;@group(0)@binding(9) var AD:texture_2d<f32>;@group(1)@binding(11) var RB:texture_2d<f32>;@group(1)@binding(13) var v4_:sampler;fn _9(){var _m:bool;var _p:f32;var _r:f32;var _z:f32;var _q:f32;var _y:f32;var _A:bool;var _D:f32;var _B:f32;var _x:f32;var _u:vec2<f32>;var _s:vec4<f32>;var _G:u32;var _C:u32;var _k:vec4<f32>;var _w:vec3<f32>;let _F=_M;let _n=_F.xy;let _b=bitcast<vec2<u32>>(vec2<i32>(floor(_n)));let _aj=j.W6_;let _g=bitcast<i32>((((((_b.y>>bitcast<u32>(5u))*(((_aj+31u)&4294967264u)<<bitcast<u32>(5u)))+((_b.x>>bitcast<u32>(5u))<<bitcast<u32>(10u)))+(((_b.x&28u)<<bitcast<u32>(5u))+((_b.y&28u)<<bitcast<u32>(2i))))+(((_b.y&3u)<<bitcast<u32>(2i))+(_b.x&3u))));let _O=W4_.v2_[_g];let _j=(_O>>bitcast<u32>(17u));let _5=I0_1;let _am=U2_1;let _ac=textureSampleLevel(JD,Ta,_am,0f);W4_.v2_[_g]=(((_5<<bitcast<u32>(17u))+65536u)+bitcast<u32>(i32(round((clamp(_ac.x,0f,1f)*2048f)))));let _V=((f32((_O&131071u))*0.00048828125f)+-32f);let _a=XC.v2_[_j];_r=_V;if ((_a.x&768u)!=0u){let _W=abs(_V);_m=vj;if vj{_m=((_a.x&512u)!=0u);}let _6=_m;_p=_W;if _6{_p=(1f-abs(((fract((_W*0.5f))*2f)+-1f)));}let _ag=_p;_r=_ag;}let _ah=_r;let _l=clamp(_ah,0f,1f);_y=_l;if qj{let _X=(_a.x>>bitcast<u32>(16u));_q=_l;if (_X!=0u){let _Z=m0_.v2_[_g];if (_X==(_Z>>bitcast<u32>(16i))){_z=min(_l,unpack2x16float(_Z).x);}else{_z=0f;}let _ai=_z;_q=_ai;}let _8=_q;_y=_8;}let _Q=_y;_A=rj;if rj{_A=((_a.x&1024u)!=0u);}let _4=_A;_D=_Q;if _4{let _P=(_j*8u);let _h=JB.v2_[(_P+2u)];let _T=JB.v2_[(_P+3u)];let _R=_T.zw;let _Y=((abs(((mat2x2<f32>(vec2<f32>(_h.x,_h.y),vec2<f32>(_h.z,_h.w))*_n)+_T.xy))*_R)-_R);_D=min(_Q,clamp((min(_Y.x,_Y.y)+0.5f),0f,1f));}let _J=_D;let _v=(_a.x&15u);if (_v<=1u){let _K=(qj&&(_v==0u));_G=0u;if _K{_G=(_a.y|pack2x16float(vec2<f32>(_J,0f)));}let _an=_G;_C=_an;_k=select(unpack4x8unorm(_a.y),vec4<f32>(0f,0f,0f,0f),vec4(_K));}else{let _U=(_j*8u);let _d=JB.v2_[_U];let _i=JB.v2_[(_U+1u)];let _e=((mat2x2<f32>(vec2<f32>(_d.x,_d.y),vec2<f32>(_d.z,_d.w))*_n)+_i.xy);let _0=f32(_v);let _ao=j.Q8_;let _7=j.R8_;let _L=((_i.w*_ao)+_7);switch bitcast<i32>(0u){default:{if (zj&&(_0==4f)){let _S=normalize(vec2<f32>(_e.x,-(_e.y)));let _I=acos(clamp(_S.x,-1f,1f));if (_S.y>=0f){_x=_I;}else{_x=-(_I);}let _ad=_x;_u=vec2<f32>((_ad*0.15915494f),_L);break;}else{if (_0==2f){_B=_e.x;}else{_B=length(_e);}let _al=_B;_u=vec2<f32>(((clamp(_al,0f,1f)*select(0.001953125f,0.9980469f,(_i.z<0f)))+((max(0f,_i.z)*0.001953125f)+0.0009765625f)),_L);break;}}}let _3=_u;let _f=textureSampleLevel(ZC,S8_,_3,0f);_s=_f;if!((sj&&(((_a.x>>bitcast<u32>(4i))&15u)!=0u))){let _H=(_f.xyz*_f.w);_s=vec4<f32>(_H.x,_H.y,_H.z,(_f.w*abs(bitcast<f32>(_a.y))));}let _af=_s;_C=0u;_k=_af;}let _1=_C;let _aa=_k;let _c=(_aa*_J);let _N=_c.xyz;let _ak=j.H3_;let _ab=j.I3_;if (yj&&(_c.w!=0f)){_w=(vec3(((fract((52.982918f*fract(((0.06711056f*_F.x)+(0.00583715f*_F.y)))))*_ak)+_ab))+_N);}else{_w=_N;}let _E=_w;let _o=vec4<f32>(_E.x,_c.y,_c.z,_c.w);let _t=vec4<f32>(_o.x,_E.y,_o.z,_o.w);Q1_=vec4<f32>(_t.x,_t.y,_E.z,_t.w);if (_1!=0u){m0_.v2_[_g]=_1;}return;}@fragment fn main(@builtin(position) _ae:vec4<f32>,@location(1)@interpolate(flat,either) I0_:u32,@location(0) U2_:vec2<f32>)->@location(0) vec4<f32>{_M=_ae;I0_1=I0_;U2_1=U2_;_9();let _2=Q1_;return _2;})WGSL",
    .usedOverrides = {{true, true, true, false, false, true, false, false, true, true, false, false, false, false, false, false, false, false}},
    .label = "atomic_draw_atlas_blit.webgpu_fixedcolor_frag",
};
} // namespace wgsl
