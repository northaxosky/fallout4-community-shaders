#include "Render/Engine.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <winrt/base.h>

namespace
{
	bool drawAnchorInstallFails = false;
	bool deferredLightsActive = true;
	std::uint32_t sharedDataBindCount = 0;
	std::uint32_t computeContributionBindCount = 0;
	std::optional<bool> activeComputeVariantDefine;
	std::array<winrt::com_ptr<ID3D11Buffer>, 2> publishedComputeBuffers;
	std::atomic<std::uint32_t> compilationAttempts{ 0 };
	bool forceCompilationFailure = false;
	bool holdCompilationPending = false;

	struct TestNativeMacro
	{
		const char* name;
		const char* value;
	};

	TestNativeMacro* EmitBlurMacros(
		RE::BSShader*,
		TestNativeMacro* a_output)
	{
		a_output[0] = { "TEXTAP", "7" };
		a_output[1] = { "BRIGHTPASS", "" };
		a_output[2] = { nullptr, nullptr };
		return a_output;
	}

	class TestCompilationHandle final :
		public cs::engine::ShaderVariantCompilationHandle
	{
	public:
		TestCompilationHandle(
			winrt::com_ptr<ID3D11DeviceChild> a_shader,
			cs::engine::ShaderVariantCompilationCompletion a_completion) :
			shader(std::move(a_shader)),
			completion(std::move(a_completion))
		{}

		cs::engine::ShaderVariantCompilationState
			GetState() const noexcept override
		{
			return state.load(std::memory_order_acquire);
		}

		winrt::com_ptr<ID3D11DeviceChild> Acquire() noexcept override
		{
			return GetState()
					== cs::engine::ShaderVariantCompilationState::kReady ?
				shader :
				nullptr;
		}

		std::string GetError() const override
		{
			if (GetState()
				!= cs::engine::ShaderVariantCompilationState::kFailed) {
				return {};
			}
			const std::scoped_lock lock(mutex);
			return error;
		}

		void Succeed()
		{
			Complete(cs::engine::ShaderVariantCompilationState::kReady, {});
		}

		void Fail(std::string a_error)
		{
			Complete(
				cs::engine::ShaderVariantCompilationState::kFailed,
				std::move(a_error));
		}

	private:
		void Complete(
			cs::engine::ShaderVariantCompilationState a_state,
			std::string a_error)
		{
			cs::engine::ShaderVariantCompilationCompletion notify;
			{
				const std::scoped_lock lock(mutex);
				error = std::move(a_error);
				notify = std::move(completion);
			}
			state.store(a_state, std::memory_order_release);
			if (notify)
				notify(a_state, error);
		}

		winrt::com_ptr<ID3D11DeviceChild> shader;
		std::atomic<cs::engine::ShaderVariantCompilationState> state{
			cs::engine::ShaderVariantCompilationState::kPending
		};
		mutable std::mutex mutex;
		std::string error;
		cs::engine::ShaderVariantCompilationCompletion completion;
	};

	std::shared_ptr<TestCompilationHandle> pendingCompilation;

	class TestCompilationCache final :
		public cs::engine::ShaderVariantCompilationCache
	{
	public:
		std::shared_ptr<cs::engine::ShaderVariantCompilationHandle> Request(
			cs::engine::ShaderVariantCompilationRequest a_request) override
		{
			using namespace cs::engine;
			compilationAttempts.fetch_add(1, std::memory_order_relaxed);
			if (!a_request.device) {
				auto handle = std::make_shared<TestCompilationHandle>(
					nullptr, std::move(a_request.completion));
				handle->Fail("missing test device");
				return handle;
			}
			if (forceCompilationFailure) {
				auto handle = std::make_shared<TestCompilationHandle>(
					nullptr, std::move(a_request.completion));
				handle->Fail("controlled native compilation failure");
				return handle;
			}

			const auto profile =
				a_request.stage == ShaderStage::kVertex ? "vs_5_0" :
				a_request.stage == ShaderStage::kCompute ? "cs_5_0" :
				"ps_5_0";
			const std::string_view source =
				a_request.stage == ShaderStage::kVertex ?
					"float4 main(uint id : SV_VertexID) : SV_Position { "
					"return float4(id == 2 ? 3.0 : -1.0, "
					"id == 1 ? 3.0 : -1.0, 0.0, 1.0); }" :
				a_request.stage == ShaderStage::kCompute ?
					"cbuffer SharedData : register(b5) { uint SharedValue; };"
					"cbuffer FeatureData : register(b6) { uint FeatureValue; };"
					"cbuffer NativeData : register(b7) { uint NativeValue; };"
					"Texture2D<uint> NativeTexture : register(t3);"
					"RWStructuredBuffer<uint> Output : register(u0);"
					"[numthreads(1,1,1)] void main() {"
					"InterlockedAdd(Output[0], 1);"
					"Output[1] = SharedValue;"
					"Output[2] = FeatureValue;"
					"Output[3] = NativeValue;"
					"Output[4] = NativeTexture.Load(int3(0,0,0));"
					"}" :
					"float4 main() : SV_Target { return 1.0; }";
			winrt::com_ptr<ID3DBlob> bytecode;
			winrt::com_ptr<ID3DBlob> errors;
			const auto compileResult = D3DCompile(
				source.data(),
				source.size(),
				nullptr,
				nullptr,
				nullptr,
				"main",
				profile,
				0,
				0,
				bytecode.put(),
				errors.put());
			if (FAILED(compileResult) || !bytecode) {
				const auto error = errors ?
					std::string(
						static_cast<const char*>(errors->GetBufferPointer()),
						errors->GetBufferSize()) :
					"test shader compilation failed";
				auto handle = std::make_shared<TestCompilationHandle>(
					nullptr, std::move(a_request.completion));
				handle->Fail(error);
				return handle;
			}

			winrt::com_ptr<ID3D11DeviceChild> shader;
			HRESULT createResult = E_FAIL;
			if (a_request.stage == ShaderStage::kVertex) {
				winrt::com_ptr<ID3D11VertexShader> typed;
				createResult = a_request.device->CreateVertexShader(
					bytecode->GetBufferPointer(),
					bytecode->GetBufferSize(),
					nullptr,
					typed.put());
				if (typed)
					shader.attach(typed.detach());
			} else if (a_request.stage == ShaderStage::kCompute) {
				winrt::com_ptr<ID3D11ComputeShader> typed;
				createResult = a_request.device->CreateComputeShader(
					bytecode->GetBufferPointer(),
					bytecode->GetBufferSize(),
					nullptr,
					typed.put());
				if (typed)
					shader.attach(typed.detach());
			} else {
				winrt::com_ptr<ID3D11PixelShader> typed;
				createResult = a_request.device->CreatePixelShader(
					bytecode->GetBufferPointer(),
					bytecode->GetBufferSize(),
					nullptr,
					typed.put());
				if (typed)
					shader.attach(typed.detach());
			}
			if (FAILED(createResult) || !shader) {
				auto handle = std::make_shared<TestCompilationHandle>(
					nullptr, std::move(a_request.completion));
				handle->Fail("test shader creation failed");
				return handle;
			}

			auto handle = std::make_shared<TestCompilationHandle>(
				std::move(shader), std::move(a_request.completion));
			if (holdCompilationPending) {
				pendingCompilation = handle;
			} else {
				handle->Succeed();
			}
			return handle;
		}

		void Invalidate() override {}

		void Stop() noexcept override {}
	};
}

namespace cs::log
{
	spdlog::logger* Get(const char*)
	{
		return spdlog::default_logger_raw();
	}
}

namespace cs::render
{
	void EnsureSharedDataUpdateInstalled()
	{}

	bool IsSharedDataReady() noexcept
	{
		return true;
	}

	void BindSharedData(
		ID3D11DeviceContext* a_context,
		cs::engine::ShaderStage a_stage) noexcept
	{
		if (a_context && a_stage == cs::engine::ShaderStage::kCompute) {
			++sharedDataBindCount;
			ID3D11Buffer* buffers[]{
				publishedComputeBuffers[0].get(),
				publishedComputeBuffers[1].get()
			};
			a_context->CSSetConstantBuffers(
				cs::render::kSharedDataSlot, 2, buffers);
		}
	}

	bool IsDeferredLightsActive() noexcept
	{
		return deferredLightsActive;
	}
}

namespace cs::engine
{
	std::shared_ptr<ShaderVariantCompilationCache>
		CreateCachingShaderVariantCompilationCache()
	{
		return std::make_shared<TestCompilationCache>();
	}

	bool EnsureDeferredDrawAnchorInstalled()
	{
		return !drawAnchorInstallFails;
	}
}

namespace
{
	int failures = 0;

	void Expect(bool a_condition, const char* a_message)
	{
		if (a_condition)
			return;
		std::cerr << "FAIL: " << a_message << '\n';
		++failures;
	}

	void CheckPrepassDescriptor()
	{
		using namespace cs::engine;
		const auto descriptor = BuildShaderFamilyCompilationDescriptor({
			.target = ShaderInjectionTarget::kDeferredPrepass,
			.stage = ShaderStage::kVertex,
			.descriptor =
				(1U << 1) | (1U << 3) | (1U << 4) | (1U << 5)
				| (1U << 25),
		});
		Expect(descriptor.has_value(), "PrePass stage key was rejected");
		if (!descriptor)
			return;
		Expect(descriptor->defines.contains("TEXTURE"), "TEXTURE bit was lost");
		Expect(descriptor->defines.contains("NORMALS"), "NORMALS bit was lost");
		Expect(
			descriptor->defines.contains("BINORMAL_TANGENT"),
			"BINORMAL_TANGENT bit was lost");
		Expect(
			descriptor->defines.contains("LOD_LANDSCAPE"),
			"normalized vertex LOD_LANDSCAPE bit was lost");
	}

