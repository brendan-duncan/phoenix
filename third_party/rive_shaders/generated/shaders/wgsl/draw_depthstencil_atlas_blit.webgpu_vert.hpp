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

inline constexpr Shader draw_depthstencil_atlas_blit_webgpu_vert = {
    .source = R"WGSL(
enable clip_distances;struct _D{@builtin(position) _o:vec4<f32>,_2:f32,@builtin(clip_distances) _b:array<f32,4>,_3:array<f32,1>,}struct yi{v2_:array<vec4<u32>>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct ug{v2_:array<vec2<u32>>,}struct vg{v2_:array<vec4<f32>>,}struct zi{v2_:array<vec4<u32>>,}struct VertexOutput{@builtin(position) _o:vec4<f32>,@builtin(clip_distances) _b:array<f32,4>,@location(1) member:vec2<f32>,@location(4)@interpolate(flat,either) member_1:f32,@location(6)@interpolate(flat,either) member_2:f32,@location(0) member_3:vec4<f32>,@location(9) member_4:vec3<f32>,}@id(0) override qj:bool=true;@id(2) override sj:bool=true;@id(1) override rj:bool=true;@id(4) override uj:bool=true;var<private>_a:_D=_D(vec4<f32>(0f,0f,0f,1f),1f,array<f32,4>(),array<f32,1>());@group(0)@binding(2) var<storage>KB:yi;@group(0)@binding(0) var<uniform>j:WB;var<private>_K:i32;var<private>LB_1:vec3<f32>;var<private>U2_:vec2<f32>;@group(0)@binding(3) var<storage>XC:ug;var<private>f4_:f32;var<private>S0_:f32;@group(0)@binding(4) var<storage>JB:vg;var<private>N0_:vec4<f32>;var<private>O0_:vec3<f32>;@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(5) var<storage>ED:zi;@group(3)@binding(9) var eb:sampler;fn _O(){var _s:u32;var _t:f32;var _q:f32;var _m:f32;var _p:bool;var _r:f32;let _l=LB_1;let _d=(bitcast<u32>(_l.z)&65535u);let _A=KB.v2_[((_d*4u)+2u)];let _j=_l.xy;let _x=bitcast<vec3<f32>>(_A.yzw);let _Z=j.Ti;U2_=(((_j*_x.x)+_x.yz)*_Z);let _c=XC.v2_[_d];let _n=(_c.x&15u);if qj{let _H=(_n==0u);if _H{_s=_c.y;}else{_s=_c.x;}let _0=_s;let _v=(_0>>bitcast<u32>(16i));let _X=j.z6_;if (_v==0u){_t=0f;}else{_t=unpack2x16float(((_v+1023u)*_X)).x;}let _G=_t;_q=_G;if _H{_q=-(_G);}let _P=_q;f4_=_P;}if sj{S0_=f32(((_c.x>>bitcast<u32>(4i))&15u));}if rj{let _I=(_d*8u);let _e=JB.v2_[(_I+2u)];let _z=JB.v2_[(_I+3u)];if any((_e!=vec4<f32>(0f,0f,0f,0f))){let _g=((mat2x2<f32>(vec2<f32>(_e.x,_e.y),vec2<f32>(_e.z,_e.w))*_j)+_z.xy);_a._b[0i]=(_g.x+1f);_a._b[1i]=(_g.y+1f);_a._b[2i]=(1f-_g.x);_a._b[3i]=(1f-_g.y);}else{let _k=(_z.x-0.5f);_a._b[3i]=_k;_a._b[2i]=_k;_a._b[1i]=_k;_a._b[0i]=_k;}}if (_n==1u){N0_=unpack4x8unorm(_c.y);}else{let _w=(_d*8u);let _f=JB.v2_[_w];let _h=JB.v2_[(_w+1u)];let _E=((mat2x2<f32>(vec2<f32>(_f.x,_f.y),vec2<f32>(_f.z,_f.w))*_j)+_h.xy);let _B=((_h.w+(f32(_n)*0.125f))+(max(_h.z,0f)*0.00024414063f));if (_h.z<0f){_m=-(_B);}else{_m=_B;}let _V=_m;N0_=vec4<f32>(_E.x,_E.y,_V,(-0.75f-round((bitcast<f32>(_c.y)*255f))));}_p=uj;if uj{_p=((_c.x&2048u)!=0u);}let _1=_p;if _1{let _F=(_d*8u);let _i=JB.v2_[(_F+4u)];let _y=JB.v2_[(_F+5u)];let _C=((mat2x2<f32>(vec2<f32>(_i.x,_i.y),vec2<f32>(_i.z,_i.w))*_j)+_y.xy);_r=(1f+_y.z);if ((_c.x&4096u)!=0u){_r=(-1f-f32(((_c.x&24576u)>>bitcast<u32>(13u))));}let _M=_r;O0_=vec3<f32>(_C.x,_C.y,_M);}else{O0_=vec3<f32>(0f,0f,0f);}let _N=j.Sg;let _J=j.Tg;let _u=vec4<f32>(((_l.x*_N)-1f),((_l.y*_J)-sign(_J)),0f,1f);_a._o=vec4<f32>(_u.x,_u.y,((f32(((_A.x<<bitcast<u32>(8u))|255u))*0.000000059604645f)+0.000000029802322f),_u.w);return;}@vertex fn main(@builtin(vertex_index) _T:u32,@location(0) LB:vec3<f32>)->VertexOutput{_K=i32(_T);LB_1=LB;_O();let _L=_a._o;let _Q=_a._b;let _S=U2_;let _U=f4_;let _W=S0_;let _Y=N0_;let _R=O0_;return VertexOutput(_L,_Q,_S,_U,_W,_Y,_R);})WGSL",
    .usedOverrides = {{true, true, true, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_atlas_blit.webgpu_vert",
};
} // namespace wgsl
