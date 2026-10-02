#include "FeatureShaderDeclarations.h"
#include "Render/Engine.h"
#include "Render/NativeShaderFamily.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"
#include "Utils/ShaderSamplerBindings.h"

#include <array>
#include <atomic>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <iostream>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <utility>
#include <winrt/base.h>

namespace
{
	bool drawAnchorInstallFails = false;
	bool deferredLightsActive = true;
	std::uint32_t sharedDataBindCount = 0;
	std::uint32_t computeContributionBindCount = 0;
	std::optional<bool> activeComputeVariantDefine;
	std::array<winrt::com_ptr<ID3D11Buffer>, cs::render::kSubstrateBufferCount> publishedComputeBuffers;
	winrt::com_ptr<ID3D11ShaderResourceView> publishedDepth;
	std::vector<cs::engine::ShaderVariantCompilationRequest> compilationRequests;
	std::uint32_t compilationInvalidations = 0;
	bool compilationPending = false;

	class TestCompilationHandle final :
		public cs::engine::ShaderVariantCompilationHandle
	{
	public:
		TestCompilationHandle(
			winrt::com_ptr<ID3D11DeviceChild> a_shader,
			cs::engine::ShaderVariantCompilationState a_state =
				cs::engine::ShaderVariantCompilationState::kReady,
			std::string a_error = {}) :
			shader(std::move(a_shader)),
			state(a_state),
			error(std::move(a_error))
		{}

		cs::engine::ShaderVariantCompilationState
		GetState() const noexcept override
		{
			return state;
		}

		winrt::com_ptr<ID3D11DeviceChild> Acquire() noexcept override
		{
			return shader;
		}

		std::string GetError() const override
		{
			return error;
		}

	private:
		winrt::com_ptr<ID3D11DeviceChild> shader;
		cs::engine::ShaderVariantCompilationState state;
		std::string error;
	};

