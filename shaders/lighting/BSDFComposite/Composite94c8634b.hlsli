// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.221 Shaders011.fxp offset 11570348.
// Native DXBC: SHA-1 94c8634be6709fdba723d668852a2deadfa48ddd, 284 bytes, ps_5_0.
// fxc 10.0.26100.0 /T ps_5_0 /E main /O3 /Qstrip_reflect reproduces it byte-for-byte.

struct PS_INPUT
{
    float4 position : SV_POSITION;
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