	void CheckPrepassTextureSemantic()
	{
		using namespace cs::engine;
		const auto descriptor = BuildShaderFamilyCompilationDescriptor({
			.target = ShaderInjectionTarget::kDeferredPrepass,
			.stage = ShaderStage::kPixel,
			.descriptor = 1U << 13,
			.forceEarlyDepthStencil = true,
		});
		Expect(
			descriptor.has_value(),
			"PrePass semantic descriptor was rejected");
		if (!descriptor)
			return;
		Expect(
			descriptor->defines.at("TEXTURE") == "0",
			"native stage-key no-texture bit was not preserved");
		Expect(
			descriptor->defines.contains("EARLYDEPTH"),
			"reflected early-depth semantic was not preserved");
		const auto textured = BuildShaderFamilyCompilationDescriptor({
			.target = ShaderInjectionTarget::kDeferredPrepass,
			.stage = ShaderStage::kPixel,
			.descriptor = (1U << 13) | (1U << 1),
		});
		Expect(
			textured && textured->defines.at("TEXTURE") == "1",
			"native stage-key texture bit was not preserved");
	}

	void CheckUnprovenPrepassSplineGrassDescriptors()
	{
		using namespace cs::engine;
		constexpr std::uint32_t descriptors[]{
			0x0080049BU,
			0x0180049BU
		};
		for (const auto descriptor : descriptors) {
			Expect(
				!BuildShaderFamilyCompilationDescriptor({
					.target = ShaderInjectionTarget::kDeferredPrepass,
					.stage = ShaderStage::kVertex,
					.descriptor = descriptor,
				}).has_value(),
				"unproven PrePass spline grass descriptor was admitted");
		}
	}

	void CheckContributorConflict()
	{
		using namespace cs::engine;
		const auto* target =
			GetShaderInjectionTarget(ShaderInjectionTarget::kBsLighting);
		Expect(target != nullptr, "BSLighting target metadata is missing");
		if (!target)
			return;
		const ShaderVariantCompilationDescriptor family{
			.sourcePath = L"BSLightingShader.hlsl",
			.entryPoint = "main",
			.profile = "ps_5_0",
			.defines = { { "CONFLICT", "family" } },
		};
		const ShaderReplacementRegistration contribution{
			.targetId = ShaderInjectionTarget::kBsLighting,
			.stages = ShaderStageBit(ShaderStage::kPixel),
			.contributor = "test",
			.defines = { { "CONFLICT", "feature" } },
		};
		std::string error;
		const auto request = BuildEffectiveShaderCompileRequest(
			*target,
			ShaderStage::kPixel,
			family,
			std::span(&contribution, 1),
			&error);
		Expect(!request.has_value(), "conflicting defines were accepted");
		Expect(!error.empty(), "conflicting defines did not report an error");
	}

	void CheckComputeDescriptor()
	{
		using namespace cs::engine;
		const auto descriptor = BuildShaderFamilyCompilationDescriptor({
			.target = ShaderInjectionTarget::kDfTiledLighting,
			.stage = ShaderStage::kCompute,
			.descriptor = 1,
			.nativeName = "DFTiledLighting",
		});
		Expect(descriptor.has_value(), "DFTiled compute descriptor was rejected");
		if (!descriptor)
			return;
		Expect(
			descriptor->profile == "cs_5_0",
			"DFTiled compute descriptor selected the wrong profile");
	}

