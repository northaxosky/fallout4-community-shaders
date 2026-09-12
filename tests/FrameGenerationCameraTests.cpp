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

	std::string_view Between(
		std::string_view a_source,
		std::string_view a_startMarker,
		std::string_view a_endMarker)
	{
		const auto start = a_source.find(a_startMarker);
		if (start == std::string_view::npos) {
			return {};
		}
		const auto end = a_source.find(a_endMarker, start);
		return end == std::string_view::npos
			? std::string_view{}
			: a_source.substr(start, end - start);
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
		const std::filesystem::path& a_upscalingPath,
		const std::filesystem::path& a_pipelinePath,
		const std::filesystem::path& a_presentationPath,
		const std::filesystem::path& a_capturePath,
		const std::filesystem::path& a_streamlinePath)
	{
		const auto fidelityFx = ReadFile(a_fidelityFxPath);
		Check(
			!fidelityFx.contains("TryGetCameraMatrices") &&
				!fidelityFx.contains(".invView") &&
				!fidelityFx.contains("cameraState.posAdjust"),
			"frame generation does not use timing-dependent camera transforms");
		const auto upscaling = ReadFile(a_upscalingPath) + ReadFile(a_capturePath);
		const auto pipeline = ReadFile(a_pipelinePath);
		const auto presentation = ReadFile(a_presentationPath);
		const auto streamline = ReadFile(a_streamlinePath);
		const auto dx12SwapChain =
			ReadFile(a_fidelityFxPath.parent_path() / "DX12SwapChain.cpp");
		Check(
			fidelityFx.contains("dispatchParameters.jitterOffset.x = -a_context.jitterX") &&
				fidelityFx.contains("dispatchParameters.jitterOffset.y = -a_context.jitterY"),
			"FSR uses the geometry-jitter sign required by its SDK contract");
		Check(
			streamline.contains("eUseFrameBasedResourceTagging") &&
				streamline.contains("slSetTagForFrame(*frameToken, vp,") &&
				!streamline.contains("slSetTag(vp,"),
			"native DLSS uses the frame-token tagging API required by its session");
		const auto constantsCall = streamline.find("slSetConstants(");
		Check(
			constantsCall != std::string::npos &&
				constantsCall == streamline.rfind("slSetConstants(") &&
				streamline.contains("_constantsFrame == a_frameIndex"),
			"SR and FG share one frame-keyed common-constants publisher");
		const auto simulationStart = Between(
			pipeline,
			"void TemporalPipeline::BeginSimulation()",
			"void TemporalPipeline::EndSimulationAndBeginRenderSubmit()");
		const auto simulationMarker =
			simulationStart.find("temporal::LatencyMarker::kSimulationStart");
		const auto inputMarker =
			simulationStart.find("temporal::LatencyMarker::kInputSample");
		Check(
			simulationMarker != std::string::npos &&
				inputMarker != std::string::npos &&
				simulationMarker < inputMarker,
			"simulation start precedes the optional input marker");
		const auto presentBlock = Between(
			dx12SwapChain,
			"HRESULT DX12SwapChain::PresentImpl(",
			"void DX12SwapChain::ClearSharedBuffers(");
		const auto presentCall =
			presentBlock.find("presentResult = _swapChain->Present(");
		const auto signal = presentBlock.find("_queue->Signal(");
		const auto wait = presentBlock.find("_context11->Wait(");
		const auto clear = presentBlock.find("ClearSharedBuffers(false)");
		Check(
			presentCall != std::string::npos && signal != std::string::npos &&
				wait != std::string::npos && clear != std::string::npos &&
				signal < wait && wait < presentCall && presentCall < clear,
			"application-command-list copies are fenced before provider Present");
		Check(
			upscaling.contains("cs::engine::GetFrameBuffer()") &&
				pipeline.contains("cs::engine::CameraWorldOrigin(snapshot.data)") &&
				pipeline.contains("cs::engine::GetCameraWorldBasis(snapshot.data)") &&
				pipeline.contains("camera.engineFrame = snapshot.frameCount") &&
				presentation.contains("SetFrameGenerationCameraData(camera)") &&
				!fidelityFx.contains("cs::engine::GetFrameBuffer()"),
			"engine integration passes canonical b12 camera data through the typed provider boundary");
		Check(
			upscaling.contains(".depth = superResolutionDepthTexture->resource.get()") &&
				upscaling.contains("superResolutionDepthTexture->uav.get()"),
			"SR passes typed depth-copy output instead of sharing the engine depth-stencil resource");
		const auto upscaleStart = fidelityFx.find("bool FidelityFX::Upscale(");
		const auto upscaleDispatch =
			fidelityFx.find("ffxFsr3ContextDispatchUpscale", upscaleStart);
		const auto upscaleFov =
			fidelityFx.find(
				"dispatchParameters.cameraFovAngleVertical = a_context.cameraVerticalFov;",
				upscaleStart);
		Check(
			upscaleStart != std::string::npos &&
				upscaleDispatch != std::string::npos &&
				upscaleFov > upscaleStart &&
				upscaleFov < upscaleDispatch &&
				fidelityFx.contains(
					"dispatchParameters.frameTimeDelta = a_context.frameTimeMilliseconds;") &&
				!fidelityFx.contains("superResolutionFovCache") &&
				upscaling.contains(
					"superResolutionFovCache.Resolve("),
			"FSR receives caller-resolved camera and timing values through its typed context");
		const auto frameGenerationStart =
			fidelityFx.find("bool FidelityFX::SetFrameGenerationCameraData(");
		const auto frameGenerationEnd =
			fidelityFx.find("void FidelityFX::ResetFrameGenerationCameraData()", frameGenerationStart);
		const auto frameGenerationBlock =
			frameGenerationStart != std::string::npos && frameGenerationEnd != std::string::npos
			? std::string_view(fidelityFx).substr(
				  frameGenerationStart,
				  frameGenerationEnd - frameGenerationStart)
			: std::string_view{};
		Check(
			frameGenerationBlock.contains("a_camera.valid") &&
				!frameGenerationBlock.contains("superResolutionFovCache") &&
				pipeline.contains(
					"cs::engine::VerticalFieldOfViewFromWorldToClip("),
			"frame generation validates a strictly current caller-owned camera snapshot");

		const auto engine = ReadFile(a_enginePath);
		Check(
			!engine.contains("CameraMatrices") &&
				!engine.contains("TryGetCameraMatrices") &&
				!engine.contains("GetVerticalFOV") &&
				!engine.contains("camViewData"),
			"Engine.h contains no timing-dependent camera transform accessors");

		Check(
			upscaling.contains(
				"ResetFsrFrameGenerationCamera();") &&
				pipeline.contains(
					"_impl->fidelityFX.ResetFrameGenerationCameraData();"),
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
	if (argc != 8) {
		std::cerr <<
			"usage: FrameGenerationCameraTests <FidelityFX.cpp> <Engine.h> "
			"<TemporalResolve.cpp> <TemporalPipeline.cpp> <PresentationProviders.cpp> "
			"<TemporalFrameGenerationInputs.cpp> <Streamline.cpp>\n";
		return 2;
	}

	TestBasisUsesColumns();
	TestFov(0.5f, -0.5f, "symmetric vertical FOV survives a rotated view");
	TestFov(0.7f, -0.5f, "asymmetric vertical FOV survives a rotated view");
	TestFovRejection();
	TestSuperResolutionFovCache();
	TestSourceContracts(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Frame-generation camera checks passed\n";
	return 0;
}
