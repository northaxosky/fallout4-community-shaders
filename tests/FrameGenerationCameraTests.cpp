#include "Render/FrameBufferMath.h"
#include "SuperResolutionFov.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
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

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream) {
			std::cerr << "FAIL: could not open " << a_path.string() << '\n';
			++failures;
			return {};
		}
		return std::string(
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>());
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

	void TestSourceContracts(
		const std::filesystem::path& a_fidelityFxPath,
		const std::filesystem::path& a_enginePath,
		const std::filesystem::path& a_upscalingPath)
	{
		const auto fidelityFx = ReadFile(a_fidelityFxPath);
		Check(
			!fidelityFx.contains("TryGetCameraMatrices") &&
				!fidelityFx.contains(".invView") &&
				!fidelityFx.contains("cameraState.posAdjust"),
			"frame generation does not use timing-dependent camera transforms");
		Check(
			fidelityFx.contains("cs::engine::GetFrameBuffer()") &&
				fidelityFx.contains("cs::engine::CameraWorldOrigin(frameBuffer.data)") &&
				fidelityFx.contains("cs::engine::GetCameraWorldBasis(frameBuffer.data)") &&
				fidelityFx.contains("TryGetPublishedVerticalFov(frameBuffer, verticalFov)"),
			"frame generation reads position, basis, and FOV from the published b12 snapshot");
		const auto upscaleStart = fidelityFx.find("bool FidelityFX::Upscale(");
		const auto upscaleDispatch =
			fidelityFx.find("ffxFsr3ContextDispatchUpscale", upscaleStart);
		const auto upscaleFrameBuffer =
			fidelityFx.find("const auto& frameBuffer = cs::engine::GetFrameBuffer();", upscaleStart);
		const auto upscaleFovSource =
			fidelityFx.find("superResolutionFovCache.Resolve(frameBuffer, verticalFov)", upscaleStart);
		const auto unavailableFov =
			fidelityFx.find("fovSource == SuperResolutionFovSource::kUnavailable", upscaleStart);
		const auto cachedFov =
			fidelityFx.find("fovSource == SuperResolutionFovSource::kCached", upscaleStart);
		const auto upscaleFov =
			fidelityFx.find("dispatchParameters.cameraFovAngleVertical = verticalFov;", upscaleStart);
		Check(
			upscaleStart != std::string::npos &&
				upscaleDispatch != std::string::npos &&
				upscaleFrameBuffer > upscaleStart &&
				upscaleFrameBuffer < upscaleDispatch &&
				upscaleFovSource > upscaleFrameBuffer &&
				upscaleFovSource < upscaleDispatch &&
				unavailableFov > upscaleFovSource &&
				unavailableFov < upscaleDispatch &&
				cachedFov > unavailableFov &&
				cachedFov < upscaleDispatch &&
				upscaleFov > upscaleFovSource &&
				upscaleFov < upscaleDispatch &&
				!fidelityFx.contains("GetVerticalFOV"),
			"FSR super-resolution receives current or cached b12 FOV without a transient decline");
		const auto frameGenerationStart =
			fidelityFx.find("bool FidelityFX::CacheFrameGenerationCameraData()");
		const auto frameGenerationEnd =
			fidelityFx.find("void FidelityFX::ResetFrameGenerationCameraData()", frameGenerationStart);
		const auto frameGenerationBlock =
			frameGenerationStart != std::string::npos && frameGenerationEnd != std::string::npos
			? std::string_view(fidelityFx).substr(
				  frameGenerationStart,
				  frameGenerationEnd - frameGenerationStart)
			: std::string_view{};
		Check(
			frameGenerationBlock.contains("TryGetPublishedVerticalFov(frameBuffer, verticalFov)") &&
				!frameGenerationBlock.contains("superResolutionFovCache"),
			"frame generation remains strictly fail-closed instead of reusing cached camera data");

		const auto engine = ReadFile(a_enginePath);
		Check(
			!engine.contains("CameraMatrices") &&
				!engine.contains("TryGetCameraMatrices") &&
				!engine.contains("GetVerticalFOV") &&
				!engine.contains("camViewData"),
			"Engine.h contains no timing-dependent camera transform accessors");

		const auto upscaling = ReadFile(a_upscalingPath);
		Check(
			upscaling.contains("fidelityFX.ResetFrameGenerationCameraData();"),
			"each capture invalidates the previous frame-generation camera before early exits");
		Check(
			!upscaling.contains("fg_camera_state_fov_deg") &&
				!upscaling.contains("fg_camera_origin") &&
				!upscaling.contains("fg_camera_right") &&
				!upscaling.contains("fg_camera_up") &&
				!upscaling.contains("fg_camera_forward") &&
				!upscaling.contains("fg_camera_basis_det") &&
				!upscaling.contains("fg_camera_row_norms"),
			"temporary camera proof telemetry stays removed");
	}
}

int main(int argc, char** argv)
{
	if (argc < 4) {
		std::cerr <<
			"usage: FrameGenerationCameraTests <FidelityFX.cpp> <Engine.h> <Upscaling.cpp>\n";
		return 2;
	}

	TestBasisUsesColumns();
	TestFov(0.5f, -0.5f, "symmetric vertical FOV survives a rotated view");
	TestFov(0.7f, -0.5f, "asymmetric vertical FOV survives a rotated view");
	TestFovRejection();
	TestSuperResolutionFovCache();
	TestSourceContracts(argv[1], argv[2], argv[3]);

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Frame-generation camera checks passed\n";
	return 0;
}
