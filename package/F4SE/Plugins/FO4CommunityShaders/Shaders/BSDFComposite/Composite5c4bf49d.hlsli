// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.221 Shaders011.fxp offset 11570680.
// Native DXBC: SHA-1 5c4bf49dced74855109669b344bfeb208ff4b2b4, 320 bytes, ps_5_0.
// fxc 10.0.26100.0 /T ps_5_0 /E main /O3 /Qstrip_reflect reproduces it byte-for-byte.

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 secondary : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    output.color = 0.0;
    output.secondary = 0.0;
    return output;
}
