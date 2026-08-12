// Generalized source for the CB0-absent / cube-t8 / no-t0 deferred-composite family.
//
//   b3730e0e : (no defines)         CB2[3], CB12[47], t1-t7 + t8 cubearray, s1-s8
//   fbdcf44d : OUTPUTMASK=1        CB2[6], CB12[47], t1-t7 + t8 cubearray + t9, s1-s9
//
// OUTPUTMASK widens CB2 from three to six registers and adds t9/s9.

#ifndef OUTPUTMASK
#define OUTPUTMASK 0
#endif

cbuffer PerFrame : register(b12)
{
    float4 g_PF[47];      // cb12[0..46]
};

cbuffer PerPass : register(b2)
{
    float4 g_PixelToUV;   // cb2[0]  .xy pixel->uv   .zw uv->[0,1]
    float4 g_DirAndScale; // cb2[1]  .xyz direction  .w scale
    float4 g_TintAndExp;  // cb2[2]  .xyz tint       .w exponent
#if OUTPUTMASK
    float4 g_Unused3;     // cb2[3]  (never read)
    float4 g_Unused4;     // cb2[4]  (never read)
    float4 g_UVClamp;     // cb2[5]  .xy  read only when OUTPUTMASK
#endif
};

Texture2D<float4>        TexNormal    : register(t1);
Texture2D<float4>        TexParam     : register(t2);
Texture2D<float4>        TexSurface   : register(t3);
Texture2D<float4>        TexA         : register(t4);
Texture2D<float4>        TexB         : register(t5);
Texture2D<float4>        TexC         : register(t6);
Texture2D<float4>        TexDepth     : register(t7);
TextureCubeArray<float4> TexCube      : register(t8);

SamplerState SampNormal  : register(s1);
SamplerState SampParam   : register(s2);
SamplerState SampSurface : register(s3);
SamplerState SampA       : register(s4);
SamplerState SampB       : register(s5);
SamplerState SampC       : register(s6);
SamplerState SampDepth   : register(s7);
SamplerState SampCube    : register(s8);

#if OUTPUTMASK
Texture2D<float4> TexMask  : register(t9);
SamplerState      SampMask : register(s9);
#endif

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    float2 uv = svpos.xy * g_PixelToUV.xy;

    float3 surf  = TexSurface.SampleLevel(SampSurface, uv, 0).xyw;
    float  depth = TexDepth.SampleLevel(SampDepth, uv, 0).x;

    float4 pos;
    float4 m0, m1, m2, m3;
    if (depth <= 0.01)
    {
        pos.z = depth * 100.0;
        m0 = g_PF[24]; m1 = g_PF[25]; m2 = g_PF[26]; m3 = g_PF[27];
    }
    else
    {
        pos.z = depth * 1.01 - 0.01;
        m0 = g_PF[20]; m1 = g_PF[21]; m2 = g_PF[22]; m3 = g_PF[23];
    }

    float2 sn = float2(uv.x * g_PixelToUV.z, 1.0 - uv.y * g_PixelToUV.w);
    pos.xy = sn * 2.0 - 1.0;
    pos.w  = 1.0;

    float3 pxyz = float3(dot(m0, pos), dot(m1, pos), dot(m2, pos));
    float  pw   = dot(m3, pos);
    pos.xyz = pxyz / pw;

    float2 prm = TexParam.SampleLevel(SampParam, uv, 0).yz;

    float3 cube = 0.0;
    if (prm.x > 0.5 / 255.0)                     // 0x3B008081; NOT the printed 0.001961
    {
        float4 nn;
        nn.xy = TexNormal.SampleLevel(SampNormal, uv, 0).xy * 4.0 - 2.0;
        float f = dot(nn.xy, nn.xy);
        nn.zw = 1.0 - f * float2(0.25, 0.5);
        nn.xy = nn.xy * sqrt(nn.z);
        nn.z  = -nn.w;

        float3 v   = normalize(-pos.xyz);
        float  ndv = dot(v, nn.xyz);
        ndv = ndv + ndv;
        float3 r = nn.xyz * -ndv + v;

        float3 rw = float3(dot(g_PF[12].xyz, r),
                           dot(g_PF[13].xyz, r),
                           dot(g_PF[14].xyz, r));

        float lod = (1.0 - surf.x) * 6.0;
        lod = pos.z * 0.001953125 + lod;         // 0x3B000000 = 2^-9; NOT the printed 0.001953
        float idx = floor(prm.x * 255.0 - 1.0);

        cube = TexCube.SampleLevel(SampCube, float4(rw, idx), lod).xyz;
        float lum = dot(cube, float3(0.299, 0.587, 0.114));
        cube = lerp(cube, lum.xxx, g_PF[30].y * 0.9);
    }

    float4 result;

    float2 idt = surf.z * 255.0 - float2(2.0, 3.0);
    bool2  hit = abs(idt) < 0.25;
    if (!(hit.x || hit.y))
    {
        float3 b   = TexB.SampleLevel(SampB, uv, 0).xyz;
        float3 b3  = b * 3.0;
        float3 a   = TexA.Sample(SampA, uv).xyz;
        float3 c   = TexC.SampleLevel(SampC, uv, 0).xyz;
        float3 col = c + a;
        col = b * 1.5 + col;

        float k  = surf.y * 3.0;
        float s  = min(1.0 / rsqrt(saturate(surf.x - 0.3)), 1.0);
        k = k * s;
        float g2 = (prm.y * prm.y) * 50.0;
        col = ((cube * k) * g2) * b3 + col;

#if OUTPUTMASK
        float mask = TexMask.Sample(SampMask, min(uv, g_UVClamp.xy)).x;
        col = col * mask;
#endif

        pos.w = 1.0;
        float  h    = dot(g_PF[14], pos) + g_PF[35].z;
        float  dd   = dot(pos.xyz, pos.xyz);
        float  dist = sqrt(dd) * g_PF[41].x - g_PF[41].z;
        float  ds   = saturate(dist);
        float2 hh   = saturate(h * g_PF[46].xy - g_PF[46].zw);
        float  fogH = ds * (hh.y - hh.x) + hh.x;

        float w43 = g_PF[43].w;
        float t1v = (0.75 < dist) ? min(((ds - 0.75) * 4.0) * (1.0 - w43) + w43, 1.0) : w43;
        float t2v = (dist < 0.015) ? (ds * 66.666672) : 1.0;
        float fk  = min(pow(ds, g_PF[42].w), t1v);

        float alpha = 1.0 - fogH;
        alpha = fogH * g_PF[44].w + alpha;

        float3 cA   = lerp(g_PF[42].xyz, g_PF[44].xyz, fk);
        float3 cB   = lerp(g_PF[43].xyz, g_PF[45].xyz, fk);
        float3 fogC = lerp(cA, cB, fogH);
        float  amt  = (fk * alpha) * t2v;

        float3 dir = pos.xyz * rsqrt(dd);
        float  sun = pow(max(dot(dir, g_DirAndScale.xyz), 0.0), g_TintAndExp.w) * g_DirAndScale.w;
        fogC = lerp(fogC, g_TintAndExp.xyz, sun);

        if (amt < g_PF[43].w)
        {
            // 0x3EAAAAAB; NOT the printed 0.333333
            float lum2 = dot(col, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
            fogC = lerp(fogC, lum2.xxx, lum2);
        }

        result = float4(lerp(col, fogC, amt), 0.5);
    }
    else
    {
        result = float4(0.0, 0.0, 0.0, 0.0);
    }

    return result;
}
