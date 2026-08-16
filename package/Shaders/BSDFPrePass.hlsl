// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_29[30];
    float4 cb12_idx30_global_fade;

    float4 PrevFrame_WorldToClip_row0;
    float4 PrevFrame_WorldToClip_row1;
    float4 PrevFrame_WorldToClip_row2;
    float4 PrevFrame_WorldToClip_row3;

    float4 cb12_pad_35_36[2];

    float4 CurrFrame_WorldToClip_row0;
    float4 CurrFrame_WorldToClip_row1;
    float4 CurrFrame_WorldToClip_row2;
    float4 CurrFrame_WorldToClip_row3;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_idx0_scroll_anchor_and_alpha;
    float4 cb2_idx1_specular_tint;
    float4 cb2_idx2_scroll_delta;
    float4 cb2_pad_3;
    float4 cb2_idx4_material_flags;
    float4 cb2_idx5_material_id_and_smoothness;
};

Texture2D<float4> g_tAlbedo : register(t0);
Texture2D<float4> g_tNormalMap : register(t1);
Texture2D<float4> g_tMaterial : register(t2);

SamplerState g_sAlbedo : register(s0);
SamplerState g_sNormalMap : register(s1);
SamplerState g_sMaterial : register(s2);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 curr_pos_u : TEXCOORD3;
    float4 prev_pos_v : TEXCOORD4;
    uint isFrontFace : SV_IsFrontFace;
};

struct PS_OUTPUT
{
    float4 albedo : SV_Target0;
    float2 normalOct : SV_Target1;
    float4 material : SV_Target2;
    float4 auxA : SV_Target3;
    float3 specTint : SV_Target4;
    float2 motionVec : SV_Target5;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    output.albedo.w = cb2_idx0_scroll_anchor_and_alpha.z;

    bool bypassFade = (cb2_idx4_material_flags.w == -1.0);
    float alphaFade = bypassFade
        ? 1.0
        : (1.0 - cb2_idx4_material_flags.w * cb12_idx30_global_fade.x);

    float2 uv = float2(input.curr_pos_u.w, input.prev_pos_v.w);
    float4 albedoSample = g_tAlbedo.Sample(g_sAlbedo, uv);
    output.albedo.xyz = albedoSample.xyz * alphaFade;

    float3 nGeom = normalize(input.normal);
    float4 normalMapSample = g_tNormalMap.Sample(g_sNormalMap, uv);
    float4 materialSample = g_tMaterial.Sample(g_sMaterial, uv);

    float2 nts_xy = normalMapSample.xy * 2.0 - 1.0;
    float nts_xy_lenSq = saturate(dot(nts_xy, nts_xy));
    float nts_z = sqrt(1.0 - nts_xy_lenSq);
    nts_z = (input.isFrontFace != 0) ? nts_z : -nts_z;
    float3 nts = float3(nts_xy, nts_z);

    float3 tNorm = normalize(input.tangent);
    float3 bNorm = normalize(input.bitangent);
    float axisX = dot(tNorm, nts);
    float axisY = dot(bNorm, nts);
    float axisZ = min(dot(nGeom, nts), 0.0);
    float3 nWorldFromTBN = normalize(float3(axisX, axisY, axisZ));

    float octZ = sqrt(nWorldFromTBN.z * -8.0 + 8.0);
    output.normalOct = nWorldFromTBN.xy / octZ.xx + 0.5;

    float matSpan = cb2_idx5_material_id_and_smoothness.w
                  - cb2_idx5_material_id_and_smoothness.z;
    float matGate = (cb2_idx5_material_id_and_smoothness.w < 0.0)
                  ? 0.0
                  : cb12_idx30_global_fade.x;
    float matVal = cb2_idx5_material_id_and_smoothness.y
                 ? (matGate * matSpan + cb2_idx5_material_id_and_smoothness.z)
                 : (matGate * cb2_idx5_material_id_and_smoothness.w);
    output.material.z = sqrt(matVal * 0.02);

    bool bitB = cb2_idx4_material_flags.x;
    bool bitA = cb2_idx4_material_flags.y && cb12_idx30_global_fade.x;
    output.material.x = (bitB || bitA) ? 1.0 : 0.0;

    output.material.y = cb2_idx5_material_id_and_smoothness.x * (1.0 / 255.0);
    output.material.w = saturate(cb2_idx5_material_id_and_smoothness.x);

    float matZxFade = cb12_idx30_global_fade.x
                    * cb2_idx4_material_flags.z;
    float matZxComp = mad(
        -cb2_idx4_material_flags.z,
        cb12_idx30_global_fade.x,
        1.0);
    float lerpedMatX = materialSample.x * matZxComp + matZxFade;

    float2 anchor = cb2_idx0_scroll_anchor_and_alpha.xy;
    float2 target = cb2_idx2_scroll_delta.xy;
    float2 delta = target - anchor;
    float2 svRaw = cb12_idx30_global_fade.xx * delta + anchor;
    float2 sval = svRaw * anchor;
    sval = (target >= 0.0) ? sval : anchor;

    output.auxA.x = materialSample.y * sval.x;
    output.auxA.y = lerpedMatX * sval.y;
    output.auxA.z = cb2_idx0_scroll_anchor_and_alpha.w * 0.01;
    output.auxA.w = 1.0;

    output.specTint = cb2_idx1_specular_tint.xyz;

    float4 currWorld = float4(input.curr_pos_u.xyz, 1.0);
    float currClipX = dot(CurrFrame_WorldToClip_row0, currWorld);
    float currClipY = dot(CurrFrame_WorldToClip_row1, currWorld);
    float currClipW = dot(CurrFrame_WorldToClip_row3, currWorld);
    float2 currNDC = float2(currClipX, currClipY) / currClipW.xx;

    float4 prevWorld = float4(input.prev_pos_v.xyz, 1.0);
    float prevClipX = dot(PrevFrame_WorldToClip_row0, prevWorld);
    float prevClipY = dot(PrevFrame_WorldToClip_row1, prevWorld);
    float prevClipW = dot(PrevFrame_WorldToClip_row3, prevWorld);
    float2 prevNDC = float2(prevClipX, prevClipY) / prevClipW.xx;

    output.motionVec = (currNDC - prevNDC) * float2(-0.5, 0.5);
    return output;
}
