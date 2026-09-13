#include "Render/FrameBufferMath.h"
#include "SuperResolutionFov.h"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	void CheckNear(
		float a_actual,
		float a_expected,
		float a_tolerance,
		std::string_view a_message)
	{
		if (!std::isfinite(a_actual) || std::abs(a_actual - a_expected) > a_tolerance) {
			std::cerr << "FAIL: " << a_message << " (actual " << a_actual
					  << ", expected " << a_expected << ")\n";
			++failures;
		}
	}

	std::array<DirectX::XMFLOAT4, 4> Rows(
		const DirectX::XMFLOAT4X4& a_matrix)
	{
		return {
			DirectX::XMFLOAT4{ a_matrix._11, a_matrix._12, a_matrix._13, a_matrix._14 },
			DirectX::XMFLOAT4{ a_matrix._21, a_matrix._22, a_matrix._23, a_matrix._24 },
			DirectX::XMFLOAT4{ a_matrix._31, a_matrix._32, a_matrix._33, a_matrix._34 },
			DirectX::XMFLOAT4{ a_matrix._41, a_matrix._42, a_matrix._43, a_matrix._44 }
		};
	}

	void TestBasisUsesColumns()
	{
		cs::engine::FrameBuffer frameBuffer{};
		frameBuffer.ViewToWorld[0] = { 0.0f, 0.0f, 1.0f, 10.0f };
		frameBuffer.ViewToWorld[1] = { 1.0f, 0.0f, 0.0f, 20.0f };
		frameBuffer.ViewToWorld[2] = { 0.0f, 1.0f, 0.0f, 30.0f };

		const auto basis = cs::engine::GetCameraWorldBasis(frameBuffer);
		CheckNear(basis.right.x, 0.0f, 0.0f, "right x comes from row 0 x");
		CheckNear(basis.right.y, 1.0f, 0.0f, "right y comes from row 1 x");
		CheckNear(basis.right.z, 0.0f, 0.0f, "right z comes from row 2 x");
		CheckNear(basis.up.x, 0.0f, 0.0f, "up x comes from row 0 y");
		CheckNear(basis.up.y, 0.0f, 0.0f, "up y comes from row 1 y");
		CheckNear(basis.up.z, 1.0f, 0.0f, "up z comes from row 2 y");
		CheckNear(basis.forward.x, 1.0f, 0.0f, "forward x comes from row 0 z");
		CheckNear(basis.forward.y, 0.0f, 0.0f, "forward y comes from row 1 z");
		CheckNear(basis.forward.z, 0.0f, 0.0f, "forward z comes from row 2 z");
		Check(
			basis.right.x != frameBuffer.ViewToWorld[0].x ||
				basis.right.y != frameBuffer.ViewToWorld[0].y ||
				basis.right.z != frameBuffer.ViewToWorld[0].z,
			"camera right is not row 0");
	}

	void TestFov(
		float a_top,
		float a_bottom,
		std::string_view a_message)
	{
		const float yScale = 2.0f / (a_top - a_bottom);
		const float centerOffset = -(a_top + a_bottom) / (a_top - a_bottom);
		const DirectX::XMFLOAT4X4 projection{
			1.25f, 0.0f, 0.15f, 0.0f,
			0.0f, yScale, centerOffset, 0.0f,
			0.0f, 0.0f, 1.001f, -0.1001f,
			0.0f, 0.0f, 1.0f, 0.0f
		};
		const DirectX::XMFLOAT4X4 worldToView{
			0.0f, 1.0f, 0.0f, 40.0f,
			0.0f, 0.0f, 1.0f, -30.0f,
			1.0f, 0.0f, 0.0f, 20.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		DirectX::XMFLOAT4X4 worldToClip{};
		DirectX::XMStoreFloat4x4(
			&worldToClip,
			DirectX::XMMatrixMultiply(
				DirectX::XMLoadFloat4x4(&projection),
				DirectX::XMLoadFloat4x4(&worldToView)));
		const auto rows = Rows(worldToClip);

		CheckNear(
			cs::engine::VerticalFieldOfViewFromWorldToClip(rows.data()),
			std::atan(a_top) - std::atan(a_bottom),
			1e-5f,
			a_message);
	}

	void TestFovRejection()
	{
		const DirectX::XMFLOAT4 rows[4]{};
		CheckNear(
			cs::engine::VerticalFieldOfViewFromWorldToClip(rows),
			0.0f,
			0.0f,
			"a degenerate projection has no vertical FOV");
	}

	cs::engine::FrameBufferSnapshot MakeFovSnapshot(float a_verticalFov)
	{
		const float yScale = 1.0f / std::tan(a_verticalFov * 0.5f);
		cs::engine::FrameBufferSnapshot snapshot{};
		snapshot.valid = true;
		snapshot.data.CurrFrameWorldToClip[0] = { 1.0f, 0.0f, 0.0f, 0.0f };
		snapshot.data.CurrFrameWorldToClip[1] = { 0.0f, yScale, 0.0f, 0.0f };
		snapshot.data.CurrFrameWorldToClip[2] = { 0.0f, 0.0f, 1.0f, -0.1f };
		snapshot.data.CurrFrameWorldToClip[3] = { 0.0f, 0.0f, 1.0f, 0.0f };
		return snapshot;
	}

	void TestSuperResolutionFovCache()
	{
		cs::features::SuperResolutionFovCache cache;
		float resolvedFov = 0.0f;
		const cs::engine::FrameBufferSnapshot missing{};
		Check(
			cache.Resolve(missing, resolvedFov) ==
				cs::features::SuperResolutionFovSource::kUnavailable,
			"super-resolution declines before any valid FOV is published");

		constexpr float firstFov = 0.9f;
		Check(
			cache.Resolve(MakeFovSnapshot(firstFov), resolvedFov) ==
				cs::features::SuperResolutionFovSource::kPublished,
			"super-resolution accepts a published FOV");
		CheckNear(resolvedFov, firstFov, 1e-5f, "published FOV is passed through");

		Check(
			cache.Resolve(missing, resolvedFov) ==
				cs::features::SuperResolutionFovSource::kCached,
			"super-resolution uses its last valid FOV across a transient miss");
		CheckNear(resolvedFov, firstFov, 1e-5f, "transient miss preserves the last valid FOV");

		constexpr float secondFov = 1.1f;
		Check(
			cache.Resolve(MakeFovSnapshot(secondFov), resolvedFov) ==
				cs::features::SuperResolutionFovSource::kPublished,
			"super-resolution refreshes the cached FOV");
		CheckNear(resolvedFov, secondFov, 1e-5f, "new published FOV replaces the cached value");
	}

}

int main()
{
	TestBasisUsesColumns();
	TestFov(0.5f, -0.5f, "symmetric vertical FOV survives a rotated view");
	TestFov(0.7f, -0.5f, "asymmetric vertical FOV survives a rotated view");
	TestFovRejection();
	TestSuperResolutionFovCache();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Frame-generation camera checks passed\n";
	return 0;
}
