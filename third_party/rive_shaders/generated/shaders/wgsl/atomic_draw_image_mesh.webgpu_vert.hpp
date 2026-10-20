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

inline constexpr Shader atomic_draw_image_mesh_webgpu_vert = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _o{@builtin(position) _f:vec4<f32>,_K:f32,_M:array<f32,1>,_L:array<f32,1>,}struct yi{v2_:array<vec4<u32>>,}struct ug{v2_:array<vec2<u32>>,}struct vg{v2_:array<vec4<f32>>,}struct zi{v2_:array<vec4<u32>>,}struct VertexOutput{@location(0) member:vec2<f32>,@location(1) member_1:vec4<f32>,@location(3)@interpolate(flat,either) member_2:vec4<f32>,@location(4)@interpolate(flat,either) member_3:u32,@location(5)@interpolate(flat,either) member_4:u32,@builtin(position) _f:vec4<f32>,}@id(1) override rj:bool=true;var<private>_H:i32;var<private>_A:i32;var<private>AC_1:vec4<f32>;var<private>PC_1:vec2<f32>;var<private>PB_1:vec4<f32>;var<private>n2_:vec2<f32>;var<private>QC_1:vec2<f32>;var<private>IC_1:vec4<f32>;var<private>W0_:vec4<f32>;var<private>UB_1:vec4<f32>;var<private>Y1_:vec4<f32>;var<private>BC_1:u32;var<private>V3_:u32;var<private>CC_1:u32;var<private>N1_:u32;var<private>DC_1:u32;@group(0)@binding(0) var<uniform>j:WB;var<private>_q:_o=_o(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());@group(0)@binding(7) var VB:texture_2d<u32>;@group(0)@binding(9) var AD:texture_2d<f32>;@group(0)@binding(2) var<storage>KB:yi;@group(0)@binding(3) var<storage>XC:ug;@group(0)@binding(4) var<storage>JB:vg;@group(0)@binding(5) var<storage>ED:zi;@group(3)@binding(9) var eb:sampler;var<private>MC_1:u32;fn _D(){var _g:bool;var _h:vec4<f32>;let _a=AC_1;let _s=PC_1;let _e=PB_1;let _i=((mat2x2<f32>(vec2<f32>(_a.x,_a.y),vec2<f32>(_a.z,_a.w))*_s)+_e.xy);let _z=QC_1;let _m=IC_1;n2_=((_z*_m.zw)+_m.xy);if rj{let _b=UB_1;let _p=vec2<f32>(_b.x,_b.y);let _k=vec2<f32>(_b.z,_b.w);switch bitcast<i32>(0u){default:{let _d=(abs(_p)+abs(_k));let _l=(_d.x!=0f);_g=_l;if _l{_g=(_d.y!=0f);}let _F=_g;if _F{let _c=((mat2x2<f32>(_p,_k)*_i)+_e.zw);let _n=-(_c);let _j=(vec2<f32>(1f,1f)/_d).xyxy;_h=(((vec4<f32>(_c.x,_c.y,_n.x,_n.y)*_j)+_j)+vec4<f32>(0.5f,0.5f,0.5f,0.5f));break;}else{_h=_e.zwzw;break;}}}let _B=_h;W0_=_B;}let _v=BC_1;Y1_=unpack4x8unorm(_v);let _I=CC_1;V3_=_I;let _C=DC_1;N1_=_C;let _u=j.Sg;let _r=j.Tg;_q._f=vec4<f32>(((_i.x*_u)-1f),((_i.y*_r)-sign(_r)),0f,1f);return;}@vertex fn main(@builtin(vertex_index) _J:u32,@builtin(instance_index) _y:u32,@location(2) AC:vec4<f32>,@location(0) PC:vec2<f32>,@location(4) PB:vec4<f32>,@location(1) QC:vec2<f32>,@location(9) IC:vec4<f32>,@location(3) UB:vec4<f32>,@location(5) BC:u32,@location(6) CC:u32,@location(7) DC:u32,@location(8) MC:u32)->VertexOutput{_H=i32(_J);_A=i32(_y);AC_1=AC;PC_1=PC;PB_1=PB;QC_1=QC;IC_1=IC;UB_1=UB;BC_1=BC;CC_1=CC;DC_1=DC;MC_1=MC;_D();let _x=n2_;let _G=W0_;let _E=Y1_;let _t=V3_;let _w=N1_;let _a=_q._f;return VertexOutput(_x,_G,_E,_t,_w,_a);})WGSL",
    .usedOverrides = {{false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "atomic_draw_image_mesh.webgpu_vert",
};
} // namespace wgsl