	class TestCompilationCache final :
		public cs::engine::ShaderVariantCompilationCache
	{
	public:
		std::shared_ptr<cs::engine::ShaderVariantCompilationHandle> Request(
			cs::engine::ShaderVariantCompilationRequest a_request) override
		{
			using namespace cs::engine;
			compilationRequests.push_back(a_request);
			if (compilationPending)
				return std::make_shared<TestCompilationHandle>(nullptr, ShaderVariantCompilationState::kPending);
			if (!a_request.device || a_request.stage != ShaderStage::kCompute) {
				return std::make_shared<TestCompilationHandle>(
					nullptr,
					ShaderVariantCompilationState::kFailed,
					"unexpected test compilation request");
			}

			constexpr std::string_view source =
				"cbuffer PerFrame : register(b4) { uint FrameValue; };"
				"cbuffer SharedData : register(b5) { uint SharedValue; };"
				"cbuffer FeatureData : register(b6) { uint FeatureValue; };"
				"cbuffer FO4SharedData : register(b7) { uint FO4Value; };"
				"Texture2D<uint> CanonicalDepth : register(t17);"
				"Texture2D<uint> NativeTexture : register(t3);"
				"RWStructuredBuffer<uint> Output : register(u0);"
				"[numthreads(1,1,1)] void main() {"
				"InterlockedAdd(Output[0], 1);"
				"Output[1] = SharedValue + FrameValue;"
				"Output[2] = FeatureValue;"
				"Output[3] = FO4Value + CanonicalDepth.Load(int3(0,0,0));"
				"Output[4] = NativeTexture.Load(int3(0,0,0));"
				"}";
			winrt::com_ptr<ID3DBlob> bytecode;
			winrt::com_ptr<ID3DBlob> errors;
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
					errors.put())) ||
				!bytecode) {
				return std::make_shared<TestCompilationHandle>(
					nullptr,
					ShaderVariantCompilationState::kFailed,
					errors ?
						std::string(
							static_cast<const char*>(
								errors->GetBufferPointer()),
							errors->GetBufferSize()) :
						"test shader compilation failed");
			}

			winrt::com_ptr<ID3D11ComputeShader> shader;
			if (FAILED(a_request.device->CreateComputeShader(
					bytecode->GetBufferPointer(),
					bytecode->GetBufferSize(),
					nullptr,
					shader.put())) ||
				!shader) {
				return std::make_shared<TestCompilationHandle>(
					nullptr,
					ShaderVariantCompilationState::kFailed,
					"test shader creation failed");
			}

			winrt::com_ptr<ID3D11DeviceChild> child;
			child.attach(shader.detach());
			return std::make_shared<TestCompilationHandle>(
				std::move(child));
		}

		void Invalidate() override { ++compilationInvalidations; }
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
		if (!a_context || a_stage != cs::engine::ShaderStage::kCompute)
			return;

		++sharedDataBindCount;
		ID3D11Buffer* buffers[cs::render::kSubstrateBufferCount]{};
		for (std::size_t index = 0; index < publishedComputeBuffers.size(); ++index)
			buffers[index] = publishedComputeBuffers[index].get();
		a_context->CSSetConstantBuffers(
			cs::render::kFrameDataSlot, cs::render::kSubstrateBufferCount, buffers);
		ID3D11ShaderResourceView* depth = publishedDepth.get();
		a_context->CSSetShaderResources(cs::render::kCanonicalDepthSlot, 1, &depth);
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
				   a_context.put())) &&
		       a_device && a_context;
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
			.sourcePath = GetShaderPath("Lighting").wstring(),
			.entryPoint = "main",
			.profile = "ps_5_0",
			.defines = { { "CONFLICT", "family" } },
		};
		struct Definition : ShaderDefineProvider
		{
			bool loaded = true;
			std::string_view GetShaderDefineName() const override { return "CONFLICT"; }
			bool HasShaderDefine(ShaderInjectionTarget) const override { return true; }
			bool IsLoaded() const override { return loaded; }
		} definition;
		const ShaderReplacementRegistration contribution{
			.targetId = ShaderInjectionTarget::kBsLighting,
			.stages = ShaderStageBit(ShaderStage::kPixel),
			.contributor = "test",
			.feature = &definition,
		};
		std::string error;
		Expect(
			!BuildEffectiveShaderCompileRequest(
				*target,
				ShaderStage::kPixel,
				family,
				std::span(&contribution, 1),
				&error) &&
				!error.empty(),
			"conflicting contributor defines were accepted");
		definition.loaded = false;
		const auto unloaded = BuildEffectiveShaderCompileRequest(*target, ShaderStage::kPixel, family, std::span(&contribution, 1));
		Expect(unloaded && !unloaded->defines.contains("FO4CS_SUBSTRATE") && unloaded->defines.at("CONFLICT") == "family",
			"an unloaded feature contributed shader defines");
	}

	void CheckRegistration()
	{
		using namespace cs::engine;
		ShaderReplacementRegistration incompletePair;
		incompletePair.targetId = ShaderInjectionTarget::kDeferredPrepass;
		incompletePair.requiresGraphicsPair = true;
		Expect(!RegisterReplacement(std::move(incompletePair)), "pixel-only paired contribution was accepted");
		ShaderReplacementRegistration completePair;
		completePair.targetId = ShaderInjectionTarget::kDeferredPrepass;
		completePair.stages = ShaderStageBit(ShaderStage::kVertex) | ShaderStageBit(ShaderStage::kPixel);
		completePair.requiresGraphicsPair = true;
		Expect(RegisterReplacement(std::move(completePair)), "paired vertex/pixel contribution was rejected");

		drawAnchorInstallFails = true;
		ShaderReplacementRegistration rejectedAnchor;
		rejectedAnchor.targetId = ShaderInjectionTarget::kBsdfComposite;
		rejectedAnchor.contributor = "ledger-rejected-anchor";
		rejectedAnchor.bind = [](ID3D11DeviceContext*) {};
		Expect(
			!RegisterReplacement(std::move(rejectedAnchor)),
			"registration with a failed draw anchor was accepted");
		drawAnchorInstallFails = false;

		ShaderReplacementRegistration reuseAnchor;
		reuseAnchor.targetId = ShaderInjectionTarget::kBsdfComposite;
		reuseAnchor.contributor = "ledger-anchor-reuse";
		Expect(
			RegisterReplacement(std::move(reuseAnchor)),
			"registration failed after the draw anchor became available");
	}

	class ExecutableDispatchFixture
	{
	public:
		using Function = void(STDMETHODCALLTYPE*)(
			ID3D11DeviceContext*, UINT, UINT, UINT);

		ExecutableDispatchFixture()
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
		if (FAILED(a_device->CreateBuffer(&desc, &initial, buffer.put())))
			return {};
		return buffer;
	}

	winrt::com_ptr<ID3D11ShaderResourceView> CreateUintSrv(
		ID3D11Device* a_device,
		std::uint32_t a_value,
		UINT a_bindFlags = D3D11_BIND_SHADER_RESOURCE)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 1;
		desc.Height = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = a_bindFlags;
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
		std::array<winrt::com_ptr<ID3D11Buffer>, cs::render::kSubstrateBufferCount> buffers;
		winrt::com_ptr<ID3D11ShaderResourceView> depth;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11Buffer> highBuffer;
		winrt::com_ptr<ID3D11ShaderResourceView> highSrv;
	};

	NativeComputeInputs CreateNativeComputeInputs(ID3D11Device* a_device)
	{
		NativeComputeInputs inputs;
		for (std::size_t index = 0; index < inputs.buffers.size(); ++index) {
			inputs.buffers[index] = CreateUintConstantBuffer(
				a_device, static_cast<std::uint32_t>(index + 4));
		}
		inputs.srv = CreateUintSrv(a_device, 9);
		inputs.depth = CreateUintSrv(a_device, 170);
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
		ID3D11Buffer* buffers[cs::render::kSubstrateBufferCount]{};
		for (std::size_t index = 0; index < a_inputs.buffers.size(); ++index)
			buffers[index] = a_inputs.buffers[index].get();
		a_context->CSSetConstantBuffers(
			cs::render::kFrameDataSlot, cs::render::kSubstrateBufferCount, buffers);
		ID3D11ShaderResourceView* depth = a_inputs.depth.get();
		a_context->CSSetShaderResources(cs::render::kCanonicalDepthSlot, 1, &depth);
		ID3D11Buffer* highBuffer = a_inputs.highBuffer.get();
		a_context->CSSetConstantBuffers(8, 1, &highBuffer);
		ID3D11ShaderResourceView* srv = a_inputs.srv.get();
		a_context->CSSetShaderResources(3, 1, &srv);
		ID3D11ShaderResourceView* highSrv = a_inputs.highSrv.get();
		a_context->CSSetShaderResources(4, 1, &highSrv);
		a_context->CSSetShaderResources(8, 1, &highSrv);
		a_context->CSSetUnorderedAccessViews(0, 1, &a_uav, nullptr);
	}

	bool NativeComputeInputsMatch(
		ID3D11DeviceContext* a_context,
		const NativeComputeInputs& a_inputs,
		ID3D11UnorderedAccessView* a_uav)
	{
		ID3D11Buffer* buffers[cs::render::kSubstrateBufferCount]{};
		a_context->CSGetConstantBuffers(
			cs::render::kFrameDataSlot, cs::render::kSubstrateBufferCount, buffers);
		ID3D11ShaderResourceView* depth = nullptr;
		a_context->CSGetShaderResources(cs::render::kCanonicalDepthSlot, 1, &depth);
		ID3D11Buffer* highBuffer = nullptr;
		a_context->CSGetConstantBuffers(8, 1, &highBuffer);
		ID3D11ShaderResourceView* srv = nullptr;
		a_context->CSGetShaderResources(3, 1, &srv);
		ID3D11ShaderResourceView* highSrv = nullptr;
		a_context->CSGetShaderResources(4, 1, &highSrv);
		ID3D11ShaderResourceView* metadata = nullptr;
		a_context->CSGetShaderResources(8, 1, &metadata);
		ID3D11UnorderedAccessView* uav = nullptr;
		a_context->CSGetUnorderedAccessViews(0, 1, &uav);
		const bool matches =
			buffers[0] == a_inputs.buffers[0].get() && buffers[1] == a_inputs.buffers[1].get() && buffers[2] == a_inputs.buffers[2].get() && buffers[3] == a_inputs.buffers[3].get() && depth == a_inputs.depth.get() && highBuffer == a_inputs.highBuffer.get() && srv == a_inputs.srv.get() && highSrv == a_inputs.highSrv.get() && metadata == a_inputs.highSrv.get() && uav == a_uav;
		if (depth)
			depth->Release();
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
		if (metadata)
			metadata->Release();
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
				nullptr)) ||
			!bytecode) {
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

	bool InstallBridge(
		ID3D11DeviceContext* a_context,
		ExecutableDispatchFixture& a_bridge)
	{
		if (!a_bridge)
			return false;
		auto& trampoline = REL::GetTrampoline();
		if (trampoline.empty()) {
			try {
				trampoline.create(
					128,
					reinterpret_cast<void*>(a_bridge.Tail()));
			} catch (...) {
				return false;
			}
		}
		return cs::engine::InstallComputeDispatchBridgeForTesting(
			a_context, a_bridge.Tail());
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
		FreezeAndCompileShaderInjections(device.get());
		const auto snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kDfTiledLighting);
		Expect(
			snapshot.requested && !snapshot.published && !snapshot.publicationError.empty(),
			"compute ownership did not fail closed without the bridge");
	}

	void CheckNativeFamilyOwnership(const std::filesystem::path& a_shaderRoot)
	{
		using namespace cs::engine;
		using Type = RE::BSShaderManager::ShaderEnum;
		struct Family
		{
			ShaderInjectionTarget target;
			Type type;
			const char* name;
		};
		constexpr std::array families{
			Family{ ShaderInjectionTarget::kDeferredPrepass, Type::kDFPrepass, "DFPrepass" },
			Family{ ShaderInjectionTarget::kBsdfLight, Type::kDFPrepass, "DFLight" },
			Family{ ShaderInjectionTarget::kBsdfComposite, Type::kDFComposite, "DFComposite" },
			Family{ ShaderInjectionTarget::kEffect, Type::kEffect, "Effect" },
			Family{ ShaderInjectionTarget::kDistantTree, Type::kDistantTree, "DistantTree" },
			Family{ ShaderInjectionTarget::kBsWater, Type::kWater, "Water" },
			Family{ ShaderInjectionTarget::kImageSpace, Type::kImageSpace, "ISSSLRRaytracing" },
			Family{ ShaderInjectionTarget::kDfTiledLighting, Type::kTotal, "DFTiledLighting" }
		};
		for (const auto& contribution : GetFeatureShaderContributions()) {
			const auto family = std::ranges::find(families, contribution.targetId, &Family::target);
			Expect(family != families.end(), "contributor lacks a native-family fixture");
			if (family == families.end())
				continue;
			const auto target = family->type == Type::kTotal ?
			                        ResolveStandaloneComputeTarget(family->name) :
			                        ResolveGraphicsShaderTarget(family->type, family->name);
			Expect(target == contribution.targetId, "native family routed to the wrong contribution target");
			Expect(IsShaderSourceAvailable(a_shaderRoot, family->name), "contributor's engine-named source is missing");
			for (const auto stage : { ShaderStage::kVertex, ShaderStage::kPixel, ShaderStage::kCompute }) {
				if ((contribution.stages & ShaderStageBit(stage)) == 0)
					continue;
				const auto descriptor = BuildShaderFamilyCompilationDescriptor(
					{ .target = contribution.targetId, .stage = stage, .nativeName = family->name });
				Expect(descriptor && descriptor->sourcePath == GetShaderPath(family->name).wstring(),
					"native family did not compile from its engine name");
				if (descriptor) {
					const auto request = BuildEffectiveShaderCompileRequest(*GetShaderInjectionTarget(contribution.targetId),
						stage, *descriptor, GetFeatureShaderContributions());
					Expect(request.has_value(),
						"native family rejected its feature contributions");
					if (request) {
						Expect(std::ranges::none_of(request->defines, [](const auto& define) {
							return define.first.ends_with("_FULLSCREEN_DEBUG");
						}),
							"production contribution includes fullscreen debug code");
					}
				}
			}
		}
		Expect(!ResolveGraphicsShaderTarget(Type::kDFPrepass, "Unknown"), "ambiguous type 4 did not fail closed");
		Expect(!IsShaderSourceAvailable(a_shaderRoot, "Lighting"), "secondary-view Lighting must remain stock");
		Expect(!ResolveStandaloneComputeTarget("Unknown"), "unknown standalone compute did not fail closed");
		Expect(ResolveStandaloneComputeTarget("IndexBufferOffsetCS") == ShaderInjectionTarget::kImageSpace,
			"unowned standalone compute observation was lost");
		const auto wetness = std::ranges::find_if(GetFeatureShaderContributions(), [](const auto& contribution) {
			return contribution.targetId == ShaderInjectionTarget::kDeferredPrepass && contribution.contributor == "WetnessEffects";
		});
		Expect(wetness != GetFeatureShaderContributions().end() && wetness->requiresGraphicsPair &&
				   wetness->stages == (ShaderStageBit(ShaderStage::kVertex) | ShaderStageBit(ShaderStage::kPixel)),
			"wetness producer lost its required VS/PS pair");

		// Exercise the live provider/options path, including the feature-wide sentinel query.
		ShaderDefineDeclaration wetnessDebug{
			cs::features::wetness::kShaderDefines.name,
			{ ShaderInjectionTarget::kDeferredPrepass, ShaderInjectionTarget::kBsdfLight, ShaderInjectionTarget::kBsdfComposite },
			cs::features::wetness::kShaderDefines.debug
		};
		ShaderDefineDeclaration waterDebug{
			cs::features::water_effects::kShaderDefines.name,
			{ ShaderInjectionTarget::kBsdfLight, ShaderInjectionTarget::kBsdfComposite },
			cs::features::water_effects::kShaderDefines.debug
		};
		auto contributions = DescribeFeatureShaderBindings("WetnessEffects", wetnessDebug);
		contributions.append_range(DescribeFeatureShaderBindings("WaterEffects", waterDebug));
		const auto options = [&] {
			return BuildEffectiveShaderCompileRequest(
				*GetShaderInjectionTarget(ShaderInjectionTarget::kBsdfComposite),
				ShaderStage::kPixel, {}, contributions)
			    ->defines;
		};
		const std::vector expectedTargets{ ShaderInjectionTarget::kBsdfComposite };
		Expect(wetnessDebug.GetShaderDefineOptions().empty(), "unselected feature-wide query enabled debug");
		Expect(wetnessDebug.SetFullscreenDebugSelected(true) == expectedTargets,
			"debug selection invalidated production lighting or prepass");
		Expect(wetnessDebug.GetShaderDefineOptions() == wetnessDebug.GetShaderDefineOptions(ShaderInjectionTarget::kBsdfComposite) &&
				   wetnessDebug.GetShaderDefineOptions(ShaderInjectionTarget::kBsdfLight).empty(),
			"debug options lost kCount semantics or leaked into lighting");
		Expect(options().contains(wetnessDebug.debug) && !options().contains(waterDebug.debug),
			"selected fullscreen owner did not exclusively contribute debug");
		Expect(wetnessDebug.SetFullscreenDebugSelected(true).empty(),
			"same-owner mode selection requested a recompile");
		Expect(wetnessDebug.SetFullscreenDebugSelected(false) == expectedTargets &&
				   waterDebug.SetFullscreenDebugSelected(true) == expectedTargets,
			"owner switch failed to retire both define sets");
		Expect(!options().contains(wetnessDebug.debug) && options().contains(waterDebug.debug),
			"previous fullscreen owner's debug define survived selection change");
		Expect(waterDebug.SetFullscreenDebugSelected(false) == expectedTargets &&
				   !options().contains(waterDebug.debug) && waterDebug.GetShaderDefineOptions().empty(),
			"off/preview selection retained fullscreen debug compilation");
	}

	void CheckPixelBindings()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(CreateWarpDevice(device, context), "could not create pixel binding WARP device");
		if (!device || !context)
			return;
		auto originalBuffer = CreateUintConstantBuffer(device.get(), 1);
		auto injectedBuffer = CreateUintConstantBuffer(device.get(), 2);
		auto originalTexture = CreateUintSrv(device.get(), 1);
		auto injectedTexture = CreateUintSrv(device.get(), 2);
		winrt::com_ptr<ID3D11SamplerState> sampler;
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		Expect(SUCCEEDED(device->CreateSamplerState(&samplerDesc, sampler.put())), "could not create pixel sampler");
		if (!originalBuffer || !injectedBuffer || !originalTexture || !injectedTexture || !sampler)
			return;

		bool samplersOnly = false;
		ShaderReplacementRegistration contribution;
		contribution.targetId = ShaderInjectionTarget::kBsdfLight;
		contribution.contributor = "pixel-bindings";
		contribution.bind = [&](ID3D11DeviceContext* ctx) {
			auto* buffer = injectedBuffer.get();
			auto* texture = injectedTexture.get();
			auto* state = sampler.get();
			if (!samplersOnly) {
				BindInjectionConstantBuffers(ctx, 1, 1, &buffer);
				BindInjectionConstantBuffers(ctx, 10, 1, &buffer);
				BindInjectionShaderResources(ctx, 3, 1, &texture);
				BindInjectionShaderResources(ctx, 24, 1, &texture);
				BindInjectionShaderResources(ctx, 25, 1, &texture);
			}
			BindInjectionSamplers(ctx, 1, 1, &state);
			BindInjectionSamplers(ctx, 1, 1, &state);
		};
		Expect(RegisterReplacement(std::move(contribution)), "could not register pixel bindings");
		FreezeAndCompileShaderInjections(device.get());
		Expect(GetShaderInjectionTargetSnapshot(ShaderInjectionTarget::kBsdfLight).published, "pixel bindings were not published");
		BeginShaderInjectionFrame(10);
		auto* originalCB = originalBuffer.get();
		auto* originalSRV = originalTexture.get();
		context->PSSetConstantBuffers(1, 1, &originalCB);
		context->PSSetShaderResources(3, 1, &originalSRV);
		{
			ScopedPixelShaderInjectionBindings outer;
			DispatchShaderInjections(ShaderInjectionTarget::kBsdfLight, context.get());
			winrt::com_ptr<ID3D11ShaderResourceView> actual;
			context->PSGetShaderResources(25, 1, actual.put());
			Expect(actual == injectedTexture, "high resources were not bound before the draw");
			{
				ScopedPixelShaderInjectionBindings inner;
				ID3D11ShaderResourceView* empty = nullptr;
				BindInjectionShaderResources(context.get(), 3, 1, &empty);
			}
			actual = nullptr;
			context->PSGetShaderResources(3, 1, actual.put());
			Expect(actual == injectedTexture, "nested pixel scope did not restore its parent's low resource");
		}
		winrt::com_ptr<ID3D11Buffer> actualBuffer;
		winrt::com_ptr<ID3D11ShaderResourceView> actualTexture;
		winrt::com_ptr<ID3D11SamplerState> actualSampler;
		context->PSGetConstantBuffers(1, 1, actualBuffer.put());
		context->PSGetShaderResources(3, 1, actualTexture.put());
		context->PSGetSamplers(1, 1, actualSampler.put());
		Expect(actualBuffer == originalBuffer && actualTexture == originalTexture && !actualSampler,
			"pixel scope leaked into engine-owned low slots");
		actualBuffer = nullptr;
		actualTexture = nullptr;
		context->PSGetConstantBuffers(10, 1, actualBuffer.put());
		context->PSGetShaderResources(25, 1, actualTexture.put());
		Expect(actualBuffer == injectedBuffer && actualTexture == injectedTexture,
			"pixel scope unnecessarily restored plugin-owned slots");
		BeginShaderInjectionFrame(11);
		const auto metrics = GetShaderInjectionSummary().draw;
		Expect(metrics.frame == 10 && metrics.scopes == 1 && metrics.captures == 4 && metrics.restores == 4 &&
				   metrics.d3dBinds == 11 && metrics.scopeNanoseconds > 0,
			"draw counters did not count writes, nested restoration, or the completed frame");
		context->ClearState();
		{
			ScopedPixelShaderInjectionBindings scope;
			DispatchShaderInjections(ShaderInjectionTarget::kBsdfLight, context.get());
			actualTexture = nullptr;
			context->PSGetShaderResources(25, 1, actualTexture.put());
			Expect(actualTexture == injectedTexture, "engine ClearState left a stale injection binding cache");
		}
		BeginShaderInjectionFrame(12);
		const auto next = GetShaderInjectionSummary().draw;
		Expect(next.frame == 11 && next.scopes == 1 && next.captures == 3 && next.restores == 3 && next.d3dBinds == 9,
			"per-frame counters accumulated earlier draws");

		winrt::com_ptr<ID3D11Texture2D> output;
		winrt::com_ptr<ID3D11RenderTargetView> target;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		Expect(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, output.put())) &&
				   SUCCEEDED(device->CreateRenderTargetView(output.get(), nullptr, target.put())),
			"could not create pixel output fixture");
		if (!target)
			return;
		auto* nativeTarget = target.get();
		context->OMSetRenderTargets(1, &nativeTarget, nullptr);
		{
			ScopedPixelShaderInjectionBindings scope;
			CaptureShaderInjectionOutputs(context.get());
			context->OMSetRenderTargets(0, nullptr, nullptr);
		}
		winrt::com_ptr<ID3D11RenderTargetView> restoredTarget;
		context->OMGetRenderTargets(1, restoredTarget.put(), nullptr);
		Expect(restoredTarget == target, "pixel output override leaked into the engine draw");

		constexpr std::string_view source =
			"Texture2D<float4> Texture : register(t0);"
			"SamplerState Used : register(s1);"
			"SamplerState Unused : register(s13);"
			"float4 used(float2 uv : TEXCOORD) : SV_Target { return Texture.Sample(Used, uv); }"
			"float4 inert() : SV_Target { return 1; }";
		std::array<std::uint16_t, 2> masks{};
		UINT index = 0;
		for (const auto* entry : { "inert", "used" }) {
			winrt::com_ptr<ID3DBlob> code;
			const auto compiled = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
				entry, "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), nullptr);
			Expect(SUCCEEDED(compiled), "could not compile sampler usage fixture");
			if (FAILED(compiled))
				return;
			const auto mask = cs::util::ReflectShaderSamplers(code->GetBufferPointer(), code->GetBufferSize());
			Expect(mask.has_value(), "could not reflect sampler usage fixture");
			masks[index++] = mask.value_or(UINT16_MAX);
		}
		Expect(masks[0] == 0 && masks[1] == 2, "unused declarations were mistaken for sampler consumers");

		winrt::com_ptr<ID3D11SamplerState> nativeSampler;
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		Expect(SUCCEEDED(device->CreateSamplerState(&samplerDesc, nativeSampler.put())), "could not create native sampler");
		auto* nativeState = nativeSampler.get();
		auto* injectedState = sampler.get();
		samplersOnly = true;
		BeginShaderInjectionFrame(13);
		context->PSSetSamplers(1, 1, &nativeState);
		{
			ScopedPixelShaderInjectionBindings scope;
			DispatchShaderInjections(ShaderInjectionTarget::kBsdfLight, context.get(), masks[0]);
			actualSampler = nullptr;
			context->PSGetSamplers(1, 1, actualSampler.put());
			Expect(actualSampler == nativeSampler, "inert shader overwrote its native sampler");
		}
		context->PSSetSamplers(1, 1, &injectedState);
		{
			ScopedPixelShaderInjectionBindings scope;
			DispatchShaderInjections(ShaderInjectionTarget::kBsdfLight, context.get(), masks[1]);
		}
		BeginShaderInjectionFrame(14);
		const auto inert = GetShaderInjectionSummary().draw;
		Expect(inert.scopes == 2 && inert.captures == 0 && inert.restores == 0 && inert.d3dBinds == 0,
			"inert or already-matching samplers captured or rebound engine state");
		context->PSSetSamplers(1, 1, &nativeState);
		{
			ScopedPixelShaderInjectionBindings outer;
			DispatchShaderInjections(ShaderInjectionTarget::kBsdfLight, context.get(), masks[1]);
			{
				ScopedPixelShaderInjectionBindings inner;
				BindInjectionSamplers(context.get(), 1, 1, &nativeState);
			}
			actualSampler = nullptr;
			context->PSGetSamplers(1, 1, actualSampler.put());
			Expect(actualSampler == sampler, "nested sampler scope did not restore its parent");
		}
		actualSampler = nullptr;
		context->PSGetSamplers(1, 1, actualSampler.put());
		Expect(actualSampler == nativeSampler, "used sampler leaked into the following native draw");
		BeginShaderInjectionFrame(15);
		const auto used = GetShaderInjectionSummary().draw;
		Expect(used.scopes == 1 && used.captures == 2 && used.restores == 2 && used.d3dBinds == 4,
			"sampler override was not captured once per nested scope");
	}

	void CheckFrameBindings()
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(CreateWarpDevice(device, context), "could not create frame binding WARP device");
		if (!context)
			return;
		auto texture = CreateUintSrv(device.get(), 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
		auto buffer = CreateUintConstantBuffer(device.get(), 1);
		auto* view = texture.get();
		auto* cb = buffer.get();
		BeginShaderInjectionFrame(100);
		BindFrameShaderResources(context.get(), ShaderStage::kPixel, 45, 1, &view);
		BindFrameConstantBuffers(context.get(), ShaderStage::kVertex, 4, 1, &cb);
		BindFrameShaderResources(context.get(), ShaderStage::kCompute, 17, 1, &view);
		for (auto stage : { ShaderStage::kPixel, ShaderStage::kVertex, ShaderStage::kCompute })
			VerifyFrameBindings(context.get(), stage, ShaderInjectionTarget::kBsdfLight);
		Expect(GetFrameBindingMetrics().checks == 3 && GetFrameBindingMetrics().lost == 0,
			"intact frame bindings failed verification");
		context->ClearState();
		for (auto stage : { ShaderStage::kPixel, ShaderStage::kVertex, ShaderStage::kCompute }) {
			VerifyFrameBindings(context.get(), stage, ShaderInjectionTarget::kBsdfComposite);
			VerifyFrameBindings(context.get(), stage, ShaderInjectionTarget::kBsdfComposite);
		}
		const auto lost = GetFrameBindingMetrics();
		Expect(lost.checks == 6 && lost.lost == 3 && lost.resources[1].test(45) &&
				   lost.buffers[0].test(4) && lost.resources[2].test(17),
			"lost bindings were not counted once per consumer family and stage");
		BindFrameShaderResources(context.get(), ShaderStage::kPixel, 45, 1, &view);
		VerifyFrameBindings(context.get(), ShaderStage::kPixel, ShaderInjectionTarget::kBsdfComposite);
		Expect(GetFrameBindingMetrics().checks == 7 && GetFrameBindingMetrics().lost == 3,
			"producer rebind did not restart consumer sampling");
		BeginShaderInjectionFrame(101);
		Expect(GetShaderInjectionSummary().draw.frameBindings.lost == 3 &&
				   GetFrameBindingMetrics().lost == 0 && GetFrameBindingMetrics().lostTotal == 3,
			"completed-frame binding evidence was not retained");

		winrt::com_ptr<ID3D11Resource> resource;
		texture->GetResource(resource.put());
		winrt::com_ptr<ID3D11RenderTargetView> target;
		const auto created = device->CreateRenderTargetView(resource.get(), nullptr, target.put());
		Expect(SUCCEEDED(created), "could not create frame binding producer RTV");
		if (FAILED(created))
			return;
		auto* output = target.get();
		context->OMSetRenderTargets(1, &output, nullptr);
		BindFrameShaderResources(context.get(), ShaderStage::kPixel, 25, 1, &view);
		VerifyFrameBindings(context.get(), ShaderStage::kPixel, ShaderInjectionTarget::kBsdfComposite);
		Expect(GetFrameBindingMetrics().lost == 1 && GetFrameBindingMetrics().resources[1].test(25),
			"publication before native G-buffer output retirement was not detected");
		context->OMSetRenderTargets(0, nullptr, nullptr);
		BindFrameShaderResources(context.get(), ShaderStage::kPixel, 25, 1, &view);
		VerifyFrameBindings(context.get(), ShaderStage::kPixel, ShaderInjectionTarget::kBsdfComposite);
		Expect(GetFrameBindingMetrics().checks == 2 && GetFrameBindingMetrics().lost == 1,
			"publication after the native producer boundary did not restore the frame binding");
	}

	void CheckComputePhaseAndStateRestoration(const std::filesystem::path& a_shaderRoot)
	{
		using namespace cs::engine;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Expect(
			CreateWarpDevice(device, context),
			"could not create compute bridge WARP device");
		if (!device || !context)
			return;

		ExecutableDispatchFixture bridge;
		Expect(
			InstallBridge(context.get(), bridge),
			"could not install compute dispatch bridge");
		if (!ComputeDispatchBridgeInstalled())
			return;

		auto stock = CreateCountingComputeShader(device.get());
		auto output = CreateComputeOutput(device.get());
		const auto inputs = CreateNativeComputeInputs(device.get());
		Expect(
			stock && output.uav && output.staging && std::ranges::all_of(inputs.buffers, [](const auto& a_buffer) { return !!a_buffer; }) && inputs.srv && inputs.highBuffer && inputs.highSrv,
			"could not create compute bridge fixtures");
		if (!stock || !output.uav)
			return;

		Expect(SetDeveloperShaderOverride(ShaderInjectionTarget::kDfTiledLighting,
				   DeveloperShaderOverride::kForceOn) &&
				   SetDeveloperShaderSourceRoot(a_shaderRoot.wstring()),
			"could not set shader source root");
		Expect(SetShaderInjectionEnabled(false) &&
				   SetBaselineShaderOwnership(ShaderInjectionTarget::kDfTiledLighting, false),
			"could not start with shader ownership disabled");
		ObserveNativeComputeShaderForTesting(
			*ResolveStandaloneComputeTarget("DFTiledLighting"),
			1,
			"DFTiledLighting",
			stock.get());
		Expect(GetShaderInjectionTargetSnapshot(ShaderInjectionTarget::kDfTiledLighting).observedComputeShaders == 1,
			"compute owner observation was not recorded");

		ShaderReplacementRegistration contribution;
		contribution.targetId = ShaderInjectionTarget::kDfTiledLighting;
		contribution.stages = ShaderStageBit(ShaderStage::kCompute);
		contribution.contributor = "compute-phase";
		static const ShaderDefineDeclaration computeDefinition{ "COMPUTE_PHASE_TEST", { ShaderInjectionTarget::kDfTiledLighting } };
		contribution.feature = &computeDefinition;
		contribution.bind = [](ID3D11DeviceContext* a_context) {
			auto* metadata = publishedDepth.get();
			a_context->CSSetShaderResources(8, 1, &metadata);
			++computeContributionBindCount;
			activeComputeVariantDefine =
				ActiveShaderInjectionVariantHasDefine(
					ShaderInjectionTarget::kDfTiledLighting,
					"COMPUTE_PHASE_TEST");
		};
		Expect(
			RegisterReplacement(std::move(contribution)),
			"could not register compute contribution");
		FreezeAndCompileShaderInjections(device.get());

		for (std::size_t index = 0; index < publishedComputeBuffers.size(); ++index)
			publishedComputeBuffers[index] = CreateUintConstantBuffer(
				device.get(), static_cast<std::uint32_t>((index + 4) * 10));
		publishedDepth = CreateUintSrv(device.get(), 17);
		Expect(
			publishedComputeBuffers[0] && publishedComputeBuffers[1] && publishedComputeBuffers[2] && publishedComputeBuffers[3] && publishedDepth,
			"could not create shared compute buffers");

		// Pixel substrate scopes must restore the shared debug slot, including a null native binding.
		for (auto* original : { inputs.srv.get(), static_cast<ID3D11ShaderResourceView*>(nullptr) }) {
			const auto slot = cs::render::kFullscreenDebugTextureSlot;
			context->PSSetShaderResources(slot, 1, &original);
			cs::render::SubstrateBindingSnapshot outer;
			outer.Save(context.get(), ShaderStage::kPixel);
			auto* debug = publishedDepth.get();
			context->PSSetShaderResources(slot, 1, &debug);
			cs::render::SubstrateBindingSnapshot inner;
			inner.Save(context.get(), ShaderStage::kPixel);
			ID3D11ShaderResourceView* nullView = nullptr;
			context->PSSetShaderResources(slot, 1, &nullView);
			inner.Restore(context.get(), ShaderStage::kPixel);
			winrt::com_ptr<ID3D11ShaderResourceView> restored;
			context->PSGetShaderResources(slot, 1, restored.put());
			Expect(restored.get() == debug, "nested substrate scope lost the active debug texture");
			outer.Restore(context.get(), ShaderStage::kPixel);
			restored = nullptr;
			context->PSGetShaderResources(slot, 1, restored.put());
			Expect(restored.get() == original, "substrate scope leaked t61 into the native pixel state");
		}

		RE::BSGraphics::ComputeShader nativeWrapper{};
		nativeWrapper.id = 1;
		nativeWrapper.shader =
			reinterpret_cast<REX::W32::ID3D11ComputeShader*>(
				stock.get());

		deferredLightsActive = true;
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == &nativeWrapper,
			"boot-disabled ownership selected a replacement");
		Expect(GetShaderInjectionTargetSnapshot(ShaderInjectionTarget::kDfTiledLighting).computeBindCalls == 1,
			"disabled ownership hid the native compute invocation");
		Expect(SetShaderInjectionEnabled(true) &&
				   SetBaselineShaderOwnership(ShaderInjectionTarget::kDfTiledLighting, true),
			"could not enable boot-disabled shader ownership");

		deferredLightsActive = false;
		BindNativeComputeInputs(
			context.get(), stock.get(), inputs, output.uav.get());
		Expect(
			BindResolvedComputeShader(context.get(), nativeWrapper) == &nativeWrapper,
			"inactive phase selected a replacement");
		bridge.Dispatch(context.get(), 1, 1, 1);
		Expect(
			ReadComputeOutput(context.get(), output) == std::array<std::uint32_t, 5>{ 1, 0, 0, 0, 0 } && sharedDataBindCount == 0 && computeContributionBindCount == 0,
			"compute contribution ran outside deferred lighting");

		deferredLightsActive = true;
		activeComputeVariantDefine.reset();
		ResetComputeOutput(context.get(), output);
		auto frameInputs = inputs;
		frameInputs.buffers = publishedComputeBuffers;
		frameInputs.depth = publishedDepth;
		BindNativeComputeInputs(
			context.get(), stock.get(), frameInputs, output.uav.get());
		auto* replacement = BindResolvedComputeShader(context.get(), nativeWrapper);
		Expect(
			replacement != &nativeWrapper,
			"active phase did not select the replacement");
		bridge.Dispatch(context.get(), 3, 1, 1);
		Expect(
			NativeComputeInputsMatch(
				context.get(), frameInputs, output.uav.get()),
			"compute bridge did not preserve frame bindings and restore native t8");
		Expect(
			ReadComputeOutput(context.get(), output) == std::array<std::uint32_t, 5>{ 3, 90, 60, 87, 9 },
			"replacement did not execute with shared and native inputs");
		Expect(
			sharedDataBindCount == 0 && computeContributionBindCount == 1 && activeComputeVariantDefine == true,
			"active compute contribution state was not exposed exactly once");
		Expect(SetBaselineShaderOwnership(ShaderInjectionTarget::kDfTiledLighting, false),
			"live target disable was rejected");
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == &nativeWrapper,
			"disabled target still selected its feature replacement");
		Expect(SetBaselineShaderOwnership(ShaderInjectionTarget::kDfTiledLighting, true),
			"live target enable was rejected");
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == replacement,
			"re-enabled target did not reuse its replacement");
		Expect(SetShaderInjectionEnabled(false), "live master disable was rejected");
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == &nativeWrapper,
			"disabled master still selected a feature replacement");
		Expect(SetShaderInjectionEnabled(true), "live master enable was rejected");
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == replacement,
			"re-enabled master did not reuse its replacement");

		const auto compiled = compilationRequests.size();
		const std::array unrelated{ ShaderInjectionTarget::kBsdfComposite };
		InvalidateNativeShaderVariantCompilations(unrelated);
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == replacement &&
				   compilationRequests.size() == compiled && compilationInvalidations == 0,
			"fullscreen target invalidation disturbed unrelated compute variants");
		const std::array affected{ ShaderInjectionTarget::kDfTiledLighting };
		InvalidateNativeShaderVariantCompilations(affected);
		compilationPending = true;
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == &nativeWrapper &&
				   compilationRequests.size() == compiled + 1,
			"retired target used its old wrapper instead of stock while recompiling");
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) == &nativeWrapper &&
				   compilationRequests.size() == compiled + 1 && compilationInvalidations == 0,
			"pending target re-requested compilation or cleared the define-keyed cache");
		compilationPending = false;
		InvalidateNativeShaderVariantCompilations(affected);
		Expect(BindResolvedComputeShader(context.get(), nativeWrapper) != &nativeWrapper &&
				   compilationRequests.size() == compiled + 2,
			"target invalidation lost native owner identity or prevented lazy recompilation");
		publishedComputeBuffers = {};
		publishedDepth = {};
	}
}

int main(int argc, char** argv)
{
	const std::string mode = argc > 1 ? argv[1] : "";
	if (mode == "--registration")
		CheckRegistration();
	else if (mode == "--contributor-conflict")
		CheckContributorConflict();
	else if (mode == "--compute-hooks-missing")
		CheckComputeMissingHookFailsClosed();
	else if (mode == "--native-families" && argc > 2)
		CheckNativeFamilyOwnership(argv[2]);
	else if (mode == "--compute-phase" && argc > 2)
		CheckComputePhaseAndStateRestoration(argv[2]);
	else if (mode == "--pixel-bindings")
		CheckPixelBindings();
	else if (mode == "--frame-bindings")
		CheckFrameBindings();
	else {
		std::cerr << "Unknown test mode\n";
		return 2;
	}
	return failures == 0 ? 0 : 1;
}
