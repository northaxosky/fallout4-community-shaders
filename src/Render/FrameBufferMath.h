#pragma once

#include <DirectXMath.h>

#include <cmath>
#include <cstddef>

namespace cs::engine
{
	// Fallout 4 binds its per-frame constant buffer at HLSL register(b12).
	inline constexpr std::size_t kFrameBufferRegisters = 47;
	inline constexpr float kMinimumWorldCameraOriginMagnitude = 1.0f;

	// Field names and padding mirror package/Shaders/Common/DeferredContracts.hlsli and
	// BSDFPrePass.hlsl one-for-one, so the layout can be audited against the HLSL by eye.
	// The trailing comment on each member is its b12 float4 register index.
	struct alignas(16) FrameBuffer
	{
		DirectX::XMFLOAT4 cb12_pad_0_11[12];         // 0-11
		DirectX::XMFLOAT4 ViewToWorld[3];            // 12-14
		DirectX::XMFLOAT4 cb12_pad_15_19[5];         // 15-19
		DirectX::XMFLOAT4 FarReproj[4];              // 20-23
		DirectX::XMFLOAT4 NearReproj[4];             // 24-27
		DirectX::XMFLOAT4 cb12_pad_28_29[2];         // 28-29
		DirectX::XMFLOAT4 IblDesaturation;           // 30
		DirectX::XMFLOAT4 PrevFrameWorldToClip[4];   // 31-34
		DirectX::XMFLOAT4 CameraPosAdjust;           // 35
		DirectX::XMFLOAT4 CameraPreviousPosAdjust;   // 36
		DirectX::XMFLOAT4 CurrFrameWorldToClip[4];   // 37-40
		DirectX::XMFLOAT4 FogDistanceRamp;           // 41
		DirectX::XMFLOAT4 FogNearLowColorAndPower;   // 42
		DirectX::XMFLOAT4 FogNearHighColorAndClamp;  // 43
		DirectX::XMFLOAT4 FogFarLowColorAndDensity;  // 44
		DirectX::XMFLOAT4 FogFarHighColor;           // 45
		DirectX::XMFLOAT4 FogHeightRamp;             // 46
	};

	inline constexpr std::size_t kFrameBufferRegisterSize = sizeof(DirectX::XMFLOAT4);

