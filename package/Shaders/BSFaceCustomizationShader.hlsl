// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.240, shaders011.fxp ordinal 3581.

cbuffer FaceVertexConstants : register(b2)
{
    float4 ViewportTransform;
}

void main(
    float4 position : POSITION0,
    out float4 clipPosition : SV_POSITION0,
    out float4 unusedPosition : POSITION0)
{
    clipPosition.xy =
        position.xy * ViewportTransform.xy + ViewportTransform.zw;
    clipPosition.z = position.z;
    clipPosition.w = 1.0;
}
