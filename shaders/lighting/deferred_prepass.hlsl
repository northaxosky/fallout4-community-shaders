// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// FO4 deferred G-buffer prepass PS, standard opaque variant (runtime c493970c042c..., eid 13220 in FO4_frame9483).
// Status: reference asm transcription; host DrawWorld::DeferredPrePass REL::ID {OG=56596, NG=2318301, AE=2318301}.
// Flow: sample albedo/normal/material, apply alpha fade, encode octa normal, pack material data, pass aux/spec tint, and emit motion vectors from prev/current W2C rows.
// MRTs: albedo, oct normal, material data, aux A/B, and motion vectors.
// Limits: cb2 semantics and alternate skin/hair/decal/projected/two-sided/alpha-test permutations need cross-read.

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..29]: not read directly by this PS. Slots [0..19] follow the
    //          ViewRotation / Projection / PrevFrame_ViewProj / ViewToWorld
    //          / WorldToView / DepthReciprocal schema documented in the
    //          sibling reconstructions; [20..29] include the shared
    //          reprojection matrices used by composite, ambient/IBL,
    //          VLS slice, and BSDFLightShader-directional.
    float4 cb12_pad_0_29[30];

    // [30]: .x = global per-frame alpha-fade / material modulator.
    //       Read at insns 2, 38, 45, 52, 53, 56 as the per-frame
    //       scalar that gates the alpha-fade and material-id branches.
    //       Likely AlphaFadeAmount or a global LOD-fade. TODO: identify.
    float4 cb12_idx30_global_fade;

    // [31..34]: Previous-frame world-to-clip matrix. Only rows 0/1/3
    //           are read (insns 73, 74, 75 dp4 with v5.xyz1). Row 2 is
    //           skipped because motion-vector reconstruction only needs
    //           NDC.xy and W. PrevFrame_WorldToClip per the cross-frame
    //           reprojection schema shared with composite / ambient.
    float4 PrevFrame_WorldToClip_row0;
    float4 PrevFrame_WorldToClip_row1;
    float4 PrevFrame_WorldToClip_row2;  // unused by this PS
    float4 PrevFrame_WorldToClip_row3;

    // [35..36]: not read by this PS. Likely TAA jitter / sub-pixel
    //           offsets reserved for sibling passes. TODO: identify.
    float4 cb12_pad_35_36[2];

    // [37..40]: Current-frame world-to-clip matrix. Only rows 0/1/3
    //           are read (insns 67, 68, 69 dp4 with v4.xyz1). Same
    //           pattern as the prev-frame matrix.
    float4 CurrFrame_WorldToClip_row0;
    float4 CurrFrame_WorldToClip_row1;
    float4 CurrFrame_WorldToClip_row2;  // unused by this PS
    float4 CurrFrame_WorldToClip_row3;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: .x, .y = scroll-UV anchor coords; .z = alpha-test constant
    //      written verbatim to o0.w (insn 0); .w = depth/fog factor
    //      scaled by 0.01 into o3.z (insn 62).
    float4 cb2_idx0_scroll_anchor_and_alpha;

    // [1]: .xyz = specular tint / emissive constant, copied straight
    //      to o4 (insn 64). .w unused by this PS.
    float4 cb2_idx1_specular_tint;

    // [2]: .xy = scroll-UV delta vs cb2[0].xy (insns 55-57); used
    //      to compute o3.xy with the cb12[30].x global modulator.
    float4 cb2_idx2_scroll_delta;

    // [3]: not read by this PS.
    float4 cb2_pad_3;

    // [4]: .x, .y, .z, .w material/alpha controls.
    //      .w = -1.0 sentinel test (insn 1) to bypass cb12[30].x fade;
    //      .z = secondary alpha modulator (insns 52, 53);
    //      .x, .y = nonzero tests for material-id flag (insn 46).
    //      Likely (MaterialFlags, AlphaOverride).
    float4 cb2_idx4_material_flags;

    // [5]: .x = material-id (0..255 byte packed as float, output to
    //      o2.y via *(1/255) and to o2.w via saturate); .y = nonzero
    //      flag for o2.x branch (insn 41); .z, .w = smoothness range
    //      (insns 36-40) packed into o2.z via sqrt(* 0.02).
    float4 cb2_idx5_material_id_and_smoothness;
};

