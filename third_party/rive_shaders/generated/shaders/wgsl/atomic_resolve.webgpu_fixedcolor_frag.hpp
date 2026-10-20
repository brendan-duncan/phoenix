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

inline constexpr Shader atomic_resolve_webgpu_fixedcolor_frag = {
    .source = R"WGSL(
struct ug{v2_:array<vec2<u32>>,}struct m0sf{v2_:array<u32>,}struct vg{v2_:array<vec4<f32>>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct W4sf{v2_:array<u32>,}@id(8) override yj:bool=true;@id(9) override zj:bool=true;@id(5) override vj:bool=true;@id(0) override qj:bool=true;@id(1) override rj:bool=true;@id(2) override sj:bool=true;@group(0)@binding(3) var<storage>XC:ug;@group(2)@binding(1) var<storage,read_write>m0_:m0sf;@group(0)@binding(4) var<storage>JB:vg;var<private>_U:vec4<f32>;@group(0)@binding(0) var<uniform>j:WB;@group(0)@binding(8) var ZC:texture_2d<f32>;@group(3)@binding(8) var S8_:sampler;@group(2)@binding(3) var<storage,read_write>W4_:W4sf;var<private>Q1_:vec4<f32>;@group(3)@binding(9) var eb:sampler;@group(0)@binding(9) var AD:texture_2d<f32>;@group(1)@binding(11) var RB:texture_2d<f32>;@group(1)@binding(13) var v4_:sampler;fn _0(){var _m:bool;var _B:f32;var _s:f32;var _r:f32;var _j:f32;var _z:f32;var _l:bool;var _w:f32;var _n:f32;var _v:f32;var _y:vec2<f32>;var _i:vec4<f32>;var _E:vec4<f32>;var _x:vec3<f32>;let _u=_U;let _C=_u.xy;let _b=bitcast<vec2<u32>>(vec2<i32>(floor(_C)));let _Y=j.W6_;let _N=bitcast<i32>((((((_b.y>>bitcast<u32>(5u))*(((_Y+31u)&4294967264u)<<bitcast<u32>(5u)))+((_b.x>>bitcast<u32>(5u))<<bitcast<u32>(10u)))+(((_b.x&28u)<<bitcast<u32>(5u))+((_b.y&28u)<<bitcast<u32>(2i))))+(((_b.y&3u)<<bitcast<u32>(2i))+(_b.x&3u))));let _J=W4_.v2_[_N];let _T=((f32((_J&131071u))*0.00048828125f)+-32f);let _p=(_J>>bitcast<u32>(17u));let _a=XC.v2_[_p];_s=_T;if ((_a.x&768u)!=0u){let _R=abs(_T);_m=vj;if vj{_m=((_a.x&512u)!=0u);}let _ad=_m;_B=_R;if _ad{_B=(1f-abs(((fract((_R*0.5f))*2f)+-1f)));}let _3=_B;_s=_3;}let _ac=_s;let _o=clamp(_ac,0f,1f);_z=_o;if qj{let _Q=(_a.x>>bitcast<u32>(16u));_j=_o;if (_Q!=0u){let _F=m0_.v2_[_N];if (_Q==(_F>>bitcast<u32>(16i))){_r=min(_o,unpack2x16float(_F).x);}else{_r=0f;}let _ae=_r;_j=_ae;}let _6=_j;_z=_6;}let _P=_z;_l=rj;if rj{_l=((_a.x&1024u)!=0u);}let _4=_l;_w=_P;if _4{let _H=(_p*8u);let _f=JB.v2_[(_H+2u)];let _S=JB.v2_[(_H+3u)];let _O=_S.zw;let _L=((abs(((mat2x2<f32>(vec2<f32>(_f.x,_f.y),vec2<f32>(_f.z,_f.w))*_C)+_S.xy))*_O)-_O);_w=min(_P,clamp((min(_L.x,_L.y)+0.5f),0f,1f));}let _ab=_w;let _A=(_a.x&15u);if (_A<=1u){_E=select(unpack4x8unorm(_a.y),vec4<f32>(0f,0f,0f,0f),vec4((qj&&(_A==0u))));}else{let _G=(_p*8u);let _h=JB.v2_[_G];let _d=JB.v2_[(_G+1u)];let _e=((mat2x2<f32>(vec2<f32>(_h.x,_h.y),vec2<f32>(_h.z,_h.w))*_C)+_d.xy);let _W=f32(_A);let _7=j.Q8_;let _1=j.R8_;let _I=((_d.w*_7)+_1);switch bitcast<i32>(0u){default:{if (zj&&(_W==4f)){let _M=normalize(vec2<f32>(_e.x,-(_e.y)));let _V=acos(clamp(_M.x,-1f,1f));if (_M.y>=0f){_v=_V;}else{_v=-(_V);}let _8=_v;_y=vec2<f32>((_8*0.15915494f),_I);break;}else{if (_W==2f){_n=_e.x;}else{_n=length(_e);}let _9=_n;_y=vec2<f32>(((clamp(_9,0f,1f)*select(0.001953125f,0.9980469f,(_d.z<0f)))+((max(0f,_d.z)*0.001953125f)+0.0009765625f)),_I);break;}}}let _5=_y;let _g=textureSampleLevel(ZC,S8_,_5,0f);_i=_g;if!((sj&&(((_a.x>>bitcast<u32>(4i))&15u)!=0u))){let _D=(_g.xyz*_g.w);_i=vec4<f32>(_D.x,_D.y,_D.z,(_g.w*abs(bitcast<f32>(_a.y))));}let _X=_i;_E=_X;}let _Z=_E;let _c=(_Z*_ab);let _K=_c.xyz;let _aa=j.H3_;let _2=j.I3_;if (yj&&(_c.w!=0f)){_x=(vec3(((fract((52.982918f*fract(((0.06711056f*_u.x)+(0.00583715f*_u.y)))))*_aa)+_2))+_K);}else{_x=_K;}let _q=_x;let _t=vec4<f32>(_q.x,_c.y,_c.z,_c.w);let _k=vec4<f32>(_t.x,_q.y,_t.z,_t.w);Q1_=vec4<f32>(_k.x,_k.y,_q.z,_k.w);return;}@fragment fn main(@builtin(position) _af:vec4<f32>)->@location(0) vec4<f32>{_U=_af;_0();let _e3=Q1_;return _e3;})WGSL",
    .usedOverrides = {{true, true, true, false, false, true, false, false, true, true, false, false, false, false, false, false, false, false}},
    .label = "atomic_resolve.webgpu_fixedcolor_frag",
};
} // namespace wgsl
