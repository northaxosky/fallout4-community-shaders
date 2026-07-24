#include "RE/N/NiFrustum.h"

#include <DirectXMath.h>

#include <cmath>
#include <cstdio>

namespace
{
	struct Point
	{
		float x;
		float y;
		float z;
	};

	struct ProjectedPoint
	{
		float x;
		float y;
		float z;
		float w;
	};

	struct CameraRotationRows
	{
		Point forward;
		Point up;
		Point right;
	};

	int failures = 0;

	void Check(bool condition, const char* assertion)
	{
		if (!condition) {
			std::printf("FAIL: %s\n", assertion);
			++failures;
		}
	}

	void CheckNear(float actual, float expected, float tolerance, const char* assertion)
	{
		if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
			std::printf(
				"FAIL: %s (actual %.9g, expected %.9g, tolerance %.9g)\n",
				assertion,
				static_cast<double>(actual),
				static_cast<double>(expected),
				static_cast<double>(tolerance));
			++failures;
		}
	}

	void CheckNearAt(
		float actual,
		float expected,
		float tolerance,
		const char* caseName,
		int pointIndex,
		const char* assertion)
	{
		if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
			std::printf(
				"FAIL: %s point %d %s (actual %.9g, expected %.9g, tolerance %.9g)\n",
				caseName,
				pointIndex,
				assertion,
				static_cast<double>(actual),
				static_cast<double>(expected),
				static_cast<double>(tolerance));
			++failures;
		}
	}

	float Dot(const Point& a_left, const Point& a_right)
	{
		return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z;
	}

	CameraRotationRows MakeCameraRotationRows(float a_yaw, float a_pitch)
	{
		const float sinYaw = std::sin(a_yaw);
		const float cosYaw = std::cos(a_yaw);
		const float sinPitch = std::sin(a_pitch);
		const float cosPitch = std::cos(a_pitch);
		return {
			{ cosPitch * cosYaw, cosPitch * sinYaw, sinPitch },
			{ -sinPitch * cosYaw, -sinPitch * sinYaw, cosPitch },
			{ sinYaw, -cosYaw, 0.0f }
		};
	}

	Point WorldNormalToD3DView(const CameraRotationRows& a_rows, const Point& a_worldNormal)
	{
		return {
			Dot(a_rows.right, a_worldNormal),
			Dot(a_rows.up, a_worldNormal),
			Dot(a_rows.forward, a_worldNormal)
		};
	}

	float ViewNormalWorldUp(const CameraRotationRows& a_rows, const Point& a_viewNormal)
	{
		const Point directionNi{ a_viewNormal.z, a_viewNormal.y, a_viewNormal.x };
		return directionNi.x * a_rows.forward.z +
		       directionNi.y * a_rows.up.z +
		       directionNi.z * a_rows.right.z;
	}

	float OldRowDotWorldUp(const CameraRotationRows& a_rows, const Point& a_viewNormal)
	{
		return Dot(a_rows.right, a_viewNormal);
	}

	ProjectedPoint Project(const Point& point, const DirectX::XMFLOAT4X4& projection)
	{
		const auto clip = DirectX::XMVector4Transform(
			DirectX::XMVectorSet(point.x, point.y, point.z, 1.0f),
			DirectX::XMLoadFloat4x4(&projection));
		DirectX::XMFLOAT4 clipValues;
		DirectX::XMStoreFloat4(&clipValues, clip);
		const float reciprocalW = 1.0f / clipValues.w;
		return {
			clipValues.x * reciprocalW,
			clipValues.y * reciprocalW,
			clipValues.z * reciprocalW,
			clipValues.w
		};
	}

	Point Unproject(const ProjectedPoint& ndc, const DirectX::XMFLOAT4X4& inverseProjection)
	{
		const auto view = DirectX::XMVector4Transform(
			DirectX::XMVectorSet(ndc.x, ndc.y, ndc.z, 1.0f),
			DirectX::XMLoadFloat4x4(&inverseProjection));
		DirectX::XMFLOAT4 viewValues;
		DirectX::XMStoreFloat4(&viewValues, view);
		const float reciprocalW = 1.0f / viewValues.w;
		return {
			viewValues.x * reciprocalW,
			viewValues.y * reciprocalW,
			viewValues.z * reciprocalW
		};
	}

	void CheckRoundTrips(
		const char* caseName,
		const Point* points,
		int pointCount,
		const DirectX::XMFLOAT4X4& projection,
		const DirectX::XMFLOAT4X4& inverseProjection,
		const DirectX::XMFLOAT4& ndcToViewMul,
		const DirectX::XMFLOAT4& ndcToViewAdd,
		bool checkRay)
	{
		for (int index = 0; index < pointCount; ++index) {
			const Point& point = points[index];
			const ProjectedPoint ndc = Project(point, projection);
			const float uvX = (ndc.x + 1.0f) * 0.5f;
			const float uvY = (1.0f - ndc.y) * 0.5f;

			const Point reconstructed = Unproject(ndc, inverseProjection);
			CheckNearAt(reconstructed.x, point.x, 1.0e-3f, caseName, index, "invProj x");
			CheckNearAt(reconstructed.y, point.y, 1.0e-3f, caseName, index, "invProj y");
			CheckNearAt(reconstructed.z, point.z, 1.0e-3f, caseName, index, "invProj z");

			if (checkRay) {
				const float viewX = (uvX * ndcToViewMul.x + ndcToViewAdd.x) * point.z;
				const float viewY = (uvY * ndcToViewMul.y + ndcToViewAdd.y) * point.z;
				CheckNearAt(viewX, point.x, 1.0e-3f, caseName, index, "NDCToView x");
				CheckNearAt(viewY, point.y, 1.0e-3f, caseName, index, "NDCToView y");
			}
		}
	}

	bool IsFinite(const DirectX::XMFLOAT4X4& value)
	{
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				if (!std::isfinite(value.m[row][column])) {
					return false;
				}
			}
		}
		return true;
	}

	bool IsFinite(const DirectX::XMFLOAT4& value)
	{
		return std::isfinite(value.x) &&
		       std::isfinite(value.y) &&
		       std::isfinite(value.z) &&
		       std::isfinite(value.w);
	}

	void CheckRejected(
		float left,
		float right,
		float top,
		float bottom,
		float nearZ,
		float farZ,
		const char* assertion)
	{
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 inverseProjection{};
		DirectX::XMFLOAT4 ndcToViewMul{};
		DirectX::XMFLOAT4 ndcToViewAdd{};
		const bool built = RE::BuildPerspectiveFromFrustum(
			{ left, right, top, bottom, nearZ, farZ, false },
			projection,
			inverseProjection,
			ndcToViewMul,
			ndcToViewAdd);

		Check(!built, assertion);
		if (!IsFinite(projection) ||
			!IsFinite(inverseProjection) ||
			!IsFinite(ndcToViewMul) ||
			!IsFinite(ndcToViewAdd)) {
			std::printf("FAIL: %s wrote non-finite output\n", assertion);
			++failures;
		}
	}

	void TestSymmetricFrustum()
	{
		constexpr float left = -0.83910f;
		constexpr float right = 0.83910f;
		constexpr float bottom = -0.47181f;
		constexpr float top = 0.47181f;
		constexpr float nearZ = 0.1f;
		constexpr float farZ = 10000.0f;

		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 inverseProjection{};
		DirectX::XMFLOAT4 ndcToViewMul{};
		DirectX::XMFLOAT4 ndcToViewAdd{};
		const bool built = RE::BuildPerspectiveFromFrustum(
			{ left, right, top, bottom, nearZ, farZ, false },
			projection,
			inverseProjection,
			ndcToViewMul,
			ndcToViewAdd);
		Check(built, "Case A builds symmetric frustum");
		if (!built) {
			return;
		}

		CheckNear(Project({ nearZ * right, 0.0f, nearZ }, projection).x, 1.0f, 1.0e-4f, "Case A right edge maps to +1 NDC x");
		CheckNear(Project({ nearZ * left, 0.0f, nearZ }, projection).x, -1.0f, 1.0e-4f, "Case A left edge maps to -1 NDC x");
		CheckNear(Project({ 0.0f, nearZ * top, nearZ }, projection).y, 1.0f, 1.0e-4f, "Case A top edge maps to +1 NDC y");
		CheckNear(Project({ 0.0f, nearZ * bottom, nearZ }, projection).y, -1.0f, 1.0e-4f, "Case A bottom edge maps to -1 NDC y");
		CheckNear(Project({ 0.0f, 0.0f, nearZ }, projection).z, 0.0f, 1.0e-4f, "Case A near plane maps to 0 NDC z");
		CheckNear(Project({ 0.0f, 0.0f, farZ }, projection).z, 1.0f, 1.0e-4f, "Case A far plane maps to 1 NDC z");
		CheckNear(Project({ 2.0f * nearZ * right, 0.0f, 2.0f * nearZ }, projection).x, 1.0f, 1.0e-4f, "Case A farther right edge remains +1 NDC x");
		CheckNear(projection._11, 1.0f / right, 1.0e-4f, "Case A row-major _11 is 1/right");
		CheckNear(projection._22, 1.0f / top, 1.0e-4f, "Case A row-major _22 is 1/top");

		const Point points[]{
			{ 0.0f, 0.0f, 0.2f },
			{ right * 0.25f, bottom * 0.30f, 1.0f },
			{ left * 0.55f * 5.0f, top * 0.60f * 5.0f, 5.0f },
			{ right * 0.75f * 20.0f, bottom * 0.70f * 20.0f, 20.0f }
		};
		CheckRoundTrips(
			"Case A",
			points,
			static_cast<int>(sizeof(points) / sizeof(points[0])),
			projection,
			inverseProjection,
			ndcToViewMul,
			ndcToViewAdd,
			true);
	}

	void TestAsymmetricFrustum()
	{
		constexpr float left = -0.6f;
		constexpr float right = 1.0f;
		constexpr float bottom = -0.5f;
		constexpr float top = 0.7f;
		constexpr float nearZ = 0.05f;
		constexpr float farZ = 5000.0f;

		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 inverseProjection{};
		DirectX::XMFLOAT4 ndcToViewMul{};
		DirectX::XMFLOAT4 ndcToViewAdd{};
		const bool built = RE::BuildPerspectiveFromFrustum(
			{ left, right, top, bottom, nearZ, farZ, false },
			projection,
			inverseProjection,
			ndcToViewMul,
			ndcToViewAdd);
		Check(built, "Case B builds asymmetric frustum");
		if (!built) {
			return;
		}

		CheckNear(Project({ nearZ * right, 0.0f, nearZ }, projection).x, 1.0f, 1.0e-4f, "Case B right edge maps to +1 NDC x");
		CheckNear(Project({ nearZ * left, 0.0f, nearZ }, projection).x, -1.0f, 1.0e-4f, "Case B left edge maps to -1 NDC x");
		CheckNear(Project({ 0.0f, nearZ * top, nearZ }, projection).y, 1.0f, 1.0e-4f, "Case B top edge maps to +1 NDC y");
		CheckNear(Project({ 0.0f, nearZ * bottom, nearZ }, projection).y, -1.0f, 1.0e-4f, "Case B bottom edge maps to -1 NDC y");

		const Point points[]{
			{ 0.02f, 0.01f, 0.1f },
			{ -0.21f, -0.195f, 0.75f },
			{ 2.72f, 1.36f, 4.0f },
			{ 0.8f, -0.4f, 20.0f }
		};
		CheckRoundTrips(
			"Case B",
			points,
			static_cast<int>(sizeof(points) / sizeof(points[0])),
			projection,
			inverseProjection,
			ndcToViewMul,
			ndcToViewAdd,
			false);
	}

	void TestRejectedFrusta()
	{
		CheckRejected(-0.8f, 0.8f, 0.5f, -0.5f, 0.1f, 0.1f, "Case C rejects farZ <= nearZ");
		CheckRejected(-0.8f, 0.8f, 0.5f, -0.5f, 0.0f, 1000.0f, "Case C rejects nearZ <= 0");
		CheckRejected(0.8f, 0.8f, 0.5f, -0.5f, 0.1f, 1000.0f, "Case C rejects left >= right");
		CheckRejected(-0.8f, 0.8f, 0.5f, 0.5f, 0.1f, 1000.0f, "Case C rejects bottom >= top");
		CheckRejected(std::nanf(""), 0.8f, 0.5f, -0.5f, 0.1f, 1000.0f, "Case C rejects NaN");
		CheckRejected(-0.8f, 0.8f, 0.5f, -0.5f, 0.1f, HUGE_VALF, "Case C rejects infinity");
	}

	void TestCameraViewToWorld()
	{
		struct Pose
		{
			float yaw;
			float pitch;
		};

		const Pose poses[]{
			{ 0.0f, 0.0f },
			{ 0.65f, 0.0f },
			{ 0.0f, 0.4f },
			{ 0.7f, -0.35f },
			{ -1.1f, 0.55f }
		};
		const Point groundNormal{ 0.0f, 0.0f, 1.0f };
		const Point wallNormal{ 1.0f, 0.0f, 0.0f };
		constexpr float tolerance = 1.0e-5f;
		bool oldGroundFailed = false;
		bool oldWallFailed = false;

		for (int index = 0; index < static_cast<int>(sizeof(poses) / sizeof(poses[0])); ++index) {
			const CameraRotationRows rows = MakeCameraRotationRows(poses[index].yaw, poses[index].pitch);
			const Point groundView = WorldNormalToD3DView(rows, groundNormal);
			const Point wallView = WorldNormalToD3DView(rows, wallNormal);
			CheckNearAt(ViewNormalWorldUp(rows, groundView), 1.0f, tolerance, "Camera transform", index, "ground world-up");
			CheckNearAt(ViewNormalWorldUp(rows, wallView), 0.0f, tolerance, "Camera transform", index, "wall world-up");

			if (poses[index].yaw != 0.0f && poses[index].pitch != 0.0f) {
				oldGroundFailed |= std::fabs(OldRowDotWorldUp(rows, groundView) - 1.0f) > tolerance;
				oldWallFailed |= std::fabs(OldRowDotWorldUp(rows, wallView)) > tolerance;
			}
		}

		Check(oldGroundFailed, "Camera transform old row-dot fails ground invariant");
		Check(oldWallFailed, "Camera transform old row-dot fails wall invariant");
	}
}

int main()
{
	TestSymmetricFrustum();
	TestAsymmetricFrustum();
	TestRejectedFrusta();
	TestCameraViewToWorld();

	if (failures != 0) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}

	std::printf("ProjectionMath tests passed\n");
	return 0;
}