// Resource bindings. Slot indices match the asm dcls exactly.

// t0: albedo / diffuse texture. Sampled at insn 6 by the per-pixel UV
//     packed as (v4.w, v5.w).
Texture2D<float4> g_tAlbedo   : register(t0);

// t1: tangent-space normal map. Sampled at insn 11; .xy channels hold
//     the encoded tangent-space xy (corpus sample t1.zwxy -> r1.zw packs
//     t1.x/t1.y into r1.z/r1.w, so the decoded source channels are .xy).
Texture2D<float4> g_tNormalMap : register(t1);

// t2: material data texture. Sampled at insn 12; .xy channels hold
//     smoothness + mask.
Texture2D<float4> g_tMaterial : register(t2);

SamplerState g_sAlbedo   : register(s0);
SamplerState g_sNormalMap : register(s1);
SamplerState g_sMaterial : register(s2);

// Entry point.

struct PS_INPUT
{
    float4 position    : SV_POSITION;       // v0 - position (unused body)
    float3 tangent     : TEXCOORD0;         // v1 - world-space tangent
    float3 bitangent   : TEXCOORD1;         // v2 - world-space bitangent
    float3 normal      : TEXCOORD2;         // v3 - world-space normal
    float4 curr_pos_u  : TEXCOORD3;         // v4 - curr world-pos + U
    float4 prev_pos_v  : TEXCOORD4;         // v5 - prev world-pos + V
    uint   isFrontFace : SV_IsFrontFace;    // v6 - is_front_face flag
};

struct PS_OUTPUT
{
    float4 albedo     : SV_Target0;  // o0 -> kGbufferAlbedo
    float2 normalOct  : SV_Target1;  // o1 -> kGbufferNormal
    float4 material   : SV_Target2;  // o2 -> kGbufferMaterial
    float4 auxA       : SV_Target3;  // o3 -> aux G-buffer A
    float3 specTint   : SV_Target4;  // o4 -> aux G-buffer B (spec tint)
    float2 motionVec  : SV_Target5;  // o5 -> kGbufferMotionVector
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    // Insns 0-7: alpha-fade modulator + albedo sample + o0 write.
    output.albedo.w = cb2_idx0_scroll_anchor_and_alpha.z;

    // cb2[4].w == -1.0 bypasses the fade; otherwise fade = 1 - cb2[4].w * cb12[30].x.
    bool bypassFade = (cb2_idx4_material_flags.w == -1.0);
    float alphaFade = bypassFade
        ? 1.0
        : (1.0 - cb2_idx4_material_flags.w * cb12_idx30_global_fade.x);

    float2 uv = float2(input.curr_pos_u.w, input.prev_pos_v.w);
    float4 albedoSample = g_tAlbedo.Sample(g_sAlbedo, uv);
    output.albedo.xyz = alphaFade * albedoSample.xyz;

    // Insns 8-10: normalize the interpolated world-space normal (v3).
    float3 nGeom = normalize(input.normal);

    // Insns 11-12: sample normal-map (.xy) and material (.xy).
    float4 normalMapSample = g_tNormalMap.Sample(g_sNormalMap, uv);
    float4 materialSample  = g_tMaterial.Sample(g_sMaterial, uv);

    // Insns 13-18: decode tangent-space normal from 2-channel encoding.
    float2 nts_xy = normalMapSample.xy * 2.0 - 1.0;
    float  nts_xy_lenSq = saturate(dot(nts_xy, nts_xy));
    float  nts_z = sqrt(1.0 - nts_xy_lenSq);
    nts_z = (input.isFrontFace != 0) ? nts_z : -nts_z;
    float3 nts = float3(nts_xy, nts_z);