	bool CreateWarpDevice(
		winrt::com_ptr<ID3D11Device>& a_device,
		winrt::com_ptr<ID3D11DeviceContext>& a_context)
	{
		constexpr D3D_FEATURE_LEVEL featureLevels[]{
			D3D_FEATURE_LEVEL_11_0
		};
		return SUCCEEDED(D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			0,
			featureLevels,
			static_cast<UINT>(std::size(featureLevels)),
			D3D11_SDK_VERSION,
			a_device.put(),
			nullptr,
			a_context.put()))
			&& a_device && a_context;
	}

	winrt::com_ptr<ID3DBlob> CompileStrippedShader(
		std::string_view a_source,
		const char* a_profile)
	{
		winrt::com_ptr<ID3DBlob> compiled;
		winrt::com_ptr<ID3DBlob> errors;
		if (FAILED(D3DCompile(
				a_source.data(),
				a_source.size(),
				nullptr,
				nullptr,
				nullptr,
				"main",
				a_profile,
				0,
				0,
				compiled.put(),
				errors.put()))
			|| !compiled) {
			if (errors) {
				std::cerr.write(
					static_cast<const char*>(errors->GetBufferPointer()),
					static_cast<std::streamsize>(errors->GetBufferSize()));
			}
			return {};
		}

		winrt::com_ptr<ID3DBlob> stripped;
		if (FAILED(D3DStripShader(
				compiled->GetBufferPointer(),
				compiled->GetBufferSize(),
				D3DCOMPILER_STRIP_REFLECTION_DATA,
				stripped.put()))) {
			return {};
		}
		return stripped;
	}

	void CheckNativeShaderRuntimeLayoutAndMacros()
	{
		using namespace cs::engine;
		alignas(std::max_align_t)
			std::array<std::byte, 0x260> modernStorage{};
		auto* modernShader = reinterpret_cast<RE::BSShader*>(
			modernStorage.data());
		auto* modernVertex = reinterpret_cast<native::VertexShaderMap*>(
			modernStorage.data() + 0x98);
		auto* modernPixel = reinterpret_cast<native::PixelShaderMap*>(
			modernStorage.data() + 0x128);
		auto* modernCompute = reinterpret_cast<native::ComputeShaderMap*>(
			modernStorage.data() + 0x158);
		const char* modernFilename = "DFPrePass";
		std::memcpy(
			modernStorage.data() + 0x188,
			&modernFilename,
			sizeof(modernFilename));
		Expect(
			&native::VertexShadersForTesting(modernShader, true)
				== modernVertex,
			"modern BSShader vertex-map offset was not selected");
		Expect(
			&native::PixelShadersForTesting(modernShader, true)
				== modernPixel,
			"modern BSShader pixel-map offset was not selected");
		Expect(
			&native::ComputeShadersForTesting(modernShader, true)
				== modernCompute,
			"modern BSShader compute-map offset was not selected");
		Expect(
			native::FxpFilenameForTesting(modernShader, true)
				== modernFilename,
			"modern BSShader filename offset was not selected");
		alignas(std::max_align_t)
			std::array<std::byte, 0x130> originalStorage{};
		auto* originalShader = reinterpret_cast<RE::BSShader*>(
			originalStorage.data());
		auto* originalVertex = reinterpret_cast<native::VertexShaderMap*>(
			originalStorage.data() + 0x20);
		auto* originalPixel = reinterpret_cast<native::PixelShaderMap*>(
			originalStorage.data() + 0xB0);
		auto* originalCompute = reinterpret_cast<native::ComputeShaderMap*>(
			originalStorage.data() + 0xE0);
		const char* originalFilename = "DFPrePass";
		std::memcpy(
			originalStorage.data() + 0x110,
			&originalFilename,
			sizeof(originalFilename));
		Expect(
			&native::VertexShadersForTesting(originalShader, false)
				== originalVertex,
			"original BSShader vertex-map offset was not selected");
		Expect(
			&native::PixelShadersForTesting(originalShader, false)
				== originalPixel,
			"original BSShader pixel-map offset was not selected");
		Expect(
			&native::ComputeShadersForTesting(originalShader, false)
				== originalCompute,
			"original BSShader compute-map offset was not selected");
		Expect(
			native::FxpFilenameForTesting(originalShader, false)
				== originalFilename,
			"original BSShader filename offset was not selected");
		alignas(std::max_align_t)
			std::array<std::byte, 0x80> standaloneComputeStorage{};
		Expect(
			&native::StandaloneComputeShaders(
				standaloneComputeStorage.data())
				== reinterpret_cast<native::ComputeShaderMap*>(
					standaloneComputeStorage.data() + 0x20),
			"standalone compute owner map offset was not selected");
		const char* standaloneName = "IndexBufferOffsetCS";
		std::memcpy(
			standaloneComputeStorage.data() + 0x18,
			&standaloneName,
			sizeof(standaloneName));
		Expect(
			native::StandaloneComputeOwnerName(
				standaloneComputeStorage.data())
				== standaloneName,
			"standalone compute owner name offset was not selected");
		alignas(std::max_align_t)
			std::array<std::byte, 0x260> imageSpaceStorage{};
		auto* imageSpaceShader = reinterpret_cast<RE::BSShader*>(
			imageSpaceStorage.data());
		std::array<std::uintptr_t, 18> vtable{};
		vtable[17] = reinterpret_cast<std::uintptr_t>(&EmitBlurMacros);
		auto* vtablePointer = vtable.data();
		std::memcpy(
			imageSpaceStorage.data(),
			&vtablePointer,
			sizeof(vtablePointer));
		const std::int32_t shaderType = 0xC;
		std::memcpy(
			imageSpaceStorage.data() + 0x18,
			&shaderType,
			sizeof(shaderType));
		const char* sourceGroup = "ISBlur";
		const char* imageSpaceClass =
			"BSImagespaceShaderBrightPassBlur7";
		std::memcpy(
			imageSpaceStorage.data() + 0x240,
			&imageSpaceClass,
			sizeof(imageSpaceClass));
		std::memcpy(
			imageSpaceStorage.data() + 0x248,
			&sourceGroup,
			sizeof(sourceGroup));
		const auto emitted =
			native::GetImageSpaceMacrosForTesting(imageSpaceShader);
		Expect(
			emitted && emitted->count == 2
				&& emitted->values[0]
					== std::pair<std::string, std::string>(
						"TEXTAP", "7")
				&& emitted->values[1]
					== std::pair<std::string, std::string>(
						"BRIGHTPASS", ""),
			"ImageSpace native macro emitter output was not copied");
		Expect(
			native::ImageSpaceShaderPrefixForTesting(imageSpaceShader)
				== sourceGroup,
			"ImageSpace native source-group offset was not selected");
		Expect(
			native::ImageSpaceShaderClassNameForTesting(imageSpaceShader)
				== imageSpaceClass,
			"ImageSpace native class-name offset was not selected");
		if (!emitted)
			return;

		ShaderInjectionDefines macros;
		for (std::size_t index = 0;
			index < emitted->count;
			++index) {
			macros.insert_or_assign(
				emitted->values[index].first,
				emitted->values[index].second);
		}
		const auto descriptor = BuildShaderFamilyCompilationDescriptor({
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeClassName = imageSpaceClass,
			.nativeSourceGroup = sourceGroup,
			.nativeMacros = std::move(macros)
		});
		Expect(
			descriptor
				&& descriptor->defines.at(
					"IMAGESPACE_TAPARRAY_TAP_COUNT")
					== "7"
				&& descriptor->defines.at(
					"IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE")
					== "1",
			"ImageSpace native macros were not lowered to source defines");
		Expect(
			!BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kVertex,
				.nativeClassName =
					"BSImagespaceShaderUnproved"
			}),
			"unproved ImageSpace owner inherited a signature-only vertex route");
		Expect(
			BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kPixel,
				.nativeClassName =
					"BSImagespaceShaderVLSSliceCoord",
				.nativeSourceGroup = "ISVLS_Coord"
			}).has_value(),
			"proved VLS SliceCoord owner was not admitted");
		Expect(
			BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kPixel,
				.nativeClassName =
					"BSImagespaceShaderVLSSliceInterp",
				.nativeSourceGroup = "ISVLS",
				.nativeMacros = { { "SLICE_INTERP", "" } }
			}).has_value(),
			"proved VLS SliceInterp contract was not admitted");
		const auto fxaaVertex =
			BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kVertex,
				.nativeSourceGroup = "ISFXAA"
			});
		const auto lensVertex =
			BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kVertex,
				.nativeClassName = "BSLensFlareVis",
				.nativeSourceGroup = "LensFlare",
				.nativeMacros = { { "VISIBILITY", "" } }
			});
		const auto hudVertex =
			BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kVertex,
				.nativeSourceGroup = "ISHUDGlass",
				.nativeMacros = { { "MARKERS", "" } }
			});
		Expect(
			fxaaVertex
				&& fxaaVertex->defines.contains(
					"IMAGESPACE_PASSTHROUGH_TEXCOORD1")
				&& lensVertex
				&& lensVertex->defines.contains(
					"IMAGESPACE_XYQUAD_VS_SOURCE")
				&& hudVertex
				&& hudVertex->defines.contains(
					"IMAGESPACE_XYQUAD_PACKED"),
			"proved ImageSpace vertex source contracts were not admitted");
		Expect(
			!BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kVertex,
				.nativeSourceGroup = "LensFlare"
			}),
			"ImageSpace visibility vertex source was admitted without its native macro");
		Expect(
			!BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kPixel,
				.nativeClassName =
					"BSImagespaceShaderVLSSliceScatterRay",
				.nativeSourceGroup = "ISVLS",
				.nativeMacros = {
					{ "VLS_SLICE_SCATTER_RAY", "" }
				}
			}),
			"unreconstructed VLS SliceScatterRay was admitted");
		Expect(
			!BuildShaderFamilyCompilationDescriptor({
				.target = ShaderInjectionTarget::kImageSpace,
				.stage = ShaderStage::kPixel,
				.nativeSourceGroup = "ISGamma",
				.nativeMacros = { { "LUT", "" } }
			}),
			"unsupported ImageSpace native macro set was admitted");
	}

	void CheckObservedNativeBytecode()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create observer WARP device");
		if (!device)
			return;

		SetPixelShaderSwapBrokerDevice(device.get());
		Expect(
			PixelShaderSwapBrokerHooksInstalled(),
			"native bytecode observer hooks were not installed without a resolver");

		constexpr std::string_view texturedSource =
			"Texture2D<float4> Source : register(t0); "
			"float4 main() : SV_Target { return Source.Load(int3(0,0,0)); }";
		constexpr std::string_view untexturedEarlySource =
			"[earlydepthstencil] float4 main() : SV_Target { return 1.0; }";
		const auto texturedBytecode =
			CompileStrippedShader(texturedSource, "ps_5_0");
		const auto untexturedBytecode =
			CompileStrippedShader(untexturedEarlySource, "ps_5_0");
		Expect(
			texturedBytecode && untexturedBytecode,
			"could not compile stripped observer fixtures");
		if (!texturedBytecode || !untexturedBytecode)
			return;

		winrt::com_ptr<ID3D11PixelShader> textured;
		winrt::com_ptr<ID3D11PixelShader> untextured;
		Expect(
			SUCCEEDED(device->CreatePixelShader(
				texturedBytecode->GetBufferPointer(),
				texturedBytecode->GetBufferSize(),
				nullptr,
				textured.put()))
				&& textured,
			"could not create stripped textured pixel shader");
		Expect(
			SUCCEEDED(device->CreatePixelShader(
				untexturedBytecode->GetBufferPointer(),
				untexturedBytecode->GetBufferSize(),
				nullptr,
				untextured.put()))
				&& untextured,
			"could not create stripped untextured pixel shader");

		const auto texturedMetadata =
			GetObservedNativeShaderMetadataForTesting(textured.get());
		const auto untexturedMetadata =
			GetObservedNativeShaderMetadataForTesting(untextured.get());
		Expect(
			texturedMetadata
				&& !texturedMetadata->forceEarlyDepthStencil,
			"observer rejected a stripped pixel shader");
		Expect(
			untexturedMetadata
				&& untexturedMetadata->forceEarlyDepthStencil,
			"observer did not preserve stripped early-depth metadata");
		winrt::com_ptr<ID3D11PixelShader> bypassed;
		{
			ScopedPixelShaderBrokerBypass bypass;
			Expect(
				SUCCEEDED(device->CreatePixelShader(
					texturedBytecode->GetBufferPointer(),
					texturedBytecode->GetBufferSize(),
					nullptr,
					bypassed.put()))
					&& bypassed,
				"could not create bypassed pixel shader");
		}
		Expect(
			!GetObservedNativeShaderMetadataForTesting(bypassed.get()),
			"broker bypass recorded an injected shader as native");

	}

	void CheckClaimLedger()
	{
		using namespace cs::engine;
		constexpr std::uint32_t normalSlot = 25;
		const auto textureClaim = [](std::uint32_t a_slot) {
			return ShaderSlotClaim{
				.stage = ShaderStage::kPixel,
				.resourceType = ShaderResourceType::kShaderResource,
				.slot = a_slot
			};
		};

		ShaderReplacementRegistration first;
		first.targetId = ShaderInjectionTarget::kBsdfComposite;
		first.contributor = "ledger-first";
		first.defines = { { "LEDGER_TEST", "1" } };
		first.isReady = [] { return false; };
		first.slotClaims = { textureClaim(normalSlot) };
		Expect(RegisterReplacement(std::move(first)), "first t25 claim was rejected");

		ShaderReplacementRegistration duplicate;
		duplicate.targetId = ShaderInjectionTarget::kBsdfComposite;
		duplicate.contributor = "ledger-duplicate";
		duplicate.slotClaims = { textureClaim(normalSlot) };
		Expect(
			!RegisterReplacement(std::move(duplicate)),
			"duplicate t25 claim was accepted");

		ShaderReplacementRegistration otherTarget;
		otherTarget.targetId = ShaderInjectionTarget::kBsdfLight;
		otherTarget.contributor = "ledger-other-target";
		otherTarget.slotClaims = { textureClaim(normalSlot) };
		Expect(
			RegisterReplacement(std::move(otherTarget)),
			"same slot on another target was rejected");

		ShaderReplacementRegistration conflictingDefine;
		conflictingDefine.targetId = ShaderInjectionTarget::kBsdfComposite;
		conflictingDefine.contributor = "ledger-conflicting-define";
		conflictingDefine.defines = { { "LEDGER_TEST", "2" } };
		Expect(
			!RegisterReplacement(std::move(conflictingDefine)),
			"conflicting define claim was accepted");

		ShaderReplacementRegistration agreeingDefine;
		agreeingDefine.targetId = ShaderInjectionTarget::kBsdfComposite;
		agreeingDefine.contributor = "ledger-agreeing-define";
		agreeingDefine.defines = { { "LEDGER_TEST", "1" } };
		Expect(
			RegisterReplacement(std::move(agreeingDefine)),
			"agreeing define claim was rejected");

		for (const auto slot :
			{ cs::render::kSharedDataSlot, cs::render::kFeatureDataSlot }) {
			ShaderReplacementRegistration reserved;
			reserved.targetId = ShaderInjectionTarget::kBsdfComposite;
			reserved.contributor = "ledger-reserved-slot";
			reserved.slotClaims = { {
				.stage = ShaderStage::kPixel,
				.resourceType = ShaderResourceType::kConstantBuffer,
				.slot = slot
			} };
			Expect(
				!RegisterReplacement(std::move(reserved)),
				"reserved b5/b6 constant-buffer claim was accepted");
		}

		constexpr std::uint32_t anchorSlot = 31;
		drawAnchorInstallFails = true;
		ShaderReplacementRegistration rejectedAnchor;
		rejectedAnchor.targetId = ShaderInjectionTarget::kBsdfComposite;
		rejectedAnchor.contributor = "ledger-rejected-anchor";
		rejectedAnchor.defines = { { "LEDGER_ANCHOR", "1" } };
		rejectedAnchor.bind = [](ID3D11DeviceContext*) {};
		rejectedAnchor.slotClaims = { textureClaim(anchorSlot) };
		Expect(
			!RegisterReplacement(std::move(rejectedAnchor)),
			"registration with a failed draw anchor was accepted");
		drawAnchorInstallFails = false;

		ShaderReplacementRegistration reuseAnchor;
		reuseAnchor.targetId = ShaderInjectionTarget::kBsdfComposite;
		reuseAnchor.contributor = "ledger-anchor-reuse";
		reuseAnchor.defines = { { "LEDGER_ANCHOR", "2" } };
		reuseAnchor.slotClaims = { textureClaim(anchorSlot) };
		Expect(
			RegisterReplacement(std::move(reuseAnchor)),
			"failed draw anchor retained slot or define claims");

		constexpr std::uint32_t samplerSlot = 13;
		const auto samplerClaim = [](std::uint32_t a_slot) {
			return ShaderSlotClaim{
				.stage = ShaderStage::kPixel,
				.resourceType = ShaderResourceType::kSampler,
				.slot = a_slot
			};
		};
		ShaderReplacementRegistration sampler;
		sampler.targetId = ShaderInjectionTarget::kBsdfLight;
		sampler.contributor = "ledger-sampler";
		sampler.slotClaims = { samplerClaim(samplerSlot) };
		Expect(RegisterReplacement(std::move(sampler)), "first s13 claim was rejected");

		ShaderReplacementRegistration textureSameIndex;
		textureSameIndex.targetId = ShaderInjectionTarget::kBsdfLight;
		textureSameIndex.contributor = "ledger-texture-same-index";
		textureSameIndex.slotClaims = { textureClaim(samplerSlot) };
		Expect(
			RegisterReplacement(std::move(textureSameIndex)),
			"t13 incorrectly conflicted with s13");

		ShaderReplacementRegistration duplicateSampler;
		duplicateSampler.targetId = ShaderInjectionTarget::kBsdfLight;
		duplicateSampler.contributor = "ledger-duplicate-sampler";
		duplicateSampler.slotClaims = { samplerClaim(samplerSlot) };
		Expect(
			!RegisterReplacement(std::move(duplicateSampler)),
			"duplicate s13 claim was accepted");
	}

	void CheckLazyPreparationDoesNotDeadlock()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create lazy preparation WARP device");
		if (!device)
			return;

		SetPixelShaderSwapBrokerDevice(device.get());
		Expect(
			SetBaselineShaderOwnership(
				ShaderInjectionTarget::kBsLighting, true),
			"could not enable baseline ownership for lazy preparation");
		FreezeAndCompileShaderInjections(device.get());
		auto* prepared = PrepareNativeShaderVariantForTesting({
			.target = ShaderInjectionTarget::kBsLighting,
			.stage = ShaderStage::kPixel,
			.descriptor = 0,
			.nativeName = "BSLightingShader",
		});
		Expect(
			prepared != nullptr,
			"lazy native preparation failed after reentrant shader observation");
		if (prepared)
			prepared->Release();

		const auto nativeBytecode = CompileStrippedShader(
			"float4 main() : SV_Target { return 0.25; }",
			"ps_5_0");
		winrt::com_ptr<ID3D11PixelShader> nativeShader;
		if (nativeBytecode) {
			std::ignore = device->CreatePixelShader(
				nativeBytecode->GetBufferPointer(),
				nativeBytecode->GetBufferSize(),
				nullptr,
				nativeShader.put());
		}
		RE::BSGraphics::PixelShader nativeWrapper{};
		nativeWrapper.id = 0;
		nativeWrapper.shader =
			reinterpret_cast<REX::W32::ID3D11PixelShader*>(
				nativeShader.get());
		nativeWrapper.constantBuffers[1].data =
			reinterpret_cast<float*>(0x1234);
		nativeWrapper.constantTable[7] = 42;
		const auto mismatched =
			ResolveNativeGraphicsShaderBindingForTesting(
			ShaderInjectionTarget::kBsLighting,
			"BSLightingShader",
			0,
			1,
			nullptr,
			&nativeWrapper);
		Expect(
			mismatched.pixel == &nativeWrapper,
			"graphics boundary ignored the authoritative pixel stage ID");
		const auto selected =
			ResolveNativeGraphicsShaderBindingForTesting(
			ShaderInjectionTarget::kBsLighting,
			"BSLightingShader",
			0,
			0,
			nullptr,
			&nativeWrapper);
		auto* replacementWrapper = selected.pixel;
		Expect(
			nativeShader && replacementWrapper
				&& replacementWrapper != &nativeWrapper
				&& replacementWrapper->shader != nativeWrapper.shader
				&& replacementWrapper->id == nativeWrapper.id
				&& replacementWrapper->constantBuffers[1].data
					== nativeWrapper.constantBuffers[1].data
				&& replacementWrapper->constantTable[7]
					== nativeWrapper.constantTable[7],
			"pixel replacement wrapper did not preserve native metadata");

		const auto nativeVertexBytecode = CompileStrippedShader(
			"float4 main(float4 p : POSITION) : SV_Position { return p; }",
			"vs_5_0");
		const auto replacementVertexABytecode = CompileStrippedShader(
			"float4 main(float4 p : POSITION) : SV_Position { return p + 1; }",
			"vs_5_0");
		const auto replacementVertexBBytecode = CompileStrippedShader(
			"float4 main(float4 p : POSITION) : SV_Position { return p + 2; }",
			"vs_5_0");
		winrt::com_ptr<ID3D11VertexShader> replacementVertexA;
		winrt::com_ptr<ID3D11VertexShader> replacementVertexB;
		if (replacementVertexABytecode) {
			std::ignore = device->CreateVertexShader(
				replacementVertexABytecode->GetBufferPointer(),
				replacementVertexABytecode->GetBufferSize(),
				nullptr,
				replacementVertexA.put());
		}
		if (replacementVertexBBytecode) {
			std::ignore = device->CreateVertexShader(
				replacementVertexBBytecode->GetBufferPointer(),
				replacementVertexBBytecode->GetBufferSize(),
				nullptr,
				replacementVertexB.put());
		}
		if (nativeVertexBytecode
			&& replacementVertexA
			&& replacementVertexB) {
			std::vector<std::byte> nativeVertexStorage(
				sizeof(RE::BSGraphics::VertexShader)
				+ nativeVertexBytecode->GetBufferSize());
			auto* nativeVertex =
				reinterpret_cast<RE::BSGraphics::VertexShader*>(
					nativeVertexStorage.data());
			nativeVertex->id = 17;
			nativeVertex->byteCodeSize =
				static_cast<std::uint32_t>(
					nativeVertexBytecode->GetBufferSize());
			nativeVertex->constantTable[5] = 91;
			std::memcpy(
				nativeVertexStorage.data()
					+ sizeof(RE::BSGraphics::VertexShader),
				nativeVertexBytecode->GetBufferPointer(),
				nativeVertexBytecode->GetBufferSize());
			auto* wrapperA =
				CacheNativeVertexReplacementWrapperForTesting(
					nativeVertex, replacementVertexA.get());
			auto* wrapperAAgain =
				CacheNativeVertexReplacementWrapperForTesting(
					nativeVertex, replacementVertexA.get());
			auto* wrapperB =
				CacheNativeVertexReplacementWrapperForTesting(
					nativeVertex, replacementVertexB.get());
			const auto trailerMatches =
				wrapperA
				&& std::memcmp(
					reinterpret_cast<const std::byte*>(wrapperA)
						+ sizeof(RE::BSGraphics::VertexShader),
					nativeVertexBytecode->GetBufferPointer(),
					nativeVertexBytecode->GetBufferSize())
					== 0;
			Expect(
				wrapperA
					&& wrapperB
					&& wrapperA != nativeVertex
					&& wrapperA == wrapperAAgain
					&& wrapperB != wrapperA
					&& wrapperA->shader
						== reinterpret_cast<
							REX::W32::ID3D11VertexShader*>(
							replacementVertexA.get())
					&& wrapperB->shader
						== reinterpret_cast<
							REX::W32::ID3D11VertexShader*>(
							replacementVertexB.get())
					&& wrapperA->id == nativeVertex->id
					&& wrapperA->byteCodeSize
						== nativeVertex->byteCodeSize
					&& wrapperA->constantTable[5]
						== nativeVertex->constantTable[5]
					&& trailerMatches,
				"vertex replacement wrappers did not preserve trailers or cache transition identities");
		} else {
			Expect(
				false,
				"could not construct vertex replacement cache-transition fixture");
		}
	}

	void CheckNativeVariantOutcomeCaching()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create native outcome cache WARP device");
		if (!device)
			return;

		compilationAttempts.store(0, std::memory_order_relaxed);
		forceCompilationFailure = true;
		Expect(
			SetBaselineShaderOwnership(
				ShaderInjectionTarget::kBsLighting, true)
				&& SetBaselineShaderOwnership(
					ShaderInjectionTarget::kImageSpace, true),
			"could not enable native outcome cache targets");
		FreezeAndCompileShaderInjections(device.get());

		const ShaderFamilyDescriptor failed{
			.target = ShaderInjectionTarget::kBsLighting,
			.stage = ShaderStage::kPixel,
			.descriptor = 0,
			.nativeName = "BSLightingShader"
		};
		Expect(
			QueueNativeShaderVariantForTesting(failed),
			"failed native compilation was not queued");
		Expect(
			compilationAttempts.load(std::memory_order_relaxed) == 1
				&& GetShaderInjectionTargetSnapshot(
					ShaderInjectionTarget::kBsLighting)
					.variantsFailed
					== 1
				&& GetShaderInjectionTargetSnapshot(
					ShaderInjectionTarget::kBsLighting)
					.compileFailures
					== 1,
			"queued native failure was not diagnosed before a draw lookup");
		for (std::size_t index = 0; index < 8; ++index) {
			Expect(
				PrepareNativeShaderVariantForTesting(failed) == nullptr,
				"failed native compilation published a shader");
		}
		const auto failedSnapshot =
			GetShaderInjectionTargetSnapshot(
				ShaderInjectionTarget::kBsLighting);
		Expect(
			compilationAttempts.load(std::memory_order_relaxed) == 1
				&& failedSnapshot.compileFailures == 1,
			"repeated failed native requests retried or diagnosed more than once");

		const ShaderFamilyDescriptor unsupported{
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kVertex,
			.nativeClassName = "BSImagespaceShaderUnproved"
		};
		for (std::size_t index = 0; index < 8; ++index) {
			Expect(
				PrepareNativeShaderVariantForTesting(unsupported)
					== nullptr,
				"unsupported native descriptor published a shader");
		}
		const auto stats = GetNativeVariantCacheStatsForTesting();
		const auto unsupportedSnapshot =
			GetShaderInjectionTargetSnapshot(
				ShaderInjectionTarget::kImageSpace);
		Expect(
			compilationAttempts.load(std::memory_order_relaxed) == 1
				&& stats.entries == 2
				&& stats.compilation == 1
				&& stats.unsupported == 1
				&& unsupportedSnapshot.variantsUnsupported == 1
				&& unsupportedSnapshot.variantsFailed == 0,
			"unsupported native requests were not retained as one terminal cache outcome");

		InvalidateNativeShaderVariantCompilations();
		for (std::size_t index = 0; index < 4; ++index)
			std::ignore = PrepareNativeShaderVariantForTesting(failed);
		Expect(
			compilationAttempts.load(std::memory_order_relaxed) == 2
				&& GetShaderInjectionTargetSnapshot(
					ShaderInjectionTarget::kBsLighting)
					.compileFailures
					== 2,
			"native invalidation did not permit exactly one fresh failed attempt");
	}

	void CheckRouteEligibilityAndVariantObservations()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create route-validation WARP device");
		if (!device)
			return;

		ShaderReplacementRegistration contribution;
		contribution.targetId = ShaderInjectionTarget::kBsLighting;
		contribution.stages = ShaderStageBit(ShaderStage::kPixel);
		contribution.contributor = "route-validation";
		contribution.defines = {
			{ "ROUTE_VALIDATION", "1" },
			{ "ROUTE_DEBUG", "1" }
		};
		Expect(
			RegisterReplacement(std::move(contribution)),
			"could not register route-validation contribution");
		FreezeAndCompileShaderInjections(device.get());

		const std::array validRoutes{
			ShaderInjectionRouteRequirement{
				.target = ShaderInjectionTarget::kBsLighting,
				.stages = ShaderStageBit(ShaderStage::kPixel),
				.contributor = "route-validation",
				.defines = {
					{ "ROUTE_VALIDATION", "1" },
					{ "ROUTE_DEBUG", "1" }
				}
			}
		};
		std::string error;
		Expect(
			ValidateShaderInjectionRoutes(
				"route validation", validRoutes, error),
			"published route was rejected before any variant was observed");
		auto snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kBsLighting);
		Expect(
			snapshot.requested
				&& snapshot.published
				&& snapshot.variantsObserved == 0
				&& snapshot.variantsPending == 0
				&& snapshot.variantsReady == 0
				&& snapshot.variantsFailed == 0
				&& snapshot.variantsUnsupported == 0,
			"unobserved published route reported fabricated compilation state");

		auto missingContributor = validRoutes;
		missingContributor.front().contributor = "missing-contributor";
		Expect(
			!ValidateShaderInjectionRoutes(
				"route validation", missingContributor, error),
			"route validation accepted a missing contributor");

		auto missingDefine = validRoutes;
		missingDefine.front().defines["ROUTE_DEBUG"] = "0";
		Expect(
			!ValidateShaderInjectionRoutes(
				"route validation", missingDefine, error),
			"route validation accepted a missing exact define value");

		auto missingStage = validRoutes;
		missingStage.front().stages =
			ShaderStageBit(ShaderStage::kVertex);
		Expect(
			!ValidateShaderInjectionRoutes(
				"route validation", missingStage, error),
			"route validation accepted an undelivered shader stage");

		holdCompilationPending = true;
		const ShaderFamilyDescriptor descriptor{
			.target = ShaderInjectionTarget::kBsLighting,
			.stage = ShaderStage::kPixel,
			.descriptor = 0,
			.nativeName = "BSLightingShader"
		};
		Expect(
			PrepareNativeShaderVariantForTesting(descriptor) == nullptr,
			"pending native compilation replaced the stock shader");
		RE::BSGraphics::PixelShader nativePixel{};
		nativePixel.id = descriptor.descriptor;
		const auto pendingBinding =
			ResolveNativeGraphicsShaderBindingForTesting(
				ShaderInjectionTarget::kBsLighting,
				"BSLightingShader",
				0xFFFFFFFFU,
				descriptor.descriptor,
				nullptr,
				&nativePixel);
		snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kBsLighting);
		Expect(
			pendingBinding.pixel == &nativePixel
				&& snapshot.matches == 1
				&& snapshot.substitutions == 0
				&& snapshot.variantsObserved == 1
				&& snapshot.variantsPending == 1
				&& snapshot.variantsReady == 0
				&& snapshot.variantsFailed == 0
				&& ValidateShaderInjectionRoutes(
					"route validation", validRoutes, error),
			"expected pending fallback changed route eligibility or terminal diagnostics");

		Expect(
			static_cast<bool>(pendingCompilation),
			"pending compilation handle was not retained");
		if (pendingCompilation)
			pendingCompilation->Succeed();
		holdCompilationPending = false;
		winrt::com_ptr<ID3D11DeviceChild> replacement;
		replacement.attach(
			PrepareNativeShaderVariantForTesting(descriptor));
		snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kBsLighting);
		Expect(
			replacement
				&& snapshot.variantsObserved == 1
				&& snapshot.variantsPending == 0
				&& snapshot.variantsReady == 1
				&& snapshot.variantsFailed == 0,
			"completed native compilation did not report a truthful ready transition");
		pendingCompilation.reset();
	}

	void CheckUnavailableRoute(
		bool a_coreDisabled,
		bool a_forceOff,
		bool a_missingDevice)
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		if (!a_missingDevice) {
			Expect(
				CreateWarpDevice(device, context),
				"could not create unavailable-route WARP device");
			if (!device)
				return;
		}

		ShaderReplacementRegistration contribution;
		contribution.targetId = ShaderInjectionTarget::kBsLighting;
		contribution.stages = ShaderStageBit(ShaderStage::kPixel);
		contribution.contributor = "unavailable-route";
		contribution.defines = { { "UNAVAILABLE_ROUTE", "1" } };
		Expect(
			RegisterReplacement(std::move(contribution)),
			"could not register unavailable-route contribution");
		if (a_coreDisabled) {
			Expect(
				SetShaderInjectionEnabled(false),
				"could not disable shader injection");
		}
		if (a_forceOff) {
			Expect(
				SetDeveloperShaderForceOffEnabled(true)
					&& SetDeveloperShaderOverride(
						ShaderInjectionTarget::kBsLighting,
						DeveloperShaderOverride::kForceOff),
				"could not force off shader route");
		}
		FreezeAndCompileShaderInjections(device.get());

		const std::array routes{
			ShaderInjectionRouteRequirement{
				.target = ShaderInjectionTarget::kBsLighting,
				.stages = ShaderStageBit(ShaderStage::kPixel),
				.contributor = "unavailable-route",
				.defines = { { "UNAVAILABLE_ROUTE", "1" } }
			}
		};
		std::string error;
		Expect(
			!ValidateShaderInjectionRoutes(
				"unavailable route", routes, error)
				&& !error.empty(),
			"unavailable route passed eligibility validation");
	}

	class ExecutableDispatchFixture
	{
	public:
		using Function = void(STDMETHODCALLTYPE*)(
			ID3D11DeviceContext*, UINT, UINT, UINT);

		explicit ExecutableDispatchFixture(bool a_validTail = true)
		{
			constexpr std::array<std::uint8_t, 10> code{
				0x48, 0x8B, 0x01,
				0x48, 0xFF, 0xA0, 0x48, 0x01, 0x00, 0x00
			};
			memory = VirtualAlloc(
				nullptr,
				code.size(),
				MEM_COMMIT | MEM_RESERVE,
				PAGE_READWRITE);
			if (!memory)
				return;
			std::memcpy(memory, code.data(), code.size());
			if (!a_validTail)
				static_cast<std::uint8_t*>(memory)[3] = 0xCC;
			DWORD oldProtect = 0;
			if (!VirtualProtect(
					memory,
					code.size(),
					PAGE_EXECUTE_READ,
					&oldProtect)) {
				VirtualFree(memory, 0, MEM_RELEASE);
				memory = nullptr;
				return;
			}
			FlushInstructionCache(GetCurrentProcess(), memory, code.size());
		}

		~ExecutableDispatchFixture()
		{
			if (memory)
				VirtualFree(memory, 0, MEM_RELEASE);
		}

		ExecutableDispatchFixture(const ExecutableDispatchFixture&) = delete;
		ExecutableDispatchFixture& operator=(
			const ExecutableDispatchFixture&) = delete;

		explicit operator bool() const noexcept
		{
			return memory != nullptr;
		}

		std::uintptr_t Tail() const noexcept
		{
			return reinterpret_cast<std::uintptr_t>(memory) + 3;
		}

		void Dispatch(
			ID3D11DeviceContext* a_context,
			UINT a_x,
			UINT a_y,
			UINT a_z) const noexcept
		{
			reinterpret_cast<Function>(memory)(a_context, a_x, a_y, a_z);
		}

	private:
		void* memory = nullptr;
	};

	struct ComputeOutput
	{
		winrt::com_ptr<ID3D11Buffer> buffer;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		winrt::com_ptr<ID3D11Buffer> staging;
	};

	winrt::com_ptr<ID3D11Buffer> CreateUintConstantBuffer(
		ID3D11Device* a_device,
		std::uint32_t a_value)
	{
		const std::array<std::uint32_t, 4> data{ a_value, 0, 0, 0 };
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(data);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		D3D11_SUBRESOURCE_DATA initial{ data.data() };
		winrt::com_ptr<ID3D11Buffer> buffer;
		if (FAILED(a_device->CreateBuffer(
				&desc, &initial, buffer.put()))) {
			return {};
		}
		return buffer;
	}

	winrt::com_ptr<ID3D11ShaderResourceView> CreateUintSrv(
		ID3D11Device* a_device,
		std::uint32_t a_value)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 1;
		desc.Height = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA initial{ &a_value, sizeof(a_value) };
		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_device->CreateTexture2D(
				&desc, &initial, texture.put()))) {
			return {};
		}
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (FAILED(a_device->CreateShaderResourceView(
				texture.get(), nullptr, srv.put()))) {
			return {};
		}
		return srv;
	}

	struct NativeComputeInputs
	{
		std::array<winrt::com_ptr<ID3D11Buffer>, 3> buffers;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11Buffer> highBuffer;
		winrt::com_ptr<ID3D11ShaderResourceView> highSrv;
	};

	NativeComputeInputs CreateNativeComputeInputs(ID3D11Device* a_device)
	{
		NativeComputeInputs inputs;
		for (std::size_t index = 0; index < inputs.buffers.size(); ++index) {
			inputs.buffers[index] = CreateUintConstantBuffer(
				a_device, static_cast<std::uint32_t>(index + 5));
		}
		inputs.srv = CreateUintSrv(a_device, 9);
		inputs.highBuffer = CreateUintConstantBuffer(a_device, 88);
		inputs.highSrv = CreateUintSrv(a_device, 44);
		return inputs;
	}

	void BindNativeComputeInputs(
		ID3D11DeviceContext* a_context,
		ID3D11ComputeShader* a_shader,
		const NativeComputeInputs& a_inputs,
		ID3D11UnorderedAccessView* a_uav)
	{
		a_context->CSSetShader(a_shader, nullptr, 0);
		ID3D11Buffer* buffers[]{
			a_inputs.buffers[0].get(),
			a_inputs.buffers[1].get(),
			a_inputs.buffers[2].get()
		};
		a_context->CSSetConstantBuffers(
			cs::render::kSharedDataSlot, 3, buffers);
		ID3D11Buffer* highBuffer = a_inputs.highBuffer.get();
		a_context->CSSetConstantBuffers(8, 1, &highBuffer);
		ID3D11ShaderResourceView* srv = a_inputs.srv.get();
		a_context->CSSetShaderResources(3, 1, &srv);
		ID3D11ShaderResourceView* highSrv = a_inputs.highSrv.get();
		a_context->CSSetShaderResources(4, 1, &highSrv);
		a_context->CSSetUnorderedAccessViews(0, 1, &a_uav, nullptr);
	}

	bool NativeComputeInputsMatch(
		ID3D11DeviceContext* a_context,
		const NativeComputeInputs& a_inputs,
		ID3D11UnorderedAccessView* a_uav)
	{
		ID3D11Buffer* buffers[3]{};
		a_context->CSGetConstantBuffers(
			cs::render::kSharedDataSlot, 3, buffers);
		ID3D11Buffer* highBuffer = nullptr;
		a_context->CSGetConstantBuffers(8, 1, &highBuffer);
		ID3D11ShaderResourceView* srv = nullptr;
		a_context->CSGetShaderResources(3, 1, &srv);
		ID3D11ShaderResourceView* highSrv = nullptr;
		a_context->CSGetShaderResources(4, 1, &highSrv);
		ID3D11UnorderedAccessView* uav = nullptr;
		a_context->CSGetUnorderedAccessViews(0, 1, &uav);
		const bool matches =
			buffers[0] == a_inputs.buffers[0].get()
			&& buffers[1] == a_inputs.buffers[1].get()
			&& buffers[2] == a_inputs.buffers[2].get()
			&& highBuffer == a_inputs.highBuffer.get()
			&& srv == a_inputs.srv.get()
			&& highSrv == a_inputs.highSrv.get()
			&& uav == a_uav;
		for (auto* buffer : buffers) {
			if (buffer)
				buffer->Release();
		}
		if (highBuffer)
			highBuffer->Release();
		if (srv)
			srv->Release();
		if (highSrv)
			highSrv->Release();
		if (uav)
			uav->Release();
		return matches;
	}

	ComputeOutput CreateComputeOutput(ID3D11Device* a_device)
	{
		constexpr std::array<std::uint32_t, 5> zeros{};
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(zeros);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		D3D11_SUBRESOURCE_DATA initial{ zeros.data() };

		ComputeOutput output;
		if (FAILED(a_device->CreateBuffer(
				&desc, &initial, output.buffer.put()))) {
			return {};
		}
		if (FAILED(a_device->CreateUnorderedAccessView(
				output.buffer.get(), nullptr, output.uav.put()))) {
			return {};
		}
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		if (FAILED(a_device->CreateBuffer(
				&desc, nullptr, output.staging.put()))) {
			return {};
		}
		return output;
	}

	winrt::com_ptr<ID3D11ComputeShader> CreateCountingComputeShader(
		ID3D11Device* a_device)
	{
		constexpr std::string_view source =
			"RWStructuredBuffer<uint> Output : register(u0); "
			"[numthreads(1,1,1)] void main() { "
			"InterlockedAdd(Output[0], 1); }";
		winrt::com_ptr<ID3DBlob> bytecode;
		if (FAILED(D3DCompile(
				source.data(),
				source.size(),
				nullptr,
				nullptr,
				nullptr,
				"main",
				"cs_5_0",
				0,
				0,
				bytecode.put(),
				nullptr))
			|| !bytecode) {
			return {};
		}
		winrt::com_ptr<ID3D11ComputeShader> shader;
		if (FAILED(a_device->CreateComputeShader(
				bytecode->GetBufferPointer(),
				bytecode->GetBufferSize(),
				nullptr,
				shader.put()))) {
			return {};
		}
		return shader;
	}

	void ResetComputeOutput(
		ID3D11DeviceContext* a_context,
		const ComputeOutput& a_output)
	{
		constexpr std::array<std::uint32_t, 5> zeros{};
		a_context->UpdateSubresource(
			a_output.buffer.get(), 0, nullptr, zeros.data(), 0, 0);
	}

	std::optional<std::array<std::uint32_t, 5>> ReadComputeOutput(
		ID3D11DeviceContext* a_context,
		const ComputeOutput& a_output)
	{
		ID3D11UnorderedAccessView* nullUav = nullptr;
		a_context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		a_context->CopyResource(
			a_output.staging.get(), a_output.buffer.get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(
				a_output.staging.get(),
				0,
				D3D11_MAP_READ,
				0,
				&mapped))) {
			return std::nullopt;
		}
		std::array<std::uint32_t, 5> values{};
		std::memcpy(values.data(), mapped.pData, sizeof(values));
		a_context->Unmap(a_output.staging.get(), 0);
		return values;
	}

	bool FirstOutputEquals(
		const std::optional<std::array<std::uint32_t, 5>>& a_values,
		std::uint32_t a_expected) noexcept
	{
		return a_values && (*a_values)[0] == a_expected;
	}

	void BindComputeOutput(
		ID3D11DeviceContext* a_context,
		ID3D11ComputeShader* a_shader,
		ID3D11UnorderedAccessView* a_uav)
	{
		a_context->CSSetShader(a_shader, nullptr, 0);
		a_context->CSSetUnorderedAccessViews(0, 1, &a_uav, nullptr);
	}

	RE::BSGraphics::ComputeShader* BindResolvedComputeShader(
		ID3D11DeviceContext* a_context,
		RE::BSGraphics::ComputeShader& a_native)
	{
		auto* selected =
			cs::engine::ResolveNativeComputeShaderBinding(&a_native);
		a_context->CSSetShader(
			reinterpret_cast<ID3D11ComputeShader*>(selected->shader),
			nullptr,
			0);
		return selected;
	}

	void CheckComputeDispatchBridge()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create dispatch bridge WARP device");
		if (!device || !context)
			return;

		ExecutableDispatchFixture invalid(false);
		Expect(
			static_cast<bool>(invalid),
			"could not allocate invalid dispatch fixture");
		if (invalid) {
			std::array<std::uint8_t, 7> before{};
			std::memcpy(
				before.data(),
				reinterpret_cast<const void*>(invalid.Tail()),
				before.size());
			Expect(
				!InstallComputeDispatchBridgeForTesting(
					context.get(), invalid.Tail()),
				"invalid dispatch tail was accepted");
			std::array<std::uint8_t, 7> after{};
			std::memcpy(
				after.data(),
				reinterpret_cast<const void*>(invalid.Tail()),
				after.size());
			Expect(before == after, "invalid dispatch tail was modified");
		}

		ExecutableDispatchFixture bridge;
		Expect(
			static_cast<bool>(bridge),
			"could not allocate valid dispatch fixture");
		if (!bridge)
			return;
		auto& trampoline = REL::GetTrampoline();
		if (trampoline.empty()) {
			try {
				trampoline.create(
					128,
					reinterpret_cast<void*>(bridge.Tail()));
			} catch (...) {
				Expect(false, "could not create dispatch test trampoline");
				return;
			}
		}
		Expect(
			InstallComputeDispatchBridgeForTesting(
				context.get(), bridge.Tail()),
			"valid dispatch bridge was not installed");
		if (!ComputeDispatchBridgeInstalled())
			return;

		auto shader = CreateCountingComputeShader(device.get());
		auto output = CreateComputeOutput(device.get());
		Expect(
			shader && output.buffer && output.uav && output.staging,
			"could not create dispatch execution fixtures");
		if (!shader || !output.uav)
			return;

		const auto beforeOrdinary = GetComputeDispatchBridgeStatus();
		BindComputeOutput(context.get(), shader.get(), output.uav.get());
		context->Dispatch(2, 1, 1);
		Expect(
			FirstOutputEquals(ReadComputeOutput(context.get(), output), 2),
			"ordinary Dispatch was modified by the engine bridge");
		const auto afterOrdinary = GetComputeDispatchBridgeStatus();
		Expect(
			afterOrdinary.bridgeCalls == beforeOrdinary.bridgeCalls,
			"ordinary Dispatch entered the engine bridge");

		constexpr std::array<std::uint32_t, 3> args{ 3, 1, 1 };
		D3D11_BUFFER_DESC indirectDesc{};
		indirectDesc.ByteWidth = sizeof(args);
		indirectDesc.Usage = D3D11_USAGE_DEFAULT;
		indirectDesc.MiscFlags =
			D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		D3D11_SUBRESOURCE_DATA indirectInitial{ args.data() };
		winrt::com_ptr<ID3D11Buffer> indirect;
		Expect(
			SUCCEEDED(device->CreateBuffer(
				&indirectDesc, &indirectInitial, indirect.put())),
			"could not create indirect dispatch arguments");
		ResetComputeOutput(context.get(), output);
		BindComputeOutput(context.get(), shader.get(), output.uav.get());
		context->DispatchIndirect(indirect.get(), 0);
		Expect(
			FirstOutputEquals(ReadComputeOutput(context.get(), output), 3),
			"ordinary DispatchIndirect was modified by the engine bridge");

		ResetComputeOutput(context.get(), output);
		BindComputeOutput(context.get(), shader.get(), output.uav.get());
		const auto beforeBridge = GetComputeDispatchBridgeStatus();
		bridge.Dispatch(context.get(), 4, 1, 1);
		Expect(
			FirstOutputEquals(ReadComputeOutput(context.get(), output), 4),
			"engine bridge did not preserve unmatched compute dispatch");
		const auto afterBridge = GetComputeDispatchBridgeStatus();
		Expect(
			afterBridge.bridgeCalls == beforeBridge.bridgeCalls + 1
				&& afterBridge.shaderRejections
					== beforeBridge.shaderRejections + 1,
			"engine bridge did not record unmatched shader passthrough");

		winrt::com_ptr<ID3D11DeviceContext> deferred;
		Expect(
			SUCCEEDED(device->CreateDeferredContext(0, deferred.put()))
				&& deferred,
			"could not create dispatch context rejection fixture");
		if (deferred) {
			BindComputeOutput(deferred.get(), shader.get(), output.uav.get());
			const auto beforeContext = GetComputeDispatchBridgeStatus();
			bridge.Dispatch(deferred.get(), 1, 1, 1);
			winrt::com_ptr<ID3D11CommandList> commands;
			Expect(
				SUCCEEDED(deferred->FinishCommandList(
					FALSE, commands.put()))
					&& commands,
				"rejected context did not record original dispatch");
			if (commands)
				context->ExecuteCommandList(commands.get(), FALSE);
			const auto afterContext = GetComputeDispatchBridgeStatus();
			Expect(
				afterContext.contextRejections
					== beforeContext.contextRejections + 1,
				"engine bridge did not reject a non-immediate context");
		}

		std::array<std::uint8_t, 7> patch{};
		std::memcpy(
			patch.data(),
			reinterpret_cast<const void*>(bridge.Tail()),
			patch.size());
		auto displaced = patch;
		displaced.back() ^= 0x01;
		Expect(
			REL::WriteSafe(
				bridge.Tail(), displaced.data(), displaced.size()),
			"could not displace dispatch bridge patch");
		Expect(
			!ComputeDispatchBridgeInstalled(),
			"dispatch bridge ignored displaced patch ownership");
		Expect(
			REL::WriteSafe(
				bridge.Tail(), patch.data(), patch.size()),
			"could not restore dispatch bridge patch");
		FlushInstructionCache(
			GetCurrentProcess(),
			reinterpret_cast<const void*>(bridge.Tail()),
			patch.size());
		Expect(
			ComputeDispatchBridgeInstalled(),
			"dispatch bridge ownership did not recover");
	}

	void CheckComputeMissingHookFailsClosed()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create missing-hook WARP device");
		if (!device)
			return;
		Expect(
			SetBaselineShaderOwnership(
				ShaderInjectionTarget::kDfTiledLighting, true),
			"could not enable missing-hook baseline ownership");
		ShaderReplacementRegistration contribution;
		contribution.targetId = ShaderInjectionTarget::kDfTiledLighting;
		contribution.stages = ShaderStageBit(ShaderStage::kCompute);
		contribution.contributor = "compute-missing-hook";
		contribution.defines = { { "COMPUTE_MISSING_HOOK", "1" } };
		Expect(
			RegisterReplacement(std::move(contribution)),
			"could not register missing-hook compute contribution");
		FreezeAndCompileShaderInjections(device.get());
		const auto snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kDfTiledLighting);
		const std::array routes{
			ShaderInjectionRouteRequirement{
				.target =
					ShaderInjectionTarget::kDfTiledLighting,
				.stages = ShaderStageBit(ShaderStage::kCompute),
				.contributor = "compute-missing-hook",
				.defines = { { "COMPUTE_MISSING_HOOK", "1" } }
			}
		};
		std::string error;
		Expect(
			snapshot.requested
				&& !snapshot.published
				&& snapshot.variantsObserved == 0
				&& snapshot.publicationError
					== "RunComputeShader dispatch bridge is unavailable"
				&& !ValidateShaderInjectionRoutes(
					"compute route", routes, error)
				&& error.find("dispatch bridge")
					!= std::string::npos,
			"compute ownership did not fail closed without the engine bridge");
	}

	void CheckComputeBaselineOnly()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create baseline compute WARP device");
		if (!device || !context)
			return;

		ExecutableDispatchFixture bridge;
		Expect(
			static_cast<bool>(bridge),
			"could not allocate baseline compute bridge");
		if (!bridge)
			return;
		auto& trampoline = REL::GetTrampoline();
		if (trampoline.empty()) {
			try {
				trampoline.create(
					128,
					reinterpret_cast<void*>(bridge.Tail()));
			} catch (...) {
				Expect(false, "could not create baseline compute trampoline");
				return;
			}
		}
		Expect(
			InstallComputeDispatchBridgeForTesting(
				context.get(), bridge.Tail()),
			"could not install baseline compute bridge");

		auto stock = CreateCountingComputeShader(device.get());
		auto output = CreateComputeOutput(device.get());
		const auto inputs = CreateNativeComputeInputs(device.get());
		Expect(
			stock && output.uav
				&& std::ranges::all_of(
					inputs.buffers,
					[](const auto& a_buffer) { return !!a_buffer; })
				&& inputs.srv && inputs.highBuffer && inputs.highSrv,
			"could not create baseline compute fixtures");
		if (!stock || !output.uav)
			return;
		ObserveNativeComputeShaderForTesting(
			ShaderInjectionTarget::kDfTiledLighting,
			1,
			"DFTiledLighting",
			stock.get());
		Expect(
			SetBaselineShaderOwnership(
				ShaderInjectionTarget::kDfTiledLighting, true),
			"could not enable baseline compute ownership");
		FreezeAndCompileShaderInjections(device.get());

		sharedDataBindCount = 0;
		const auto before = GetComputeDispatchBridgeStatus();
		BindNativeComputeInputs(
			context.get(), stock.get(), inputs, output.uav.get());
		constexpr std::array<std::byte, 8> nativeComputeBytecode{
			std::byte{ 0x44 },
			std::byte{ 0x58 },
			std::byte{ 0x42 },
			std::byte{ 0x43 },
			std::byte{ 1 },
			std::byte{ 2 },
			std::byte{ 3 },
			std::byte{ 4 }
		};
		std::vector<std::byte> nativeWrapperStorage(
			sizeof(RE::BSGraphics::ComputeShader)
			+ nativeComputeBytecode.size());
		auto& nativeWrapper =
			*reinterpret_cast<RE::BSGraphics::ComputeShader*>(
				nativeWrapperStorage.data());
		nativeWrapper.id = 1;
		nativeWrapper.shader =
			reinterpret_cast<REX::W32::ID3D11ComputeShader*>(
				stock.get());
		nativeWrapper.byteCodeSize =
			static_cast<std::uint32_t>(nativeComputeBytecode.size());
		nativeWrapper.shaderDesc = 0xFEDCBA9876543210;
		nativeWrapper.constantTable[0] = 17;
		std::memcpy(
			nativeWrapperStorage.data()
				+ sizeof(RE::BSGraphics::ComputeShader),
			nativeComputeBytecode.data(),
			nativeComputeBytecode.size());
		auto* selected =
			BindResolvedComputeShader(context.get(), nativeWrapper);
		Expect(
			selected != &nativeWrapper
				&& selected->shader != nativeWrapper.shader
				&& selected->id == nativeWrapper.id
				&& selected->byteCodeSize == nativeWrapper.byteCodeSize
				&& selected->shaderDesc == nativeWrapper.shaderDesc
				&& selected->constantTable[0]
					== nativeWrapper.constantTable[0]
				&& std::memcmp(
					reinterpret_cast<const std::byte*>(selected)
						+ sizeof(RE::BSGraphics::ComputeShader),
					nativeComputeBytecode.data(),
					nativeComputeBytecode.size())
					== 0,
			"compute replacement wrapper did not preserve native metadata");
		bridge.Dispatch(context.get(), 2, 1, 1);
		const auto after = GetComputeDispatchBridgeStatus();
		Expect(
			NativeComputeInputsMatch(
				context.get(), inputs, output.uav.get()),
			"baseline compute dispatch did not restore native bindings");
		Expect(
			ReadComputeOutput(context.get(), output)
				== std::array<std::uint32_t, 5>{ 2, 5, 6, 7, 9 },
			"baseline compute replacement did not execute");
		Expect(
			after.matchingDispatches == before.matchingDispatches + 1
				&& sharedDataBindCount == 0,
			"baseline-only compute activated contributed shared data");
	}

	void CheckComputePhaseAndVariant()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create phased compute WARP device");
		if (!device || !context)
			return;

		ExecutableDispatchFixture bridge;
		Expect(
			static_cast<bool>(bridge),
			"could not allocate phased compute bridge");
		if (!bridge)
			return;
		auto& trampoline = REL::GetTrampoline();
		if (trampoline.empty()) {
			try {
				trampoline.create(
					128,
					reinterpret_cast<void*>(bridge.Tail()));
			} catch (...) {
				Expect(false, "could not create phased compute trampoline");
				return;
			}
		}
		Expect(
			InstallComputeDispatchBridgeForTesting(
				context.get(), bridge.Tail()),
			"could not install phased compute bridge");

		auto stock = CreateCountingComputeShader(device.get());
		auto output = CreateComputeOutput(device.get());
		const auto inputs = CreateNativeComputeInputs(device.get());
		Expect(
			stock && output.uav
				&& std::ranges::all_of(
					inputs.buffers,
					[](const auto& a_buffer) { return !!a_buffer; })
				&& inputs.srv && inputs.highBuffer && inputs.highSrv,
			"could not create phased compute fixtures");
		if (!stock || !output.uav)
			return;
		ObserveNativeComputeShaderForTesting(
			ShaderInjectionTarget::kDfTiledLighting,
			1,
			"DFTiledLighting",
			stock.get());

		ShaderReplacementRegistration contribution;
		contribution.targetId = ShaderInjectionTarget::kDfTiledLighting;
		contribution.stages = ShaderStageBit(ShaderStage::kCompute);
		contribution.contributor = "compute-phase";
		contribution.defines = { { "COMPUTE_PHASE_TEST", "1" } };
		contribution.bind = [](ID3D11DeviceContext*) {
			++computeContributionBindCount;
			activeComputeVariantDefine =
				ActiveShaderInjectionVariantHasDefine(
					ShaderInjectionTarget::kDfTiledLighting,
					"COMPUTE_PHASE_TEST");
		};
		Expect(
			RegisterReplacement(std::move(contribution)),
			"could not register phased compute contribution");
		FreezeAndCompileShaderInjections(device.get());
		publishedComputeBuffers[0] =
			CreateUintConstantBuffer(device.get(), 50);
		publishedComputeBuffers[1] =
			CreateUintConstantBuffer(device.get(), 60);
		Expect(
			publishedComputeBuffers[0] && publishedComputeBuffers[1],
			"could not create published compute buffers");
		const auto pixelBytecode = CompileStrippedShader(
			"float4 main() : SV_Target { return 1.0; }",
			"ps_5_0");
		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		if (pixelBytecode) {
			std::ignore = device->CreatePixelShader(
				pixelBytecode->GetBufferPointer(),
				pixelBytecode->GetBufferSize(),
				nullptr,
				pixelShader.put());
		}
		auto pixelBuffer = CreateUintConstantBuffer(device.get(), 77);
		Expect(
			pixelShader && pixelBuffer,
			"could not create pixel-state preservation fixtures");
		ID3D11Buffer* pixelBufferPointer = pixelBuffer.get();
		context->PSSetShader(pixelShader.get(), nullptr, 0);
		context->PSSetConstantBuffers(5, 1, &pixelBufferPointer);

		sharedDataBindCount = 0;
		computeContributionBindCount = 0;
		deferredLightsActive = false;
		const auto beforeInactive = GetComputeDispatchBridgeStatus();
		BindNativeComputeInputs(
			context.get(), stock.get(), inputs, output.uav.get());
		RE::BSGraphics::ComputeShader nativeWrapper{};
		nativeWrapper.id = 1;
		nativeWrapper.shader =
			reinterpret_cast<REX::W32::ID3D11ComputeShader*>(
				stock.get());
		Expect(
			BindResolvedComputeShader(context.get(), nativeWrapper)
				== &nativeWrapper,
			"inactive compute phase selected a replacement wrapper");
		bridge.Dispatch(context.get(), 1, 1, 1);
		const auto afterInactive = GetComputeDispatchBridgeStatus();
		Expect(
			FirstOutputEquals(ReadComputeOutput(context.get(), output), 1)
				&& afterInactive.phaseRejections
					== beforeInactive.phaseRejections + 1
				&& sharedDataBindCount == 0
				&& computeContributionBindCount == 0,
			"compute contribution ran outside deferred-light phase");

		deferredLightsActive = true;
		activeComputeVariantDefine.reset();
		ResetComputeOutput(context.get(), output);
		BindNativeComputeInputs(
			context.get(), stock.get(), inputs, output.uav.get());
		Expect(
			BindResolvedComputeShader(context.get(), nativeWrapper)
				!= &nativeWrapper,
			"active compute phase did not select a replacement wrapper");
		const auto beforeActive = GetComputeDispatchBridgeStatus();
		bridge.Dispatch(context.get(), 3, 1, 1);
		const auto afterActive = GetComputeDispatchBridgeStatus();
		Expect(
			NativeComputeInputsMatch(
				context.get(), inputs, output.uav.get()),
			"active compute dispatch did not restore b5-b8/t3-t4/u0");
		ID3D11PixelShader* restoredPixelShader = nullptr;
		ID3D11Buffer* restoredPixelBuffer = nullptr;
		context->PSGetShader(&restoredPixelShader, nullptr, nullptr);
		context->PSGetConstantBuffers(5, 1, &restoredPixelBuffer);
		Expect(
			restoredPixelShader == pixelShader.get()
				&& restoredPixelBuffer == pixelBuffer.get(),
			"active compute dispatch disturbed pixel shader state");
		if (restoredPixelShader)
			restoredPixelShader->Release();
		if (restoredPixelBuffer)
			restoredPixelBuffer->Release();
		const auto activeValues = ReadComputeOutput(context.get(), output);
		if (activeValues
			&& *activeValues
				!= std::array<std::uint32_t, 5>{ 3, 50, 60, 7, 9 }) {
			std::cerr << "active compute values:";
			for (const auto value : *activeValues)
				std::cerr << ' ' << value;
			std::cerr << '\n';
		}
		Expect(
			activeValues
				== std::array<std::uint32_t, 5>{ 3, 50, 60, 7, 9 },
			"active compute phase did not execute with b5=50/b6=60/b7=7/t3=9");
		Expect(
			afterActive.matchingDispatches
				== beforeActive.matchingDispatches + 1,
			"active compute phase did not record a matching dispatch");
		Expect(
			sharedDataBindCount == 1,
			"active compute phase did not bind shared data exactly once");
		Expect(
			computeContributionBindCount == 1,
			"active compute phase did not run the contributor exactly once");
		Expect(
			activeComputeVariantDefine == true,
			"active compute variant defines were not exposed to the contributor");
		publishedComputeBuffers = {};
	}
}

