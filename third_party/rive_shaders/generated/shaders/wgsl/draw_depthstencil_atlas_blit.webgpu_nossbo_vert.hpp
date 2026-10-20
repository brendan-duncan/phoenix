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

inline constexpr Shader draw_depthstencil_atlas_blit_webgpu_nossbo_vert = {
    .source = R"WGSL(
enable clip_distances;struct _O{@builtin(position) _m:vec4<f32>,_8:f32,@builtin(clip_distances) _b:array<f32,4>,_9:array<f32,1>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct VertexOutput{@builtin(position) _m:vec4<f32>,@builtin(clip_distances) _b:array<f32,4>,@location(1) member:vec2<f32>,@location(4)@interpolate(flat,either) member_1:f32,@location(6)@interpolate(flat,either) member_2:f32,@location(0) member_3:vec4<f32>,@location(9) member_4:vec3<f32>,}@id(0) override qj:bool=true;@id(2) override sj:bool=true;@id(1) override rj:bool=true;@id(4) override uj:bool=true;var<private>_a:_O=_O(vec4<f32>(0f,0f,0f,1f),1f,array<f32,4>(),array<f32,1>());@group(0)@binding(2) var KB:texture_2d<u32>;@group(0)@binding(0) var<uniform>j:WB;var<private>_Y:i32;var<private>LB_1:vec3<f32>;var<private>U2_:vec2<f32>;@group(0)@binding(3) var XC:texture_2d<u32>;var<private>f4_:f32;var<private>S0_:f32;@group(0)@binding(4) var JB:texture_2d<f32>;var<private>N0_:vec4<f32>;var<private>O0_:vec3<f32>;@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(5) var ED:texture_2d<u32>;@group(3)@binding(9) var eb:sampler;fn _5(){var _q:u32;var _r:f32;var _p:f32;var _n:f32;var _s:bool;var _u:f32;let _h=LB_1;let _d=(bitcast<u32>(_h.z)&65535u);let _E=((_d*4u)+2u);let _A=textureLoad(KB,vec2<i32>(bitcast<i32>((_E&255u)),bitcast<i32>((_E>>bitcast<u32>(8i)))),0i);let _j=_h.xy;let _y=bitcast<vec3<f32>>(_A.yzw);let _2=j.Ti;U2_=(((_j*_y.x)+_y.yz)*_2);let _c=textureLoad(XC,vec2<i32>(bitcast<i32>((_d&255u)),bitcast<i32>((_d>>bitcast<u32>(8i)))),0i);let _t=(_c.x&15u);if qj{let _L=(_t==0u);if _L{_q=_c.y;}else{_q=_c.x;}let _6=_q;let _F=(_6>>bitcast<u32>(16i));let _W=j.z6_;if (_F==0u){_r=0f;}else{_r=unpack2x16float(((_F+1023u)*_W)).x;}let _P=_r;_p=_P;if _L{_p=-(_P);}let _1=_p;f4_=_1;}if sj{S0_=f32(((_c.x>>bitcast<u32>(4i))&15u));}if rj{let _w=(_d*8u);let _G=(_w+2u);let _e=textureLoad(JB,vec2<i32>(bitcast<i32>((_G&255u)),bitcast<i32>((_G>>bitcast<u32>(8i)))),0i);let _x=(_w+3u);let _K=textureLoad(JB,vec2<i32>(bitcast<i32>((_x&255u)),bitcast<i32>((_x>>bitcast<u32>(8i)))),0i);if any((_e!=vec4<f32>(0f,0f,0f,0f))){let _f=((mat2x2<f32>(vec2<f32>(_e.x,_e.y),vec2<f32>(_e.z,_e.w))*_j)+_K.xy);_a._b[0i]=(_f.x+1f);_a._b[1i]=(_f.y+1f);_a._b[2i]=(1f-_f.x);_a._b[3i]=(1f-_f.y);}else{let _l=(_K.x-0.5f);_a._b[3i]=_l;_a._b[2i]=_l;_a._b[1i]=_l;_a._b[0i]=_l;}}if (_t==1u){N0_=unpack4x8unorm(_c.y);}else{let _o=(_d*8u);let _k=textureLoad(JB,vec2<i32>(bitcast<i32>((_o&255u)),bitcast<i32>((_o>>bitcast<u32>(8i)))),0i);let _J=(_o+1u);let _g=textureLoad(JB,vec2<i32>(bitcast<i32>((_J&255u)),bitcast<i32>((_J>>bitcast<u32>(8i)))),0i);let _B=((mat2x2<f32>(vec2<f32>(_k.x,_k.y),vec2<f32>(_k.z,_k.w))*_j)+_g.xy);let _C=((_g.w+(f32(_t)*0.125f))+(max(_g.z,0f)*0.00024414063f));if (_g.z<0f){_n=-(_C);}else{_n=_C;}let _0=_n;N0_=vec4<f32>(_B.x,_B.y,_0,(-0.75f-round((bitcast<f32>(_c.y)*255f))));}_s=uj;if uj{_s=((_c.x&2048u)!=0u);}let _7=_s;if _7{let _z=(_d*8u);let _M=(_z+4u);let _i=textureLoad(JB,vec2<i32>(bitcast<i32>((_M&255u)),bitcast<i32>((_M>>bitcast<u32>(8i)))),0i);let _H=(_z+5u);let _N=textureLoad(JB,vec2<i32>(bitcast<i32>((_H&255u)),bitcast<i32>((_H>>bitcast<u32>(8i)))),0i);let _I=((mat2x2<f32>(vec2<f32>(_i.x,_i.y),vec2<f32>(_i.z,_i.w))*_j)+_N.xy);_u=(1f+_N.z);if ((_c.x&4096u)!=0u){_u=(-1f-f32(((_c.x&24576u)>>bitcast<u32>(13u))));}let _S=_u;O0_=vec3<f32>(_I.x,_I.y,_S);}else{O0_=vec3<f32>(0f,0f,0f);}let _T=j.Sg;let _D=j.Tg;let _v=vec4<f32>(((_h.x*_T)-1f),((_h.y*_D)-sign(_D)),0f,1f);_a._m=vec4<f32>(_v.x,_v.y,((f32(((_A.x<<bitcast<u32>(8u))|255u))*0.000000059604645f)+0.000000029802322f),_v.w);return;}@vertex fn main(@builtin(vertex_index) _R:u32,@location(0) LB:vec3<f32>)->VertexOutput{_Y=i32(_R);LB_1=LB;_5();let _Q=_a._m;let _3=_a._b;let _4=U2_;let _U=f4_;let _X=S0_;let _V=N0_;let _Z=O0_;return VertexOutput(_Q,_3,_4,_U,_X,_V,_Z);})WGSL",
    .usedOverrides = {{true, true, true, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_atlas_blit.webgpu_nossbo_vert",
};
} // namespace wgsl