    // Insns 19-31: build world-space perturbed normal via tangent-frame.
    //   r3 = normalize(tangent);  axisX = dot(r3, nts)  -> r0.x
    //   r3 = normalize(bitangent); axisY = dot(r3, nts) -> r0.y
    //   axisZ_capped = min(dot(nGeom, nts), 0)          -> r0.z
    //   r0.xyz = (axisX, axisY, axisZ_capped); normalize -> r0.xyz
    float3 tNorm = normalize(input.tangent);
    float3 bNorm = normalize(input.bitangent);
    float  axisX = dot(tNorm, nts);
    float  axisY = dot(bNorm, nts);
    float  axisZ = min(dot(nGeom, nts), 0.0);
    float3 nWorldFromTBN = normalize(float3(axisX, axisY, axisZ));

    // Insns 32-35: octahedral re-encode for o1.xy.
    //   z' = sqrt(z * -8 + 8)
    //   uv = xy / z'
    //   o1.xy = uv + 0.5
    float  octZ = sqrt(nWorldFromTBN.z * -8.0 + 8.0);
    output.normalOct = nWorldFromTBN.xy / octZ.xx + 0.5;

    // Insns 36-44: material smoothness pack into o2.z.
    //   span = cb2[5].w - cb2[5].z
    //   gate = (cb2[5].w < 0) ? 0 : cb12[30].x
    //   val  = (cb2[5].y != 0) ? (gate * span + cb2[5].z) : (gate * cb2[5].w)
    //   o2.z = sqrt(val * 0.02)
    float matSpan = cb2_idx5_material_id_and_smoothness.w
                  - cb2_idx5_material_id_and_smoothness.z;
    float matGate = (cb2_idx5_material_id_and_smoothness.w < 0.0)
                  ? 0.0
                  : cb12_idx30_global_fade.x;
    float matVal  = (cb2_idx5_material_id_and_smoothness.y != 0.0)
                  ? (matGate * matSpan + cb2_idx5_material_id_and_smoothness.z)
                  : (matGate * cb2_idx5_material_id_and_smoothness.w);
    output.material.z = sqrt(matVal * 0.02);

    // Insns 45-49: material-id flag bit packing into o2.x.
    //   bitA = (cb12[30].x != 0) & (cb2[4].y != 0)
    //   bitB = (cb2[4].x != 0)
    //   o2.x = (bitA | bitB) ? 1.0 : 0.0
    bool bitA = (cb12_idx30_global_fade.x != 0.0)
             && (cb2_idx4_material_flags.y != 0.0);
    bool bitB = (cb2_idx4_material_flags.x != 0.0);
    output.material.x = (bitA || bitB) ? 1.0 : 0.0;

    // Insns 50-51: material-id index + saturate flag.
    output.material.y = cb2_idx5_material_id_and_smoothness.x * (1.0 / 255.0);
    output.material.w = saturate(cb2_idx5_material_id_and_smoothness.x);

    // Insns 52-63: o3 packed data (scrolling UV + secondary depth + 1).
    //   r0.x = cb12[30].x * cb2[4].z
    //   r0.y = -cb2[4].z * cb12[30].x + 1
    //   r0.x = r1.x * r0.y + r0.x      // (material sample.x lerp to 1)
    float matZxFade = cb12_idx30_global_fade.x
                    * cb2_idx4_material_flags.z;
    float matZxComp = 1.0 - cb12_idx30_global_fade.x
                          * cb2_idx4_material_flags.z;
    float lerpedMatX = materialSample.x * matZxComp + matZxFade;

    //   delta = cb2[2].xy - cb2[0].xy
    //   sval  = cb12[30].x * delta + cb2[0].xy
    //   sval  = sval * cb2[0].xy
    //   sval  = (cb2[2].xy >= 0) ? sval : cb2[0].xy
    float2 anchor = cb2_idx0_scroll_anchor_and_alpha.xy;
    float2 target = cb2_idx2_scroll_delta.xy;
    float2 delta  = target - anchor;
    float2 svRaw  = cb12_idx30_global_fade.xx * delta + anchor;
    float2 sval   = svRaw * anchor;
    sval = (target >= 0.0) ? sval : anchor;

