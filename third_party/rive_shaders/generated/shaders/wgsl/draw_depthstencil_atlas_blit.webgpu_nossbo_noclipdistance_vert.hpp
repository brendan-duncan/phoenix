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

inline constexpr Shader draw_depthstencil_atlas_blit_webgpu_nossbo_noclipdistance_vert = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _u{@builtin(position) _n:vec4<f32>,_Z:f32,_1:array<f32,1>,_0:array<f32,1>,}struct VertexOutput{@location(1) member:vec2<f32>,@location(4)@interpolate(flat,either) member_1:f32,@location(6)@interpolate(flat,either) member_2:f32,@location(0) member_3:vec4<f32>,@location(9) member_4:vec3<f32>,@builtin(position) _n:vec4<f32>,}@id(0) override qj:bool=true;@id(2) override sj:bool=true;@id(4) override uj:bool=true;@group(0)@binding(2) var KB:texture_2d<u32>;@group(0)@binding(0) var<uniform>j:WB;var<private>_M:i32;var<private>LB_1:vec3<f32>;var<private>U2_:vec2<f32>;@group(0)@binding(3) var XC:texture_2d<u32>;var<private>f4_:f32;var<private>S0_:f32;@group(0)@binding(4) var JB:texture_2d<f32>;var<private>N0_:vec4<f32>;var<private>O0_:vec3<f32>;var<private>_r:_u=_u(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(5) var ED:texture_2d<u32>;@group(3)@binding(9) var eb:sampler;fn _J(){var _p:u32;var _m:f32;var _h:f32;var _k:f32;var _q:bool;var _i:f32;let _c=LB_1;let _b=(bitcast<u32>(_c.z)&65535u);let _F=((_b*4u)+2u);let _G=textureLoad(KB,vec2<i32>(bitcast<i32>((_F&255u)),bitcast<i32>((_F>>bitcast<u32>(8i)))),0i);let _g=_c.xy;let _v=bitcast<vec3<f32>>(_G.yzw);let _X=j.Ti;U2_=(((_g*_v.x)+_v.yz)*_X);let _a=textureLoad(XC,vec2<i32>(bitcast<i32>((_b&255u)),bitcast<i32>((_b>>bitcast<u32>(8i)))),0i);let _j=(_a.x&15u);if qj{let _E=(_j==0u);if _E{_p=_a.y;}else{_p=_a.x;}let _W=_p;let _w=(_W>>bitcast<u32>(16i));let _I=j.z6_;if (_w==0u){_m=0f;}else{_m=unpack2x16float(((_w+1023u)*_I)).x;}let _t=_m;_h=_t;if _E{_h=-(_t);}let _P=_h;f4_=_P;}if sj{S0_=f32(((_a.x>>bitcast<u32>(4i))&15u));}if (_j==1u){N0_=unpack4x8unorm(_a.y);}else{let _o=(_b*8u);let _e=textureLoad(JB,vec2<i32>(bitcast<i32>((_o&255u)),bitcast<i32>((_o>>bitcast<u32>(8i)))),0i);let _y=(_o+1u);let _f=textureLoad(JB,vec2<i32>(bitcast<i32>((_y&255u)),bitcast<i32>((_y>>bitcast<u32>(8i)))),0i);let _x=((mat2x2<f32>(vec2<f32>(_e.x,_e.y),vec2<f32>(_e.z,_e.w))*_g)+_f.xy);let _C=((_f.w+(f32(_j)*0.125f))+(max(_f.z,0f)*0.00024414063f));if (_f.z<0f){_k=-(_C);}else{_k=_C;}let _K=_k;N0_=vec4<f32>(_x.x,_x.y,_K,(-0.75f-round((bitcast<f32>(_a.y)*255f))));}_q=uj;if uj{_q=((_a.x&2048u)!=0u);}let _T=_q;if _T{let _H=(_b*8u);let _B=(_H+4u);let _d=textureLoad(JB,vec2<i32>(bitcast<i32>((_B&255u)),bitcast<i32>((_B>>bitcast<u32>(8i)))),0i);let _A=(_H+5u);let _s=textureLoad(JB,vec2<i32>(bitcast<i32>((_A&255u)),bitcast<i32>((_A>>bitcast<u32>(8i)))),0i);let _D=((mat2x2<f32>(vec2<f32>(_d.x,_d.y),vec2<f32>(_d.z,_d.w))*_g)+_s.xy);_i=(1f+_s.z);if ((_a.x&4096u)!=0u){_i=(-1f-f32(((_a.x&24576u)>>bitcast<u32>(13u))));}let _Y=_i;O0_=vec3<f32>(_D.x,_D.y,_Y);}else{O0_=vec3<f32>(0f,0f,0f);}let _V=j.Sg;let _z=j.Tg;let _l=vec4<f32>(((_c.x*_V)-1f),((_c.y*_z)-sign(_z)),0f,1f);_r._n=vec4<f32>(_l.x,_l.y,((f32(((_G.x<<bitcast<u32>(8u))|255u))*0.000000059604645f)+0.000000029802322f),_l.w);return;}@vertex fn main(@builtin(vertex_index) _R:u32,@location(0) LB:vec3<f32>)->VertexOutput{_M=i32(_R);LB_1=LB;_J();let _Q=U2_;let _O=f4_;let _U=S0_;let _L=N0_;let _S=O0_;let _N=_r._n;return VertexOutput(_Q,_O,_U,_L,_S,_N);})WGSL",
    .usedOverrides = {{true, false, true, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_atlas_blit.webgpu_nossbo_noclipdistance_vert",
};
} // namespace wgsl
