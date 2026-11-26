#pragma once

#include "Render/Extent.h"

#include <DirectXMath.h>

#include <algorithm>
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
		DirectX::XMFLOAT4 PosAdjust{}, PreviousPosAdjust{};
		std::uint32_t frameCount = 0;
	};

	[[nodiscard]] inline DirectX::XMFLOAT3 CameraWorldOrigin(
		const WorldCameraRecord& a_camera) noexcept
	{
		return {
			a_camera.ViewToWorld[0].w + a_camera.PosAdjust.x,
			a_camera.ViewToWorld[1].w + a_camera.PosAdjust.y,
			a_camera.ViewToWorld[2].w + a_camera.PosAdjust.z
		};
	}

	[[nodiscard]] inline DirectX::XMFLOAT3 CameraPreviousWorldOrigin(
		const WorldCameraRecord& a_camera) noexcept
	{
		return {
			a_camera.PreviousPosAdjust.x,
			a_camera.PreviousPosAdjust.y,
			a_camera.PreviousPosAdjust.z
		};
	}

	// Orthographic (e.g. shadow) cameras leave world-to-clip row 3 at (0,0,0,1).
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
	[[nodiscard]] inline CameraWorldBasis GetCameraWorldBasis(
		const WorldCameraRecord& a_camera) noexcept
	{
		const auto* rows = a_camera.ViewToWorld;
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

	[[nodiscard]] inline bool HasUsableCameraBasis(const WorldCameraRecord& a_camera) noexcept
	{
		for (const auto& row : a_camera.ViewToWorld) {
			const float magnitude =
				std::abs(row.x) + std::abs(row.y) + std::abs(row.z);
			if (!std::isfinite(magnitude) || !(magnitude > 1e-6f)) {
				return false;
			}
		}
		return std::isfinite(a_camera.PosAdjust.x) && std::isfinite(a_camera.PosAdjust.y) && std::isfinite(a_camera.PosAdjust.z);
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

	// Conservative pixel rect of a world sphere; nullopt at the near plane.
	[[nodiscard]] inline std::optional<render::PixelRect> ProjectSphereToPixelRect(
		const WorldCameraRecord& a_camera, const DirectX::XMFLOAT3& a_center, float a_radius,
		render::Extent a_extent) noexcept
	{
		// FO4: row-vector clip = (world - posAdjust) * ViewProjection.
		const auto& m = a_camera.ViewProjection;
		const float x = a_center.x - a_camera.PosAdjust.x;
		const float y = a_center.y - a_camera.PosAdjust.y;
		const float z = a_center.z - a_camera.PosAdjust.z;
		struct Span
		{
			float center, reach;
		};
		// A unit center move shifts a clip component by at most its column length.
		const auto span = [&](float a_x, float a_y, float a_z, float a_w) {
			return Span{ x * a_x + y * a_y + z * a_z + a_w, a_radius * std::sqrt(a_x * a_x + a_y * a_y + a_z * a_z) };
		};
		const auto cx = span(m._11, m._21, m._31, m._41);
		const auto cy = span(m._12, m._22, m._32, m._42);
		const auto cw = span(m._14, m._24, m._34, m._44);
		const float wMin = cw.center - cw.reach;
		if (!std::isfinite(wMin) || !std::isfinite(cx.center) || !std::isfinite(cy.center) || wMin <= GetCameraDepthParameters(a_camera).y)
			return std::nullopt;
		const float wMax = cw.center + cw.reach;
		// The nearer w widens a positive bound, the farther w a negative one.
		const auto ndc = [&](Span a_span, float& a_lo, float& a_hi) {
			const float lo = a_span.center - a_span.reach;
			const float hi = a_span.center + a_span.reach;
			a_lo = lo / (lo >= 0.0f ? wMax : wMin);
			a_hi = hi / (hi >= 0.0f ? wMin : wMax);
		};
		float xLo, xHi, yLo, yHi;
		ndc(cx, xLo, xHi);
		ndc(cy, yLo, yHi);
		const float width = static_cast<float>(a_extent.width);
		const float height = static_cast<float>(a_extent.height);
		const auto pixel = [](float a_value, float a_size) {
			return std::clamp(a_value, 0.0f, a_size);
		};
		const float left = pixel((xLo * 0.5f + 0.5f) * width, width);
		const float right = pixel((xHi * 0.5f + 0.5f) * width, width);
		const float top = pixel((0.5f - yHi * 0.5f) * height, height);
		const float bottom = pixel((0.5f - yLo * 0.5f) * height, height);
		return render::PixelRect{ static_cast<std::uint32_t>(std::floor(left)), static_cast<std::uint32_t>(std::floor(top)),
			static_cast<std::uint32_t>(std::ceil(right)), static_cast<std::uint32_t>(std::ceil(bottom)) };
	}

	// Pre-prepass snapshot; pass the frame when rendering, omit for diagnostics.
	[[nodiscard]] std::optional<WorldCameraRecord> GetCapturedWorldCameraRecord(
		std::optional<std::uint64_t> a_frame = std::nullopt) noexcept;

	// Startup thread only; registers world-camera capture around the prepass.
	void InstallFrameBuffer();

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
