// SPDX-License-Identifier: GPL-3.0-only
float4 main(uint vertex : SV_VertexID) : SV_Position
{
	float2 uv = float2((vertex << 1) & 2, vertex & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
