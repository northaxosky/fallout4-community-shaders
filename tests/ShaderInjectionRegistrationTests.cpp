#include "Render/Engine.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"

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
	std::array<winrt::com_ptr<ID3D11Buffer>, 2> publishedComputeBuffers;

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
			if (!a_request.device || a_request.stage != ShaderStage::kCompute) {
				return std::make_shared<TestCompilationHandle>(
					nullptr,
					ShaderVariantCompilationState::kFailed,
					"unexpected test compilation request");
			}

			constexpr std::string_view source =
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
					errors.put()))
				|| !bytecode) {
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
					shader.put()))
				|| !shader) {
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
		if (!a_context || a_stage != cs::engine::ShaderStage::kCompute)
			return;

		++sharedDataBindCount;
		ID3D11Buffer* buffers[]{
			publishedComputeBuffers[0].get(),
			publishedComputeBuffers[1].get()
		};
		a_context->CSSetConstantBuffers(
			cs::render::kSharedDataSlot, 2, buffers);
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
			a_context.put()))
			&& a_device && a_context;
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
		Expect(
			!BuildEffectiveShaderCompileRequest(
				*target,
				ShaderStage::kPixel,
				family,
				std::span(&contribution, 1),
				&error)
				&& !error.empty(),
			"conflicting contributor defines were accepted");
	}

	void CheckClaimLedger()
	{
		using namespace cs::engine;
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
		first.slotClaims = { textureClaim(25) };
		Expect(RegisterReplacement(std::move(first)), "first claim was rejected");

		ShaderReplacementRegistration duplicate;
		duplicate.targetId = ShaderInjectionTarget::kBsdfComposite;
		duplicate.contributor = "ledger-duplicate";
		duplicate.slotClaims = { textureClaim(25) };
		Expect(
			!RegisterReplacement(std::move(duplicate)),
			"duplicate target/slot claim was accepted");

		ShaderReplacementRegistration otherTarget;
		otherTarget.targetId = ShaderInjectionTarget::kBsdfLight;
		otherTarget.contributor = "ledger-other-target";
		otherTarget.slotClaims = { textureClaim(25) };
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
				"reserved b5/b6 claim was accepted");
		}

		drawAnchorInstallFails = true;
		ShaderReplacementRegistration rejectedAnchor;
		rejectedAnchor.targetId = ShaderInjectionTarget::kBsdfComposite;
		rejectedAnchor.contributor = "ledger-rejected-anchor";
		rejectedAnchor.defines = { { "LEDGER_ANCHOR", "1" } };
		rejectedAnchor.bind = [](ID3D11DeviceContext*) {};
		rejectedAnchor.slotClaims = { textureClaim(31) };
		Expect(
			!RegisterReplacement(std::move(rejectedAnchor)),
			"registration with a failed draw anchor was accepted");
		drawAnchorInstallFails = false;

		ShaderReplacementRegistration reuseAnchor;
		reuseAnchor.targetId = ShaderInjectionTarget::kBsdfComposite;
		reuseAnchor.contributor = "ledger-anchor-reuse";
		reuseAnchor.defines = { { "LEDGER_ANCHOR", "2" } };
		reuseAnchor.slotClaims = { textureClaim(31) };
		Expect(
			RegisterReplacement(std::move(reuseAnchor)),
			"failed registration retained its claims");
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
		Expect(
			SetBaselineShaderOwnership(
				ShaderInjectionTarget::kDfTiledLighting, true),
			"could not request compute ownership");
		FreezeAndCompileShaderInjections(device.get());
		const auto snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kDfTiledLighting);
		Expect(
			snapshot.requested
				&& !snapshot.published
				&& !snapshot.publicationError.empty(),
			"compute ownership did not fail closed without the bridge");
	}

	void CheckComputePhaseAndStateRestoration()
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
			stock && output.uav && output.staging
				&& std::ranges::all_of(
					inputs.buffers,
					[](const auto& a_buffer) { return !!a_buffer; })
				&& inputs.srv && inputs.highBuffer && inputs.highSrv,
			"could not create compute bridge fixtures");
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
			"could not register compute contribution");
		FreezeAndCompileShaderInjections(device.get());

		publishedComputeBuffers[0] =
			CreateUintConstantBuffer(device.get(), 50);
		publishedComputeBuffers[1] =
			CreateUintConstantBuffer(device.get(), 60);
		Expect(
			publishedComputeBuffers[0] && publishedComputeBuffers[1],
			"could not create shared compute buffers");

		RE::BSGraphics::ComputeShader nativeWrapper{};
		nativeWrapper.id = 1;
		nativeWrapper.shader =
			reinterpret_cast<REX::W32::ID3D11ComputeShader*>(
				stock.get());

		deferredLightsActive = false;
		BindNativeComputeInputs(
			context.get(), stock.get(), inputs, output.uav.get());
		Expect(
			BindResolvedComputeShader(context.get(), nativeWrapper)
				== &nativeWrapper,
			"inactive phase selected a replacement");
		bridge.Dispatch(context.get(), 1, 1, 1);
		Expect(
			ReadComputeOutput(context.get(), output)
				== std::array<std::uint32_t, 5>{ 1, 0, 0, 0, 0 }
				&& sharedDataBindCount == 0
				&& computeContributionBindCount == 0,
			"compute contribution ran outside deferred lighting");

		deferredLightsActive = true;
		activeComputeVariantDefine.reset();
		ResetComputeOutput(context.get(), output);
		BindNativeComputeInputs(
			context.get(), stock.get(), inputs, output.uav.get());
		Expect(
			BindResolvedComputeShader(context.get(), nativeWrapper)
				!= &nativeWrapper,
			"active phase did not select the replacement");
		bridge.Dispatch(context.get(), 3, 1, 1);
		Expect(
			NativeComputeInputsMatch(
				context.get(), inputs, output.uav.get()),
			"compute bridge did not restore b5-b8/t3-t4/u0");
		Expect(
			ReadComputeOutput(context.get(), output)
				== std::array<std::uint32_t, 5>{ 3, 50, 60, 7, 9 },
			"replacement did not execute with shared and native inputs");
		Expect(
			sharedDataBindCount == 1
				&& computeContributionBindCount == 1
				&& activeComputeVariantDefine == true,
			"active compute contribution state was not exposed exactly once");
		publishedComputeBuffers = {};
	}
}

int main(int argc, char** argv)
{
	const std::string mode = argc > 1 ? argv[1] : "";
	if (mode == "--claim-ledger")
		CheckClaimLedger();
	else if (mode == "--contributor-conflict")
		CheckContributorConflict();
	else if (mode == "--compute-hooks-missing")
		CheckComputeMissingHookFailsClosed();
	else if (mode == "--compute-phase")
		CheckComputePhaseAndStateRestoration();
	else {
		std::cerr << "Unknown test mode\n";
		return 2;
	}
	return failures == 0 ? 0 : 1;
}