	static_assert(sizeof(FrameBuffer) == kFrameBufferRegisters * kFrameBufferRegisterSize);
	static_assert(sizeof(FrameBuffer) == 752);
	static_assert(offsetof(FrameBuffer, ViewToWorld) == 12 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, FarReproj) == 20 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, NearReproj) == 24 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, IblDesaturation) == 30 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, PrevFrameWorldToClip) == 31 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, CameraPosAdjust) == 35 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, CameraPreviousPosAdjust) == 36 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, CurrFrameWorldToClip) == 37 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, FogDistanceRamp) == 41 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, FogHeightRamp) == 46 * kFrameBufferRegisterSize);

	[[nodiscard]] inline DirectX::XMFLOAT3 CameraWorldOrigin(
		const FrameBuffer& a_frameBuffer) noexcept
	{
		return {
			a_frameBuffer.ViewToWorld[0].w + a_frameBuffer.CameraPosAdjust.x,
			a_frameBuffer.ViewToWorld[1].w + a_frameBuffer.CameraPosAdjust.y,
			a_frameBuffer.ViewToWorld[2].w + a_frameBuffer.CameraPosAdjust.z
		};
	}

	[[nodiscard]] inline DirectX::XMFLOAT3 CameraPreviousWorldOrigin(
		const FrameBuffer& a_frameBuffer) noexcept
	{
		return {
			a_frameBuffer.CameraPreviousPosAdjust.x,
			a_frameBuffer.CameraPreviousPosAdjust.y,
			a_frameBuffer.CameraPreviousPosAdjust.z
		};
	}

	[[nodiscard]] inline const DirectX::XMFLOAT4& FrameBufferRegister(
		const FrameBuffer& a_frameBuffer,
		std::size_t a_index) noexcept
	{
		return reinterpret_cast<const DirectX::XMFLOAT4*>(&a_frameBuffer)[a_index];
	}

	// Absolute world position, matching BSDFCompositeShader.hlsl: dot each row with
	// float4(view, 1), then add the renderer's position adjustment.
	[[nodiscard]] inline DirectX::XMFLOAT3 ViewToWorldPosition(
		const DirectX::XMFLOAT3& a_viewPosition,
		const DirectX::XMFLOAT4* a_rows,
		const DirectX::XMFLOAT3& a_origin) noexcept
	{
		const auto row = [&a_viewPosition](const DirectX::XMFLOAT4& a_row) noexcept {
			return a_row.x * a_viewPosition.x
				+ a_row.y * a_viewPosition.y
				+ a_row.z * a_viewPosition.z
				+ a_row.w;
		};
		return {
			row(a_rows[0]) + a_origin.x,
			row(a_rows[1]) + a_origin.y,
			row(a_rows[2]) + a_origin.z
		};
	}

	// Directions drop the translation column and are not position-adjusted.
	[[nodiscard]] inline DirectX::XMFLOAT3 ViewToWorldDirection(
		const DirectX::XMFLOAT3& a_direction,
		const DirectX::XMFLOAT4* a_rows) noexcept
	{
		const auto row = [&a_direction](const DirectX::XMFLOAT4& a_row) noexcept {
			return a_row.x * a_direction.x
				+ a_row.y * a_direction.y
				+ a_row.z * a_direction.z;
		};
		DirectX::XMFLOAT3 result{ row(a_rows[0]), row(a_rows[1]), row(a_rows[2]) };
		const float length = std::sqrt(
			result.x * result.x + result.y * result.y + result.z * result.z);
		if (!(length > 1e-8f)) {
			return { 0.0f, 0.0f, 0.0f };
		}
		result.x /= length;
		result.y /= length;
		result.z /= length;
		return result;
	}

	// An orthographic world-to-clip leaves row 3 at (0, 0, 0, 1); shadow cameras are orthographic.
	[[nodiscard]] inline bool IsPerspectiveProjection(
		const DirectX::XMFLOAT4& a_worldToClipRow3) noexcept
	{
		const float magnitude =
			std::abs(a_worldToClipRow3.x)
			+ std::abs(a_worldToClipRow3.y)
			+ std::abs(a_worldToClipRow3.z);
		return std::isfinite(magnitude) && magnitude > 1e-6f;
	}

	[[nodiscard]] inline bool HasUsableCameraBasis(const FrameBuffer& a_frameBuffer) noexcept
	{
		for (const auto& row : a_frameBuffer.ViewToWorld) {
			const float magnitude =
				std::abs(row.x) + std::abs(row.y) + std::abs(row.z);
			if (!std::isfinite(magnitude) || !(magnitude > 1e-6f)) {
				return false;
			}
		}
		return std::isfinite(a_frameBuffer.CameraPosAdjust.x)
			&& std::isfinite(a_frameBuffer.CameraPosAdjust.y)
			&& std::isfinite(a_frameBuffer.CameraPosAdjust.z);
	}

	[[nodiscard]] inline bool HasFiniteWorldToClip(const FrameBuffer& a_frameBuffer) noexcept
	{
		for (const auto& row : a_frameBuffer.CurrFrameWorldToClip) {
			if (!std::isfinite(row.x)
				|| !std::isfinite(row.y)
				|| !std::isfinite(row.z)
				|| !std::isfinite(row.w)) {
				return false;
			}
		}
		return IsPerspectiveProjection(a_frameBuffer.CurrFrameWorldToClip[3]);
	}

	[[nodiscard]] inline bool HasUsableWorldCamera(const FrameBuffer& a_frameBuffer) noexcept
	{
		if (!HasUsableCameraBasis(a_frameBuffer) || !HasFiniteWorldToClip(a_frameBuffer)) {
			return false;
		}
		const auto origin = CameraWorldOrigin(a_frameBuffer);
		const auto previousOrigin = CameraPreviousWorldOrigin(a_frameBuffer);
		return std::isfinite(origin.x)
			&& std::isfinite(origin.y)
			&& std::isfinite(origin.z)
			&& std::isfinite(previousOrigin.x)
			&& std::isfinite(previousOrigin.y)
			&& std::isfinite(previousOrigin.z);
	}

	[[nodiscard]] inline bool HasNonzeroWorldCameraOrigin(
		const FrameBuffer& a_frameBuffer) noexcept
	{
		const auto origin = CameraWorldOrigin(a_frameBuffer);
		const float magnitudeSquared =
			origin.x * origin.x + origin.y * origin.y + origin.z * origin.z;
		return std::isfinite(magnitudeSquared)
			&& magnitudeSquared >=
				kMinimumWorldCameraOriginMagnitude
					* kMinimumWorldCameraOriginMagnitude;
	}
}
