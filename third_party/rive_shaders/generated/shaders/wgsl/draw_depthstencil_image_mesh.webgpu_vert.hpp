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

inline constexpr Shader draw_depthstencil_image_mesh_webgpu_vert = {
    .source = R"WGSL(
enable clip_distances;struct _o{@builtin(position) _i:vec4<f32>,_G:f32,@builtin(clip_distances) _b:array<f32,4>,_H:array<f32,1>,}struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct VertexOutput{@builtin(position) _i:vec4<f32>,@builtin(clip_distances) _b:array<f32,4>,@location(0) member:vec2<f32>,@location(1)@interpolate(flat,either) member_1:f32,@location(3)@interpolate(flat,either) member_2:vec4<f32>,@location(4)@interpolate(flat,either) member_3:u32,}@id(0) override qj:bool=true;@id(1) override rj:bool=true;var<private>_a:_o=_o(vec4<f32>(0f,0f,0f,1f),1f,array<f32,4>(),array<f32,1>());var<private>_p:i32;var<private>AC_1:vec4<f32>;var<private>PC_1:vec2<f32>;var<private>PB_1:vec4<f32>;var<private>S5_:vec2<f32>;var<private>QC_1:vec2<f32>;var<private>IC_1:vec4<f32>;var<private>f4_:f32;var<private>CC_1:u32;@group(0)@binding(0) var<uniform>j:WB;var<private>UB_1:vec4<f32>;var<private>MC_1:u32;var<private>Y1_:vec4<f32>;var<private>BC_1:u32;var<private>N1_:u32;var<private>DC_1:u32;fn _r(){var _j:f32;let _d=AC_1;let _A=PC_1;let _h=PB_1;let _g=((mat2x2<f32>(vec2<f32>(_d.x,_d.y),vec2<f32>(_d.z,_d.w))*_A)+_h.xy);let _x=QC_1;let _m=IC_1;S5_=((_x*_m.zw)+_m.xy);if qj{let _n=CC_1;let _q=j.z6_;if (_n==0u){_j=0f;}else{_j=unpack2x16float(((_n+1023u)*_q)).x;}let _s=_j;f4_=_s;}if rj{let _c=UB_1;if any((_c!=vec4<f32>(0f,0f,0f,0f))){let _e=((mat2x2<f32>(vec2<f32>(_c.x,_c.y),vec2<f32>(_c.z,_c.w))*_g)+_h.zw);_a._b[0i]=(_e.x+1f);_a._b[1i]=(_e.y+1f);_a._b[2i]=(1f-_e.x);_a._b[3i]=(1f-_e.y);}else{let _f=(_h.z-0.5f);_a._b[3i]=_f;_a._b[2i]=_f;_a._b[1i]=_f;_a._b[0i]=_f;}}let _u=j.Sg;let _l=j.Tg;let _k=vec4<f32>(((_g.x*_u)-1f),((_g.y*_l)-sign(_l)),0f,1f);let _C=MC_1;let _t=BC_1;Y1_=unpack4x8unorm(_t);let _E=DC_1;N1_=_E;_a._i=vec4<f32>(_k.x,_k.y,((f32(((_C<<bitcast<u32>(8u))|255u))*0.000000059604645f)+0.000000029802322f),_k.w);return;}@vertex fn main(@builtin(vertex_index) _w:u32,@location(2) AC:vec4<f32>,@location(0) PC:vec2<f32>,@location(4) PB:vec4<f32>,@location(1) QC:vec2<f32>,@location(9) IC:vec4<f32>,@location(6) CC:u32,@location(3) UB:vec4<f32>,@location(8) MC:u32,@location(5) BC:u32,@location(7) DC:u32)->VertexOutput{_p=i32(_w);AC_1=AC;PC_1=PC;PB_1=PB;QC_1=QC;IC_1=IC;CC_1=CC;UB_1=UB;MC_1=MC;BC_1=BC;DC_1=DC;_r();let _B=_a._i;let _y=_a._b;let _v=S5_;let _D=f4_;let _z=Y1_;let _F=N1_;return VertexOutput(_B,_y,_v,_D,_z,_F);})WGSL",
    .usedOverrides = {{true, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_image_mesh.webgpu_vert",
};
} // namespace wgsl