    //   o3.x = sval.x * materialSample.y
    //   o3.y = sval.y * lerpedMatX
    output.auxA.x = sval.x * materialSample.y;
    output.auxA.y = sval.y * lerpedMatX;
    output.auxA.z = cb2_idx0_scroll_anchor_and_alpha.w * 0.01;
    output.auxA.w = 1.0;

    // Insn 64: spec tint passthrough.
    output.specTint = cb2_idx1_specular_tint.xyz;

    // Insns 65-78: motion vector reconstruction.
    //   currNDC = curr_pos transformed by CB12[37..40] (rows 0/1/3),
    //   prevNDC = prev_pos transformed by CB12[31..34] (rows 0/1/3),
    //   o5.xy = (currNDC - prevNDC) * (-0.5, 0.5)
    float4 currWorld = float4(input.curr_pos_u.xyz, 1.0);
    float currClipX = dot(CurrFrame_WorldToClip_row0, currWorld);
    float currClipY = dot(CurrFrame_WorldToClip_row1, currWorld);
    float currClipW = dot(CurrFrame_WorldToClip_row3, currWorld);
    float2 currNDC  = float2(currClipX, currClipY) / currClipW.xx;

    float4 prevWorld = float4(input.prev_pos_v.xyz, 1.0);
    float prevClipX = dot(PrevFrame_WorldToClip_row0, prevWorld);
    float prevClipY = dot(PrevFrame_WorldToClip_row1, prevWorld);
    float prevClipW = dot(PrevFrame_WorldToClip_row3, prevWorld);
    float2 prevNDC  = float2(prevClipX, prevClipY) / prevClipW.xx;

    output.motionVec = (currNDC - prevNDC) * float2(-0.5, 0.5);

    return output;
}

// Round-trip notes (for the reviewer + future maintainer)
// fxc /T ps_5_0 /O3 /E main against this file produces a dxbc whose
// insn count is compared against the original 80 insns of eid 13220.
// See local roundtrip notes for the up-to-date compile delta.
// What is faithfully reconstructed (structurally):
//   * Resource declarations (3 SRVs + 3 default samplers + 2 CBs) at
//     exact slot indices.
//   * Input + output signatures (SV_POSITION unused-body; 5 TEXCOORD
//     interpolants + SV_IsFrontFace; 6 MRT outputs with exact channel
//     masks .xyzw / .xy / .xyzw / .xyzw / .xyz / .xy).
//   * Major control flow: alpha-fade modulator branch, tangent-frame
//     normal-map decode + re-encode to octahedral, material-id flag
//     packing, scrolling UV pair via cb12[30] lerp, current/previous
//     frame world-to-clip dp4 + perspective-divide for motion vector.
//   * cb12[31..34] / cb12[37..40] only-rows-0-1-3 read pattern.
// What is approximated rather than asm-exact:
//   * The `min(NdotN, 0)` axisZ clamp at insn 20 is intentional in the
//     asm (the world-space normal must point INTO the surface for the
//     octahedral lower-hemisphere encode to match the engine's normal
//     orientation convention). This may look like a bug but matches asm.
//   * cb2[4].w == -1.0 sentinel is implemented as `==` equality on
//     float; fxc may emit it as a bitwise compare. The semantic is
//     identical.
// What needs cross-read to finalize:
//   * cb12[30].x exact semantic (likely AlphaFadeAmount or LOD fade).
//   * cb2[4] / cb2[5] field-by-field semantics. The runtime
//     CB2 dump from a single dispatch could lock the names; queued.
//   * Per-permutation deltas vs eids 13241/13259/13507/13546/31244/
//     38343/39205 (skin/hair/decal/projected/two-sided/alpha-test).
