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

inline constexpr Shader draw_depthstencil_image_mesh_webgpu_noclipdistance_vert = {
    .source = R"WGSL(
struct WB{Fd:f32,hf:f32,Sg:f32,Tg:f32,W6_:u32,Fa:u32,Eg:u32,Fg:u32,J8_:vec4<i32>,Ti:vec2<f32>,jf:vec2<f32>,q2_:u32,ni:f32,z6_:u32,i3_:f32,De:f32,xg:u32,H3_:f32,I3_:f32,ye:f32,Qi:u32,Ea:u32,Q8_:f32,R8_:f32,}struct _f{@builtin(position) _d:vec4<f32>,_A:f32,_C:array<f32,1>,_B:array<f32,1>,}struct VertexOutput{@location(0) member:vec2<f32>,@location(1)@interpolate(flat,either) member_1:f32,@location(3)@interpolate(flat,either) member_2:vec4<f32>,@location(4)@interpolate(flat,either) member_3:u32,@builtin(position) _d:vec4<f32>,}@id(0) override qj:bool=true;var<private>_y:i32;var<private>AC_1:vec4<f32>;var<private>PC_1:vec2<f32>;var<private>PB_1:vec4<f32>;var<private>S5_:vec2<f32>;var<private>QC_1:vec2<f32>;var<private>IC_1:vec4<f32>;var<private>f4_:f32;var<private>CC_1:u32;@group(0)@binding(0) var<uniform>j:WB;var<private>UB_1:vec4<f32>;var<private>MC_1:u32;var<private>Y1_:vec4<f32>;var<private>BC_1:u32;var<private>N1_:u32;var<private>DC_1:u32;var<private>_i:_f=_f(vec4<f32>(0f,0f,0f,1f),1f,array<f32,1>(),array<f32,1>());fn _s(){var _c:f32;let _a=AC_1;let _w=PC_1;let _v=PB_1;let _g=((mat2x2<f32>(vec2<f32>(_a.x,_a.y),vec2<f32>(_a.z,_a.w))*_w)+_v.xy);let _x=QC_1;let _h=IC_1;S5_=((_x*_h.zw)+_h.xy);if qj{let _j=CC_1;let _m=j.z6_;if (_j==0u){_c=0f;}else{_c=unpack2x16float(((_j+1023u)*_m)).x;}let _k=_c;f4_=_k;}let _z=j.Sg;let _e=j.Tg;let _b=vec4<f32>(((_g.x*_z)-1f),((_g.y*_e)-sign(_e)),0f,1f);let _p=MC_1;let _q=BC_1;Y1_=unpack4x8unorm(_q);let _r=DC_1;N1_=_r;_i._d=vec4<f32>(_b.x,_b.y,((f32(((_p<<bitcast<u32>(8u))|255u))*0.000000059604645f)+0.000000029802322f),_b.w);return;}@vertex fn main(@builtin(vertex_index) _u:u32,@location(2) AC:vec4<f32>,@location(0) PC:vec2<f32>,@location(4) PB:vec4<f32>,@location(1) QC:vec2<f32>,@location(9) IC:vec4<f32>,@location(6) CC:u32,@location(3) UB:vec4<f32>,@location(8) MC:u32,@location(5) BC:u32,@location(7) DC:u32)->VertexOutput{_y=i32(_u);AC_1=AC;PC_1=PC;PB_1=PB;QC_1=QC;IC_1=IC;CC_1=CC;UB_1=UB;MC_1=MC;BC_1=BC;DC_1=DC;_s();let _n=S5_;let _o=f4_;let _a=Y1_;let _l=N1_;let _t=_i._d;return VertexOutput(_n,_o,_a,_l,_t);})WGSL",
    .usedOverrides = {{true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}},
    .label = "draw_depthstencil_image_mesh.webgpu_noclipdistance_vert",
};
} // namespace wgsl