int main(int argc, char** argv)
{
	const std::string mode = argc > 1 ? argv[1] : "";
	if (mode.empty() || mode == "--baseline-ownership") {
		CheckPrepassDescriptor();
		CheckPrepassTextureSemantic();
		CheckUnprovenPrepassSplineGrassDescriptors();
		CheckNativeShaderRuntimeLayoutAndMacros();
	}
	if (mode.empty() || mode == "--compute-descriptor")
		CheckComputeDescriptor();
	if (mode.empty() || mode == "--contributor-conflict")
		CheckContributorConflict();
	if (mode == "--claim-ledger")
		CheckClaimLedger();
	if (mode == "--native-observer")
		CheckObservedNativeBytecode();
	if (mode == "--lazy-preparation")
		CheckLazyPreparationDoesNotDeadlock();
	if (mode == "--native-outcome-cache")
		CheckNativeVariantOutcomeCaching();
	if (mode == "--route-validation")
		CheckRouteEligibilityAndVariantObservations();
	if (mode == "--route-core-disabled")
		CheckUnavailableRoute(true, false, false);
	if (mode == "--route-force-off")
		CheckUnavailableRoute(false, true, false);
	if (mode == "--route-missing-device")
		CheckUnavailableRoute(false, false, true);
	if (mode == "--dispatch-bridge")
		CheckComputeDispatchBridge();
	if (mode == "--compute-hooks-missing")
		CheckComputeMissingHookFailsClosed();
	if (mode == "--compute-baseline-only")
		CheckComputeBaselineOnly();
	if (mode == "--compute-phase")
		CheckComputePhaseAndVariant();
	return failures == 0 ? 0 : 1;
}
