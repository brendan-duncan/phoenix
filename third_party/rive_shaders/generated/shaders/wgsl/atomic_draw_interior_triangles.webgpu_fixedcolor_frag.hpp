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

inline constexpr Shader atomic_draw_interior_triangles_webgpu_fixedcolor_frag = {
    .source = R"WGSL(
struct ug{v2_:array<vec2<u32>>,}struct m0sf{v2_:array<u32>,}struct vg{v2_:array<vec4<f32>>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct W4sf{v2_:array<u32>,}@id(8) override yj:bool=true;@id(9) override zj:bool=true;@id(5) override vj:bool=true;@id(0) override qj:bool=true;@id(1) override rj:bool=true;@id(2) override sj:bool=true;@group(0)@binding(3) var<storage>XC:ug;@group(2)@binding(1) var<storage,read_write>m0_:m0sf;@group(0)@binding(4) var<storage>JB:vg;var<private>_S:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;@group(2)@binding(3) var<storage,read_write>W4_:W4sf;var<private>I0_1:u32;var<private>p1_1:f32;var<private>Q1_:vec4<f32>;@group(3)@binding(9) var eb:sampler;@group(0)@binding(9) var AD:texture_2d<f32>;@group(1)@binding(11) var RB:texture_2d<f32>;@group(1)@binding(13) var v4_:sampler;fn _al(){var _r:u32;var _k:bool;var _w:f32;var _L:f32;var _J:f32;var _x:f32;var _l:f32;var _E:bool;var _F:f32;var _s:f32;var _p:f32;var _v:vec2<f32>;var _M:vec4<f32>;var _I:u32;var _C:u32;var _t:vec4<f32>;var _z:u32;var _B:vec4<f32>;var _o:vec3<f32>;let _n=_S;let _A=_n.xy;let _b=bitcast<vec2<u32>>(vec2<i32>(floor(_A)));let _ac=j.W6_;let _i=bitcast<i32>((((((_b.y>>bitcast<u32>(5u))*(((_ac+31u)&4294967264u)<<bitcast<u32>(5u)))+((_b.x>>bitcast<u32>(5u))<<bitcast<u32>(10u)))+(((_b.x&28u)<<bitcast<u32>(5u))+((_b.y&28u)<<bitcast<u32>(2i))))+(((_b.y&3u)<<bitcast<u32>(2i))+(_b.x&3u))));let _K=W4_.v2_[_i];let _d=(_K>>bitcast<u32>(17u));let _D=I0_1;if (_d==_D){_r=_K;}else{_r=((_D<<bitcast<u32>(17u))+65536u);}let _ai=_r;let _an=p1_1;W4_.v2_[_i]=(_ai+bitcast<u32>(i32(round((_an*2048f)))));_z=0u;_B=vec4<f32>(0f,0f,0f,0f);if (_d!=_D){let _Y=((f32((_K&131071u))*0.00048828125f)+-32f);let _a=XC.v2_[_d];_L=_Y;if ((_a.x&768u)!=0u){let _T=abs(_Y);_k=vj;if vj{_k=((_a.x&512u)!=0u);}let _9=_k;_w=_T;if _9{_w=(1f-abs(((fract((_T*0.5f))*2f)+-1f)));}let _am=_w;_L=_am;}let _af=_L;let _H=clamp(_af,0f,1f);_l=_H;if qj{let _2=(_a.x>>bitcast<u32>(16u));_x=_H;if (_2!=0u){let _X=m0_.v2_[_i];if (_2==(_X>>bitcast<u32>(16i))){_J=min(_H,unpack2x16float(_X).x);}else{_J=0f;}let _aq=_J;_x=_aq;}let _ab=_x;_l=_ab;}let _W=_l;_E=rj;if rj{_E=((_a.x&1024u)!=0u);}let _aj=_E;_F=_W;if _aj{let _0=(_d*8u);let _e=JB.v2_[(_0+2u)];let _V=JB.v2_[(_0+3u)];let _5=_V.zw;let _P=((abs(((mat2x2<f32>(vec2<f32>(_e.x,_e.y),vec2<f32>(_e.z,_e.w))*_A)+_V.xy))*_5)-_5);_F=min(_W,clamp((min(_P.x,_P.y)+0.5f),0f,1f));}let _4=_F;let _y=(_a.x&15u);if (_y<=1u){let _U=(qj&&(_y==0u));_I=0u;if _U{_I=(_a.y|pack2x16float(vec2<f32>(_4,0f)));}let _ae=_I;_C=_ae;_t=select(unpack4x8unorm(_a.y),vec4<f32>(0f,0f,0f,0f),vec4(_U));}else{let _Z=(_d*8u);let _h=JB.v2_[_Z];let _g=JB.v2_[(_Z+1u)];let _j=((mat2x2<f32>(vec2<f32>(_h.x,_h.y),vec2<f32>(_h.z,_h.w))*_A)+_g.xy);let _Q=f32(_y);let _ak=j.Q8_;let _ao=j.R8_;let _1=((_g.w*_ak)+_ao);switch bitcast<i32>(0u){default:{if (zj&&(_Q==4f)){let _R=normalize(vec2<f32>(_j.x,-(_j.y)));let _3=acos(clamp(_R.x,-1f,1f));if (_R.y>=0f){_p=_3;}else{_p=-(_3);}let _8=_p;_v=vec2<f32>((_8*0.15915494f),_1);break;}else{if (_Q==2f){_s=_j.x;}else{_s=length(_j);}let _ad=_s;_v=vec2<f32>(((clamp(_ad,0f,1f)*select(0.001953125f,0.9980469f,(_g.z<0f)))+((max(0f,_g.z)*0.001953125f)+0.0009765625f)),_1);break;}}}let _6=_v;let _f=textureSampleLevel(ZC,S8_,_6,0f);_M=_f;if!((sj&&(((_a.x>>bitcast<u32>(4i))&15u)!=0u))){let _q=(_f.xyz*_f.w);_M=vec4<f32>(_q.x,_q.y,_q.z,(_f.w*abs(bitcast<f32>(_a.y))));}let _ar=_M;_C=0u;_t=_ar;}let _aa=_C;let _ag=_t;_z=_aa;_B=(_ag*_4);}let _N=_z;let _c=_B;let _O=_c.xyz;let _ah=j.H3_;let _7=j.I3_;if (yj&&(_c.w!=0f)){_o=(vec3(((fract((52.982918f*fract(((0.06711056f*_n.x)+(0.00583715f*_n.y)))))*_ah)+_7))+_O);}else{_o=_O;}let _u=_o;let _G=vec4<f32>(_u.x,_c.y,_c.z,_c.w);let _m=vec4<f32>(_G.x,_u.y,_G.z,_G.w);Q1_=vec4<f32>(_m.x,_m.y,_u.z,_m.w);if (_N!=0u){m0_.v2_[_i]=_N;}return;}@fragment fn main(@builtin(position) _ap:vec4<f32>,@location(1)@interpolate(flat,either) I0_:u32,@location(0)@interpolate(flat,either) p1_:f32)->@location(0) vec4<f32>{_S=_ap;I0_1=I0_;p1_1=p1_;_al();let _e7=Q1_;return _e7;})WGSL",
    .usedOverrides = {{true, true, true, false, false, true, false, false, true, true, false, false, false, false, false, false, false, false}},
    .label = "atomic_draw_interior_triangles.webgpu_fixedcolor_frag",
};
} // namespace wgsl
