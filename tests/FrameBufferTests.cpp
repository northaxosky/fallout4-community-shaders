#include "Render/FrameBufferMath.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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
		double a_actual,
		double a_expected,
		double a_tolerance,
		std::string_view a_message)
	{
		if (!std::isfinite(a_actual)
			|| std::abs(a_actual - a_expected) > a_tolerance) {
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

	constexpr std::size_t kRegister = sizeof(DirectX::XMFLOAT4);

	// A right-handed basis with a translation, so a swizzle or transposition cannot pass.
	constexpr DirectX::XMFLOAT4 kRows[3]{
		{ 0.0f, 0.0f, 1.0f, 10.0f },
		{ 1.0f, 0.0f, 0.0f, 20.0f },
		{ 0.0f, 1.0f, 0.0f, 30.0f }
	};

	void TestLayout()
	{
		using cs::engine::FrameBuffer;
		Check(
			sizeof(FrameBuffer) == 47 * kRegister,
			"the mirror spans the observed 47 b12 registers");
		Check(
			offsetof(FrameBuffer, ViewToWorld) == 12 * kRegister,
			"ViewToWorld starts at b12 register 12");
		Check(
			offsetof(FrameBuffer, FarReproj) == 20 * kRegister,
			"FarReproj starts at b12 register 20");
		Check(
			offsetof(FrameBuffer, NearReproj) == 24 * kRegister,
			"NearReproj starts at b12 register 24");
		Check(
			offsetof(FrameBuffer, IblDesaturation) == 30 * kRegister,
			"IblDesaturation sits at b12 register 30");
		Check(
			offsetof(FrameBuffer, PrevFrameWorldToClip) == 31 * kRegister,
			"PrevFrameWorldToClip starts at b12 register 31");
		Check(
			offsetof(FrameBuffer, CameraPosAdjust) == 35 * kRegister,
			"CameraPosAdjust sits at b12 register 35");
		Check(
			offsetof(FrameBuffer, CameraPreviousPosAdjust) == 36 * kRegister,
			"CameraPreviousPosAdjust sits at b12 register 36");
		Check(
			offsetof(FrameBuffer, CurrFrameWorldToClip) == 37 * kRegister,
			"CurrFrameWorldToClip starts at b12 register 37");
		Check(
			offsetof(FrameBuffer, FogDistanceRamp) == 41 * kRegister,
			"the fog block starts at b12 register 41");
		Check(
			offsetof(FrameBuffer, FogHeightRamp) == 46 * kRegister,
			"FogHeightRamp sits at b12 register 46");

		FrameBuffer frameBuffer{};
		for (std::size_t index = 0; index < cs::engine::kFrameBufferRegisters; ++index) {
			reinterpret_cast<DirectX::XMFLOAT4*>(&frameBuffer)[index].x =
				static_cast<float>(index);
		}

		CheckNear(
			cs::engine::FrameBufferRegister(frameBuffer, 35).x,
			35.0,
			1e-6,
			"register indexing reaches CameraPosAdjust");
		CheckNear(
			frameBuffer.CameraPosAdjust.x,
			35.0,
			1e-6,
			"the named field and the indexed register are the same storage");
	}

	void TestCameraOrigin()
	{
		cs::engine::FrameBuffer frameBuffer{};
		for (std::size_t row = 0; row < 3; ++row) {
			frameBuffer.ViewToWorld[row] = kRows[row];
		}
		frameBuffer.CameraPosAdjust = { 100.0f, 200.0f, 300.0f, 0.0f };
		frameBuffer.CameraPreviousPosAdjust = { 90.0f, 190.0f, 290.0f, 0.0f };
		const auto origin = cs::engine::CameraWorldOrigin(frameBuffer);
		const auto previousOrigin = cs::engine::CameraPreviousWorldOrigin(frameBuffer);
		CheckNear(origin.x, 110.0, 1e-5, "camera origin folds in row 0 w");
		CheckNear(origin.y, 220.0, 1e-5, "camera origin folds in row 1 w");
		CheckNear(origin.z, 330.0, 1e-5, "camera origin folds in row 2 w");
		CheckNear(previousOrigin.x, 90.0, 1e-5, "previous origin comes from register 36 x");
		CheckNear(previousOrigin.y, 190.0, 1e-5, "previous origin comes from register 36 y");
		CheckNear(previousOrigin.z, 290.0, 1e-5, "previous origin comes from register 36 z");
		Check(
			cs::engine::HasNonzeroWorldCameraOrigin(frameBuffer),
			"a normal world origin passes the known-bad signature guard");

		for (std::size_t row = 0; row < 3; ++row) {
			frameBuffer.ViewToWorld[row].w = 0.0f;
		}
		frameBuffer.CameraPosAdjust = { 0.18f, 0.0f, 0.0f, 0.0f };
		Check(
			!cs::engine::HasNonzeroWorldCameraOrigin(frameBuffer),
			"a near-zero origin with a valid basis is rejected");
	}

	void TestPositionReconstruction()
	{
		const DirectX::XMFLOAT3 origin{ 1000.0f, 2000.0f, 3000.0f };
		const DirectX::XMFLOAT3 view{ 2.0f, 3.0f, 5.0f };
		const auto world = cs::engine::ViewToWorldPosition(view, kRows, origin);

		// dot(row, float4(view, 1)) + origin, per component.
		CheckNear(world.x, 5.0 + 10.0 + 1000.0, 1e-4, "world x uses row 0 and the origin");
		CheckNear(world.y, 2.0 + 20.0 + 2000.0, 1e-4, "world y uses row 1 and the origin");
		CheckNear(world.z, 3.0 + 30.0 + 3000.0, 1e-4, "world z uses row 2 and the origin");

		const DirectX::XMFLOAT3 noOrigin{ 0.0f, 0.0f, 0.0f };
		const auto relative = cs::engine::ViewToWorldPosition(view, kRows, noOrigin);
		CheckNear(
			static_cast<double>(world.x) - relative.x,
			1000.0,
			1e-4,
			"the origin contributes exactly the position adjustment");
	}

	void TestDirectionReconstruction()
	{
		const DirectX::XMFLOAT3 view{ 0.0f, 0.0f, 1.0f };
		const auto direction = cs::engine::ViewToWorldDirection(view, kRows);

		// Row dots, not column weights: view +z maps to world +x for this basis.
		CheckNear(direction.x, 1.0, 1e-5, "direction x is the row-0 dot");
		CheckNear(direction.y, 0.0, 1e-5, "direction y is the row-1 dot");
		CheckNear(direction.z, 0.0, 1e-5, "direction z is the row-2 dot");

		const auto length = std::sqrt(
			static_cast<double>(direction.x) * direction.x
			+ static_cast<double>(direction.y) * direction.y
			+ static_cast<double>(direction.z) * direction.z);
		CheckNear(length, 1.0, 1e-5, "directions come back normalised");

		const DirectX::XMFLOAT3 degenerate{ 0.0f, 0.0f, 0.0f };
		const auto zero = cs::engine::ViewToWorldDirection(degenerate, kRows);
		Check(
			zero.x == 0.0f && zero.y == 0.0f && zero.z == 0.0f,
			"a zero-length direction does not divide by zero");
	}

	void TestProjectionClassifier()
	{
		const DirectX::XMFLOAT4 orthographic{ 0.0f, 0.0f, 0.0f, 1.0f };
		const DirectX::XMFLOAT4 perspective{ 0.0f, 0.0f, -1.0f, 0.0f };
		Check(
			!cs::engine::IsPerspectiveProjection(orthographic),
			"an orthographic shadow camera is classified as such");
		Check(
			cs::engine::IsPerspectiveProjection(perspective),
			"a perspective world camera is classified as such");
	}

	void TestCameraBasisGuard()
	{
		cs::engine::FrameBuffer frameBuffer{};
		Check(
			!cs::engine::HasUsableCameraBasis(frameBuffer),
			"a never-snapshotted zero buffer is rejected");

		for (std::size_t row = 0; row < 3; ++row) {
			frameBuffer.ViewToWorld[row] = kRows[row];
		}
		Check(
			cs::engine::HasUsableCameraBasis(frameBuffer),
			"a populated basis is accepted");
		frameBuffer.CurrFrameWorldToClip[3] = { 0.0f, 0.0f, -1.0f, 0.0f };
		Check(
			cs::engine::HasUsableWorldCamera(frameBuffer),
			"a finite basis, origin, and perspective projection are accepted");

		frameBuffer.CameraPosAdjust.x = std::numeric_limits<float>::quiet_NaN();
		Check(
			!cs::engine::HasUsableCameraBasis(frameBuffer),
			"a non-finite position adjustment is rejected");
		frameBuffer.CameraPosAdjust.x = 0.0f;
		frameBuffer.CameraPreviousPosAdjust.z =
			std::numeric_limits<float>::quiet_NaN();
		Check(
			!cs::engine::HasUsableWorldCamera(frameBuffer),
			"a non-finite previous position adjustment is rejected");
	}

	// The whole point of this change is that neither the axis swizzle nor the C++ camera
	// derivation comes back, so pin both in source.
	void TestSourceContracts(
		const std::filesystem::path& a_ssgiCommon,
		const std::filesystem::path& a_ssgiDisocclusion,
		const std::filesystem::path& a_ssgiSource,
		const std::filesystem::path& a_contracts)
	{
		const auto common = ReadFile(a_ssgiCommon);
		Check(
			!common.contains(".zyx"),
			"the SSGI camera path applies no axis swizzle");
		Check(
			common.contains("dot(rows[0].xyz, direction)"),
			"SSGI dots directions with the engine rows instead of weighting them as columns");
		Check(
			common.contains("float4 CameraOrigin;")
				&& common.contains("float4 PrevCameraOrigin;"),
			"the SSGI constant buffer carries both frames' camera origins");

		const auto disocclusion = ReadFile(a_ssgiDisocclusion);
		Check(
			!disocclusion.contains("ViewToCameraRelativeWorld"),
			"disocclusion no longer works in camera-relative space");
		Check(
			disocclusion.contains("ViewToWorldPosition(prevView, PrevViewToWorld, PrevCameraOrigin.xyz)")
				&& disocclusion.contains("ViewToWorld, CameraOrigin.xyz"),
			"disocclusion compares absolute world positions from both frames");

		const auto ssgi = ReadFile(a_ssgiSource);
		Check(
			!ssgi.contains("world.rotate")
				&& !ssgi.contains("cameraState.posAdjust")
				&& !ssgi.contains("GetWorldRootCamera"),
			"SSGI derives no camera basis in C++");
		Check(
			ssgi.contains("cs::engine::GetFrameBuffer()"),
			"SSGI reads its camera from the engine constant buffer snapshot");
		Check(
			ssgi.contains("camera.rows[row * 4 + 3] = 0.0f;")
				&& ssgi.contains("cs::engine::CameraWorldOrigin(frameBuffer.data)"),
			"SSGI folds row w into the origin without applying it twice");
		Check(
			ssgi.contains("cs::engine::CameraPreviousWorldOrigin(frameBuffer.data)")
				&& !ssgi.contains("_prevCamera.origin"),
			"SSGI reads the previous origin from b12 instead of carrying it in C++");

		const auto contracts = ReadFile(a_contracts);
		Check(
			contracts.contains("float4 cb12_pad_0_11[12];")
				&& contracts.contains("float4 ViewToWorld_row0;")
				&& contracts.contains("float4 cb12_pad_15_19[5];"),
			"the C++ mirror still matches the shared b12 contract block");
	}
}

int main(int argc, char** argv)
{
	if (argc < 5) {
		std::cerr << "usage: FrameBufferTests <ssgi common.hlsli> <radianceDisocc.cs.hlsl>"
					 " <ScreenSpaceGI.cpp> <DeferredContracts.hlsli>\n";
		return 2;
	}

	TestLayout();
	TestCameraOrigin();
	TestPositionReconstruction();
	TestDirectionReconstruction();
	TestProjectionClassifier();
	TestCameraBasisGuard();
	TestSourceContracts(argv[1], argv[2], argv[3], argv[4]);

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "FrameBuffer checks passed\n";
	return 0;
}
