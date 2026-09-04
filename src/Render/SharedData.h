#pragma once

#include <DirectXMath.h>

#include <cstddef>
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

namespace cs::engine
{
	enum class ShaderStage : std::uint8_t;
}

namespace cs::render
{
	// reserved on every contributed stage
	inline constexpr std::uint32_t kSharedDataSlot = 5;
	inline constexpr std::uint32_t kFeatureDataSlot = 6;
	inline constexpr std::uint32_t kSkylightingDataSlot = 7;
	inline constexpr std::uint32_t kSkylightingTextureSlot = 9;
	inline constexpr std::uint32_t kSkylightingSamplerSlot = 9;
	inline constexpr std::uint32_t kSkylightingComputeTextureSlot = 3;
	inline constexpr std::uint32_t kSkylightingComputeSamplerSlot = 0;
	static_assert(kFeatureDataSlot == kSharedDataSlot + 1);
	static_assert(kSkylightingDataSlot == kFeatureDataSlot + 1);
	static_assert(kSkylightingDataSlot < 8);
	static_assert(kSkylightingComputeTextureSlot < 8);
	static_assert(kSkylightingComputeSamplerSlot < 8);

	struct alignas(16) SkylightingSharedData
	{
		DirectX::XMFLOAT4X4 OcclusionViewProj{};
		DirectX::XMFLOAT4   OcclusionDirection{};
		DirectX::XMFLOAT4   ViewToWorld[3]{};
		DirectX::XMFLOAT4   CameraPosAdjust{};
		float               OcclusionExtent = 0.0f;
		float               MinDiffuseVisibility = 1.0f;
		float               MinSpecularVisibility = 1.0f;
		std::uint32_t       Mode = 0;
	};
	static_assert(sizeof(SkylightingSharedData) == 160);
	static_assert(offsetof(SkylightingSharedData, OcclusionExtent) == 144);
	static_assert(offsetof(SkylightingSharedData, MinDiffuseVisibility) == 148);
	static_assert(offsetof(SkylightingSharedData, MinSpecularVisibility) == 152);
	static_assert(offsetof(SkylightingSharedData, Mode) == 156);

	struct SkylightingSharedDataStatus
	{
		bool          bufferReady = false;
		bool          dataPublished = false;
		bool          srvPublished = false;
		bool          samplerPublished = false;
		bool          boundLastCall = false;
		bool          cameraPublishedLastCall = false;
		std::uint64_t publishCalls = 0;
		std::uint64_t bufferWrites = 0;
		std::uint64_t bindCalls = 0;
		std::uint64_t successfulBinds = 0;
		std::uint64_t pixelBindCalls = 0;
		std::uint64_t pixelSuccessfulBinds = 0;
		std::uint64_t computeBindCalls = 0;
		std::uint64_t computeSuccessfulBinds = 0;
		std::uint64_t computePreviousFrameCameraBinds = 0;
		std::uint64_t rejectedNoBuffer = 0;
		std::uint64_t rejectedNoData = 0;
		std::uint64_t rejectedNoSrv = 0;
		std::uint64_t rejectedNoSampler = 0;
		std::uint64_t rejectedCameraMissing = 0;
		std::uint64_t rejectedCameraStale = 0;
		std::uint64_t rejectedCameraValidation = 0;
	};

	void InitializeSharedData(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	bool IsSharedDataReady() noexcept;

	// startup thread only
	void EnsureSharedDataUpdateInstalled();

	void PublishSkylightingSharedData(
		const SkylightingSharedData& a_data,
		ID3D11ShaderResourceView* a_occlusionSRV,
		ID3D11SamplerState* a_comparisonSampler) noexcept;
	[[nodiscard]] SkylightingSharedDataStatus
		GetSkylightingSharedDataStatus() noexcept;

	void BindSharedData(
		ID3D11DeviceContext* a_context,
		engine::ShaderStage a_stage) noexcept;
}
