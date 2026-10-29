#pragma once

#include <DirectXMath.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace cs::features::light_limit_fix
{
	enum class LightFlags : std::uint32_t
	{
		kPortalStrict = 1 << 0,
		kShadow = 1 << 1,
		kSimple = 1 << 2,
		kShadowCaster = 1 << 3,
		kLocalShadow = 1 << 4,
		kInitialised = 1 << 8,
		kDisabled = 1 << 9,
		kInverseSquare = 1 << 10,
		kLinear = 1 << 11
	};

	struct PositionOpt
	{
		DirectX::XMFLOAT3 data{};
		std::uint32_t pad0 = 0;
	};

	// Mirrors LightLimitFix::Light in the shared Common.hlsli.
	struct alignas(16) LightData
	{
		DirectX::XMFLOAT3 color{};
		float fade = 1.0f;
		float radius = 0;
		float invRadius = 0;
		float fadeZone = 0;
		float sizeBias = 0;
		PositionOpt positionWS;
		std::array<std::uint32_t, 4> roomFlags{};
		std::uint32_t lightFlags = 0;
		std::uint32_t shadowMaskIndex = 0;
		std::uint32_t localShadowIndex = 0;
		std::uint32_t pad1 = 0;
	};
	static_assert(sizeof(LightData) == 80);
	static_assert(offsetof(LightData, fade) == 12);
	static_assert(offsetof(LightData, radius) == 16);
	static_assert(offsetof(LightData, invRadius) == 20);
	static_assert(offsetof(LightData, fadeZone) == 24);
	static_assert(offsetof(LightData, sizeBias) == 28);
	static_assert(offsetof(LightData, positionWS) == 32);
	static_assert(offsetof(LightData, roomFlags) == 48);
	static_assert(offsetof(LightData, lightFlags) == 64);
	static_assert(offsetof(LightData, shadowMaskIndex) == 68);
	static_assert(offsetof(LightData, localShadowIndex) == 72);
	static_assert(offsetof(LightData, pad1) == 76);

	// Mirrors cbuffer StrictLightData (b3) in LightLimitFix.hlsli.
	struct alignas(16) StrictLightDataCB
	{
		std::uint32_t numStrictLights = 0;
		std::int32_t roomIndex = -1;
		std::uint32_t shadowBitMask = 0;
		std::uint32_t firstPerson = 0;
		DirectX::XMFLOAT4 worldEyePosition{};
		std::array<LightData, 15> strictLights{};
	};
	static_assert(sizeof(StrictLightDataCB) == 32 + 15 * sizeof(LightData));
	static_assert(offsetof(StrictLightDataCB, roomIndex) == 4);
	static_assert(offsetof(StrictLightDataCB, worldEyePosition) == 16);
	static_assert(offsetof(StrictLightDataCB, strictLights) == 32);

	// FO4: spot cone beside each gathered light; exponent 0 means no cone.
	struct alignas(16) ConeData
	{
		DirectX::XMFLOAT3 direction{};
		float cosHalfAngle = 0;
		float exponent = 0;
		std::array<std::uint32_t, 3> pad{};
	};
	static_assert(sizeof(ConeData) == 32);
	static_assert(offsetof(ConeData, cosHalfAngle) == 12);
	static_assert(offsetof(ConeData, exponent) == 16);

	inline constexpr std::uint32_t kClusterMaxLights = 128;

	// Mirrors of the cluster compute shader structs and PerFrame CBs.
	struct ClusterAABB
	{
		DirectX::XMFLOAT4 minPoint, maxPoint;
	};
	struct LightGrid
	{
		std::uint32_t offset, lightCount, pad0[2];
	};
	struct LightBuildingCB
	{
		float LightsNear, LightsFar;
		std::uint32_t pad0[2];
		std::uint32_t ClusterSize[4];
	};
	struct LightCullingCB
	{
		std::uint32_t LightCount, pad0[3];
		std::uint32_t ClusterSize[4];
	};
	static_assert(sizeof(ClusterAABB) == 32);
	static_assert(sizeof(LightGrid) == 16);
	static_assert(sizeof(LightBuildingCB) == 32);
	static_assert(sizeof(LightCullingCB) == 32);
}
