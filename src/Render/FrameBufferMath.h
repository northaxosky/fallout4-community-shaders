#pragma once

#include <DirectXMath.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cs::engine
{
	struct WorldCameraRecord
	{
		DirectX::XMFLOAT4X4 View{}, Projection{}, ViewProjection{};
		DirectX::XMFLOAT4X4 ViewProjectionUnjittered{}, PreviousViewProjectionUnjittered{};
		DirectX::XMFLOAT4X4 ProjectionUnjittered{}, ProjectionInverse{}, ProjectionUnjitteredInverse{};
		DirectX::XMFLOAT4X4 ViewInverse{}, ViewProjectionInverse{};
		DirectX::XMFLOAT4 ViewToWorld[3]{};
		DirectX::XMFLOAT4 CameraPosAdjust{}, CameraPreviousPosAdjust{};
		std::uint32_t frameCount = 0;
	};

	// Fallout 4 binds its per-frame constant buffer at HLSL register(b12).
	inline constexpr std::size_t kFrameBufferRegisters = 47;
	inline constexpr float kMinimumWorldCameraOriginMagnitude = 1.0f;

	// Field names and padding mirror package/Shaders/Common/DeferredContracts.hlsli and
	// DFPrepass.hlsl one-for-one, so the layout can be audited against the HLSL by eye.
	// The trailing comment on each member is its b12 float4 register index.
	struct alignas(16) FrameBuffer
	{
		DirectX::XMFLOAT4 cb12_pad_0_11[12];                    // 0-11
		DirectX::XMFLOAT4 ViewToWorld[3];                       // 12-14
		DirectX::XMFLOAT4 cb12_pad_15_19[5];                    // 15-19
		DirectX::XMFLOAT4 FarReproj[4];                         // 20-23
		DirectX::XMFLOAT4 NearReproj[4];                        // 24-27
		DirectX::XMFLOAT4 cb12_pad_28_29[2];                    // 28-29
		DirectX::XMFLOAT4 IblDesaturation;                      // 30
		DirectX::XMFLOAT4 PreviousViewProjectionUnjittered[4];  // 31-34
		DirectX::XMFLOAT4 CameraPosAdjust;                      // 35
		DirectX::XMFLOAT4 CameraPreviousPosAdjust;              // 36
		DirectX::XMFLOAT4 ViewProjectionUnjittered[4];          // 37-40
		DirectX::XMFLOAT4 FogDistanceRamp;                      // 41
		DirectX::XMFLOAT4 FogNearLowColorAndPower;              // 42
		DirectX::XMFLOAT4 FogNearHighColorAndClamp;             // 43
		DirectX::XMFLOAT4 FogFarLowColorAndDensity;             // 44
		DirectX::XMFLOAT4 FogFarHighColor;                      // 45
		DirectX::XMFLOAT4 FogHeightRamp;                        // 46
	};

	inline constexpr std::size_t kFrameBufferRegisterSize = sizeof(DirectX::XMFLOAT4);

	static_assert(sizeof(FrameBuffer) == kFrameBufferRegisters * kFrameBufferRegisterSize);
	static_assert(sizeof(FrameBuffer) == 752);
	static_assert(offsetof(FrameBuffer, ViewToWorld) == 12 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, FarReproj) == 20 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, NearReproj) == 24 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, IblDesaturation) == 30 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, PreviousViewProjectionUnjittered) == 31 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, CameraPosAdjust) == 35 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, CameraPreviousPosAdjust) == 36 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, ViewProjectionUnjittered) == 37 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, FogDistanceRamp) == 41 * kFrameBufferRegisterSize);
	static_assert(offsetof(FrameBuffer, FogHeightRamp) == 46 * kFrameBufferRegisterSize);

	template <class Camera>
	[[nodiscard]] inline DirectX::XMFLOAT3 CameraWorldOrigin(
		const Camera& a_frameBuffer) noexcept
	{
		return {
			a_frameBuffer.ViewToWorld[0].w + a_frameBuffer.CameraPosAdjust.x,
			a_frameBuffer.ViewToWorld[1].w + a_frameBuffer.CameraPosAdjust.y,
			a_frameBuffer.ViewToWorld[2].w + a_frameBuffer.CameraPosAdjust.z
		};
	}

	template <class Camera>
	[[nodiscard]] inline DirectX::XMFLOAT3 CameraPreviousWorldOrigin(
		const Camera& a_frameBuffer) noexcept
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

	// Absolute world position, matching DFComposite.hlsl: dot each row with
	// float4(view, 1), then add the renderer's position adjustment.
	[[nodiscard]] inline DirectX::XMFLOAT3 ViewToWorldPosition(
		const DirectX::XMFLOAT3& a_viewPosition,
		const DirectX::XMFLOAT4* a_rows,
		const DirectX::XMFLOAT3& a_origin) noexcept
	{
		const auto row = [&a_viewPosition](const DirectX::XMFLOAT4& a_row) noexcept {
			return a_row.x * a_viewPosition.x + a_row.y * a_viewPosition.y + a_row.z * a_viewPosition.z + a_row.w;
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
			return a_row.x * a_direction.x + a_row.y * a_direction.y + a_row.z * a_direction.z;
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
			std::abs(a_worldToClipRow3.x) + std::abs(a_worldToClipRow3.y) + std::abs(a_worldToClipRow3.z);
		return std::isfinite(magnitude) && magnitude > 1e-6f;
	}

	struct CameraWorldBasis
	{
		DirectX::XMFLOAT3 right{};
		DirectX::XMFLOAT3 up{};
		DirectX::XMFLOAT3 forward{};
	};

	// world = R * view, so camera axes are R's columns.
	template <class Camera>
	[[nodiscard]] inline CameraWorldBasis GetCameraWorldBasis(
		const Camera& a_frameBuffer) noexcept
	{
		const auto* rows = a_frameBuffer.ViewToWorld;
		return {
			{ rows[0].x, rows[1].x, rows[2].x },
			{ rows[0].y, rows[1].y, rows[2].y },
			{ rows[0].z, rows[1].z, rows[2].z }
		};
	}

	// Recovers asymmetric vertical FOV from row-major M = P * V.
	[[nodiscard]] inline float VerticalFieldOfViewFromWorldToClip(
		const DirectX::XMFLOAT4* a_rows) noexcept
	{
		const auto& row1 = a_rows[1];
		const auto& row3 = a_rows[3];
		const float forwardLength = std::sqrt(
			row3.x * row3.x + row3.y * row3.y + row3.z * row3.z);
		if (!std::isfinite(forwardLength) || !(forwardLength > 1e-8f)) {
			return 0.0f;
		}

		const DirectX::XMFLOAT3 forward{
			row3.x / forwardLength,
			row3.y / forwardLength,
			row3.z / forwardLength
		};
		const DirectX::XMFLOAT3 scaledUp{
			row1.x / forwardLength,
			row1.y / forwardLength,
			row1.z / forwardLength
		};
		const float centerOffset =
			scaledUp.x * forward.x + scaledUp.y * forward.y + scaledUp.z * forward.z;
		const DirectX::XMFLOAT3 upComponent{
			scaledUp.x - centerOffset * forward.x,
			scaledUp.y - centerOffset * forward.y,
			scaledUp.z - centerOffset * forward.z
		};
		const float scale = std::sqrt(
			upComponent.x * upComponent.x + upComponent.y * upComponent.y + upComponent.z * upComponent.z);
		if (!std::isfinite(scale) || !(scale > 1e-8f)) {
			return 0.0f;
		}

		const float top = (1.0f - centerOffset) / scale;
		const float bottom = (-1.0f - centerOffset) / scale;
		const float fieldOfView = std::atan(top) - std::atan(bottom);
		return std::isfinite(fieldOfView) ? fieldOfView : 0.0f;
	}

	template <class Camera>
	[[nodiscard]] inline bool HasUsableCameraBasis(const Camera& a_frameBuffer) noexcept
	{
		for (const auto& row : a_frameBuffer.ViewToWorld) {
			const float magnitude =
				std::abs(row.x) + std::abs(row.y) + std::abs(row.z);
			if (!std::isfinite(magnitude) || !(magnitude > 1e-6f)) {
				return false;
			}
		}
		return std::isfinite(a_frameBuffer.CameraPosAdjust.x) && std::isfinite(a_frameBuffer.CameraPosAdjust.y) && std::isfinite(a_frameBuffer.CameraPosAdjust.z);
	}

	[[nodiscard]] inline bool HasFiniteWorldToClip(const FrameBuffer& a_frameBuffer) noexcept
	{
		for (const auto& row : a_frameBuffer.ViewProjectionUnjittered) {
			if (!std::isfinite(row.x) || !std::isfinite(row.y) || !std::isfinite(row.z) || !std::isfinite(row.w)) {
				return false;
			}
		}
		return IsPerspectiveProjection(a_frameBuffer.ViewProjectionUnjittered[3]);
	}

	[[nodiscard]] inline bool HasUsableWorldCamera(const FrameBuffer& a_frameBuffer) noexcept
	{
		if (!HasUsableCameraBasis(a_frameBuffer) || !HasFiniteWorldToClip(a_frameBuffer)) {
			return false;
		}
		const auto origin = CameraWorldOrigin(a_frameBuffer);
		const auto previousOrigin = CameraPreviousWorldOrigin(a_frameBuffer);
		return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z) && std::isfinite(previousOrigin.x) && std::isfinite(previousOrigin.y) && std::isfinite(previousOrigin.z);
	}

	[[nodiscard]] inline bool HasNonzeroWorldCameraOrigin(
		const FrameBuffer& a_frameBuffer) noexcept
	{
		const auto origin = CameraWorldOrigin(a_frameBuffer);
		const float magnitudeSquared =
			origin.x * origin.x + origin.y * origin.y + origin.z * origin.z;
		return std::isfinite(magnitudeSquared) && magnitudeSquared >=
		                                              kMinimumWorldCameraOriginMagnitude * kMinimumWorldCameraOriginMagnitude;
	}

	[[nodiscard]] inline bool HasFiniteMatrix(const DirectX::XMFLOAT4X4& a_matrix) noexcept
	{
		for (const auto& row : a_matrix.m)
			for (const auto value : row)
				if (!std::isfinite(value))
					return false;
		return true;
	}

	[[nodiscard]] inline DirectX::XMFLOAT4 GetCameraDepthParameters(const WorldCameraRecord& a_camera) noexcept
	{
		const auto& projection = a_camera.Projection;
		const float nearZ = -projection._43 / projection._33;
		const float farZ = -projection._43 / (projection._33 - 1.0f);
		return { farZ, nearZ, farZ - nearZ, farZ * nearZ };
	}

	[[nodiscard]] inline bool PrepareWorldCameraRecord(WorldCameraRecord& a_camera) noexcept
	{
		using namespace DirectX;
		const auto view = XMLoadFloat4x4(&a_camera.View);
		const auto projection = XMLoadFloat4x4(&a_camera.Projection);
		XMVECTOR determinant;
		const auto inverseView = XMMatrixInverse(&determinant, view);
		if (!std::isfinite(XMVectorGetX(determinant)) || std::abs(XMVectorGetX(determinant)) < 1e-8f)
			return false;
		XMFLOAT4X4 inverseViewRows;
		XMStoreFloat4x4(&inverseViewRows, XMMatrixTranspose(inverseView));
		for (std::size_t row = 0; row < 3; ++row)
			a_camera.ViewToWorld[row] = XMFLOAT4(inverseViewRows.m[row]);
		const auto unjittered = inverseView * XMLoadFloat4x4(&a_camera.ViewProjectionUnjittered);
		XMStoreFloat4x4(&a_camera.ViewInverse, inverseView);
		XMStoreFloat4x4(&a_camera.ViewProjectionInverse, XMMatrixInverse(nullptr, XMLoadFloat4x4(&a_camera.ViewProjection)));
		XMStoreFloat4x4(&a_camera.ProjectionUnjittered, unjittered);
		XMStoreFloat4x4(&a_camera.ProjectionInverse, XMMatrixInverse(nullptr, projection));
		XMStoreFloat4x4(&a_camera.ProjectionUnjitteredInverse, XMMatrixInverse(nullptr, unjittered));
		for (const auto* matrix : {
				 &a_camera.View, &a_camera.Projection, &a_camera.ViewProjection,
				 &a_camera.ViewProjectionUnjittered, &a_camera.PreviousViewProjectionUnjittered,
				 &a_camera.ProjectionUnjittered, &a_camera.ProjectionInverse,
				 &a_camera.ProjectionUnjitteredInverse, &a_camera.ViewInverse, &a_camera.ViewProjectionInverse }) {
			const float matrixDeterminant = XMVectorGetX(XMMatrixDeterminant(XMLoadFloat4x4(matrix)));
			if (!HasFiniteMatrix(*matrix) || !std::isfinite(matrixDeterminant) || matrixDeterminant == 0.0f)
				return false;
		}
		const auto& vp = a_camera.ViewProjectionUnjittered;
		const auto previous = CameraPreviousWorldOrigin(a_camera);
		const auto depth = GetCameraDepthParameters(a_camera);
		return HasUsableCameraBasis(a_camera) &&
		       IsPerspectiveProjection({ vp._14, vp._24, vp._34, vp._44 }) &&
		       std::isfinite(depth.x) && std::isfinite(depth.y) && depth.y > 0.0f && depth.x > depth.y &&
		       std::isfinite(previous.x) && std::isfinite(previous.y) && std::isfinite(previous.z);
	}

	inline void GetWorldSceneProjection(const WorldCameraRecord& a_camera,
		DirectX::XMFLOAT4X4& a_projection, DirectX::XMFLOAT4X4& a_inverse,
		DirectX::XMFLOAT4& a_mul, DirectX::XMFLOAT4& a_add) noexcept
	{
		using namespace DirectX;
		a_projection = a_camera.Projection;
		a_inverse = a_camera.ProjectionInverse;
		const auto inverse = XMLoadFloat4x4(&a_inverse);
		auto topLeft = XMVector4Transform(XMVectorSet(-1, 1, 1, 1), inverse);
		auto bottomRight = XMVector4Transform(XMVectorSet(1, -1, 1, 1), inverse);
		topLeft = XMVectorScale(topLeft, 1.0f / XMVectorGetZ(topLeft));
		bottomRight = XMVectorScale(bottomRight, 1.0f / XMVectorGetZ(bottomRight));
		XMStoreFloat4(&a_add, XMVectorSet(XMVectorGetX(topLeft), XMVectorGetY(topLeft), 0, 0));
		XMStoreFloat4(&a_mul, XMVectorSet(XMVectorGetX(bottomRight - topLeft), XMVectorGetY(bottomRight - topLeft), 0, 0));
	}
}
