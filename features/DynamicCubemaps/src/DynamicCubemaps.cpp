#include "DynamicCubemaps.h"

#include <DDSTextureLoader.h>
#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSBuffer.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.dynamiccubemaps");

		constexpr const wchar_t* kUpdatePath =
			L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl";
		constexpr const wchar_t* kInferPath =
			L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl";
		constexpr const wchar_t* kIrradiancePath =
			L"Data\\Shaders\\DynamicCubemaps\\SpecularIrradianceCS.hlsl";
		constexpr const wchar_t* kDefaultCubemapPath =
			L"Data\\Shaders\\DynamicCubemaps\\defaultcubemap.dds";

		std::uint32_t DispatchGroups(std::uint32_t a_extent)
		{
			return (a_extent + 7u) / 8u;
		}

		std::uint32_t ActiveExtent(std::uint32_t a_extent, float a_ratio)
		{
			if (!std::isfinite(a_ratio) || a_ratio <= 0.0f) {
				return 0;
			}
			return std::min(
				a_extent,
				static_cast<std::uint32_t>(
					static_cast<double>(a_extent) * a_ratio));
		}

		bool DescribeTexture(
			ID3D11ShaderResourceView* a_srv,
			D3D11_TEXTURE2D_DESC& a_desc)
		{
			if (!a_srv) {
				return false;
			}
			winrt::com_ptr<ID3D11Resource> resource;
			a_srv->GetResource(resource.put());
			auto texture = resource.try_as<ID3D11Texture2D>();
			if (!texture) {
				return false;
			}
			texture->GetDesc(&a_desc);
			return true;
		}

		bool IsCubeSRV(ID3D11ShaderResourceView* a_srv)
		{
			if (!a_srv) {
				return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
			a_srv->GetDesc(&desc);
			return desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE;
		}

		void UnbindCompute(ID3D11DeviceContext* a_context)
		{
			constexpr std::array<ID3D11ShaderResourceView*, 3> nullSrvs{};
			constexpr std::array<ID3D11UnorderedAccessView*, 3> nullUavs{};
			ID3D11Buffer* nullBuffer = nullptr;
			ID3D11SamplerState* nullSampler = nullptr;
			a_context->CSSetShaderResources(
				0, static_cast<UINT>(nullSrvs.size()), nullSrvs.data());
			a_context->CSSetUnorderedAccessViews(
				0, static_cast<UINT>(nullUavs.size()), nullUavs.data(), nullptr);
			a_context->CSSetConstantBuffers(0, 1, &nullBuffer);
			a_context->CSSetSamplers(0, 1, &nullSampler);
			a_context->CSSetShader(nullptr, nullptr, 0);
		}

		void UpdateBuffer(
			ID3D11DeviceContext* a_context,
			ID3D11Buffer* a_buffer,
			const void* a_data,
			std::size_t a_size)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX::ThrowIfFailed(a_context->Map(
				a_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			std::memcpy(mapped.pData, a_data, a_size);
			a_context->Unmap(a_buffer, 0);
		}

		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(
					DynamicCubemaps::GetSingleton());
			}
		} autoRegister;
	}

	DynamicCubemaps* DynamicCubemaps::GetSingleton()
	{
		static DynamicCubemaps instance;
		return &instance;
	}

	void DynamicCubemaps::Load()
	{
		std::vector<cs::engine::ShaderSlotClaim> slotClaims;
		slotClaims.reserve(kDynamicCubemapPSSlotCount);
		for (std::uint32_t offset = 0;
			 offset < kDynamicCubemapPSSlotCount;
			 ++offset) {
			slotClaims.push_back({
				.stage = cs::engine::ShaderStage::kPixel,
				.resourceType =
					cs::engine::ShaderResourceType::kShaderResource,
				.slot = kDynamicCubemapPSSlot + offset
			});
		}

		if (!cs::engine::RegisterReplacement({
				.targetId =
					cs::engine::ShaderInjectionTarget::kBsdfComposite,
				.contributor = "DynamicCubemaps",
				.defines = {
					{
						cs::engine::shader_injection_defines::
							kDynamicCubemaps,
						"1"
					}
				},
				.isReady = [this] {
					return _registrationsReady.load(
						std::memory_order_acquire);
				},
				.bind = [this](ID3D11DeviceContext* a_context) {
					BindCubemaps(a_context);
				},
				.slotClaims = std::move(slotClaims)
			})) {
			FailLoad(
				"DynamicCubemaps requires the reconstructed BSDFComposite "
				"shader; registering that replacement failed");
			return;
		}

		if (!cs::engine::RegisterPreDeferredComposite(
				[] { DynamicCubemaps::GetSingleton()->SaveBindings(); },
				cs::engine::HookPriority::Early)) {
			FailLoad(
				"DynamicCubemaps could not register its composite binding "
				"save hook");
			return;
		}
		if (!cs::engine::RegisterPreDeferredComposite(
				[] { DynamicCubemaps::GetSingleton()->UpdateCubemap(); })) {
			FailLoad(
				"DynamicCubemaps could not register its pre-composite "
				"capture hook");
			return;
		}
		if (!cs::engine::RegisterPostDeferredComposite(
				[] {
					DynamicCubemaps::GetSingleton()->RestoreBindings();
				},
				cs::engine::HookPriority::Late)) {
			FailLoad(
				"DynamicCubemaps could not register its composite binding "
				"restore hook");
			return;
		}

		_registrationsReady.store(true, std::memory_order_release);
		L->info(
			"Registered pre-composite capture and BSDFComposite "
			"consumption at t16-t17.");
	}

	void DynamicCubemaps::OnDataLoaded()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
		} else {
			L->warn("UI event source unavailable; loading-menu resets are disabled.");
		}
	}

	RE::BSEventNotifyControl DynamicCubemaps::ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (a_event.menuName == RE::LoadingMenu::MENU_NAME &&
			!a_event.opening) {
			_queuedReset.store(true, std::memory_order_release);
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void DynamicCubemaps::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_registrationsReady.load(std::memory_order_acquire) ||
			!a_device) {
			return;
		}

		try {
			const auto compile = [](
				winrt::com_ptr<ID3D11ComputeShader>& a_target,
				const wchar_t* a_path,
				const std::vector<std::pair<const char*, const char*>>&
					a_defines,
				std::string_view a_name) {
				a_target.attach(
					reinterpret_cast<ID3D11ComputeShader*>(
						cs::util::CompileShader(
							a_path, a_defines, "cs_5_0")));
				if (!a_target) {
					throw std::runtime_error(
						"compute shader compilation failed");
				}
				cs::render::annotation::SetName(a_target.get(), a_name);
			};

			compile(
				_updateCS,
				kUpdatePath,
				{},
				"DynamicCubemaps/Update.CS");
			compile(
				_updateReflectionsCS,
				kUpdatePath,
				{ { "REFLECTIONS", "1" } },
				"DynamicCubemaps/UpdateReflections.CS");
			compile(
				_inferCS,
				kInferPath,
				{},
				"DynamicCubemaps/Infer.CS");
			compile(
				_inferReflectionsCS,
				kInferPath,
				{ { "REFLECTIONS", "1" } },
				"DynamicCubemaps/InferReflections.CS");
			compile(
				_irradianceCS,
				kIrradiancePath,
				{},
				"DynamicCubemaps/SpecularIrradiance.CS");

			if (!CreateResources(a_device)) {
				throw std::runtime_error("resource creation failed");
			}
			_resourcesReady.store(true, std::memory_order_release);
			L->info(
				"Resources ready ({}x{}, {} mips, R11G11B10_FLOAT output).",
				kCubemapSize,
				kCubemapSize,
				kMipLevels);
		} catch (const std::exception& e) {
			_resourcesReady.store(false, std::memory_order_release);
			L->error("Initialization failed: {}", e.what());
		} catch (...) {
			_resourcesReady.store(false, std::memory_order_release);
			L->error("Initialization failed.");
		}
	}

	bool DynamicCubemaps::CreateResources(ID3D11Device* a_device)
	{
		const auto createCube = [a_device](
			CubeTexture& a_cube,
			DXGI_FORMAT a_format,
			bool a_generateMips,
			bool a_allMipUavs,
			std::string_view a_name) {
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = kCubemapSize;
			textureDesc.Height = kCubemapSize;
			textureDesc.MipLevels = kMipLevels;
			textureDesc.ArraySize = 6;
			textureDesc.Format = a_format;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags =
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			if (a_generateMips) {
				textureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
				textureDesc.MiscFlags |=
					D3D11_RESOURCE_MISC_GENERATE_MIPS;
			}
			textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
			DX::ThrowIfFailed(a_device->CreateTexture2D(
				&textureDesc, nullptr, a_cube.texture.put()));

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = a_format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = kMipLevels;
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(
				a_cube.texture.get(), &srvDesc, a_cube.srv.put()));

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = a_format;
			uavDesc.ViewDimension =
				D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = 6;
			uavDesc.Texture2DArray.MipSlice = 0;
			DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(
				a_cube.texture.get(), &uavDesc, a_cube.mip0Uav.put()));
			if (a_allMipUavs) {
				for (std::uint32_t mip = 1; mip < kMipLevels; ++mip) {
					uavDesc.Texture2DArray.MipSlice = mip;
					DX::ThrowIfFailed(
						a_device->CreateUnorderedAccessView(
							a_cube.texture.get(),
							&uavDesc,
							a_cube.mipUavs[mip].put()));
				}
			}

			cs::render::annotation::SetName(
				a_cube.texture.get(), std::format("{}.Texture", a_name));
			cs::render::annotation::SetName(
				a_cube.srv.get(), std::format("{}.SRV", a_name));
			cs::render::annotation::SetName(
				a_cube.mip0Uav.get(), std::format("{}.Mip0.UAV", a_name));
			for (std::uint32_t mip = 1; mip < kMipLevels; ++mip) {
				if (a_cube.mipUavs[mip]) {
					cs::render::annotation::SetName(
						a_cube.mipUavs[mip].get(),
						std::format("{}.Mip{}.UAV", a_name, mip));
				}
			}
		};

		const auto createStream = [&](CaptureStream& a_stream,
									 std::string_view a_name) {
			createCube(
				a_stream.color,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				true,
				false,
				std::format("{}/Color", a_name));
			createCube(
				a_stream.raw,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				false,
				false,
				std::format("{}/Raw", a_name));
			createCube(
				a_stream.position,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				false,
				false,
				std::format("{}/Position", a_name));
		};

		createStream(_baseStream, "DynamicCubemaps/BaseCapture");
		createStream(
			_reflectionsStream,
			"DynamicCubemaps/ReflectionsCapture");
		createCube(
			_inferred,
			DXGI_FORMAT_R11G11B10_FLOAT,
			true,
			false,
			"DynamicCubemaps/Inferred");
		createCube(
			_environment,
			DXGI_FORMAT_R11G11B10_FLOAT,
			false,
			true,
			"DynamicCubemaps/Environment");
		createCube(
			_reflections,
			DXGI_FORMAT_R11G11B10_FLOAT,
			false,
			true,
			"DynamicCubemaps/Reflections");

		if (auto* context = cs::engine::GetImmediateContext()) {
			constexpr std::array<float, 4> clear{};
			for (auto* cube : { &_inferred, &_environment, &_reflections }) {
				context->ClearUnorderedAccessViewFloat(
					cube->mip0Uav.get(), clear.data());
				for (std::uint32_t mip = 1; mip < kMipLevels; ++mip) {
					if (cube->mipUavs[mip]) {
						context->ClearUnorderedAccessViewFloat(
							cube->mipUavs[mip].get(),
							clear.data());
					}
				}
			}
		}

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(a_device->CreateSamplerState(
			&samplerDesc, _computeSampler.put()));
		cs::render::annotation::SetName(
			_computeSampler.get(), "DynamicCubemaps/LinearWrap.Sampler");

		D3D11_BUFFER_DESC bufferDesc =
			cs::buffer::ConstantBufferDesc<UpdateCubemapCB>();
		DX::ThrowIfFailed(a_device->CreateBuffer(
			&bufferDesc, nullptr, _updateBuffer.put()));
		cs::render::annotation::SetName(
			_updateBuffer.get(), "DynamicCubemaps/Update.Buffer");
		bufferDesc =
			cs::buffer::ConstantBufferDesc<
				SpecularMapFilterSettingsCB>();
		DX::ThrowIfFailed(a_device->CreateBuffer(
			&bufferDesc, nullptr, _filterBuffer.put()));
		cs::render::annotation::SetName(
			_filterBuffer.get(), "DynamicCubemaps/Filter.Buffer");

		const HRESULT loadResult = DirectX::CreateDDSTextureFromFile(
			a_device,
			kDefaultCubemapPath,
			_defaultCubemapResource.put(),
			_defaultCubemap.put());
		DX::ThrowIfFailed(loadResult);
		cs::render::annotation::SetName(
			_defaultCubemap.get(), "DynamicCubemaps/Default.SRV");
		return true;
	}

	bool DynamicCubemaps::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "shader contribution and hooks did not all register";
			_validationDetail = a_error;
			return false;
		}
		if (!_resourcesReady.load(std::memory_order_acquire)) {
			a_error = "cubemap resources or compute shaders are unavailable";
			_validationDetail = a_error;
			return false;
		}

		const auto snapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfComposite);
		const auto define = snapshot.defines.find(
			cs::engine::shader_injection_defines::kDynamicCubemaps);
		const bool contributed =
			define != snapshot.defines.end() && define->second == "1";
		if (!snapshot.requested ||
			!snapshot.compileComplete ||
			!snapshot.swappable ||
			snapshot.slotCollision ||
			!contributed) {
			a_error =
				"'" + snapshot.name +
				"' cannot deliver dynamic cubemaps (requested=" +
				std::to_string(snapshot.requested) +
				" compile_complete=" +
				std::to_string(snapshot.compileComplete) +
				" swappable=" + std::to_string(snapshot.swappable) +
				" slot_collision=" +
				std::to_string(snapshot.slotCollision) +
				" contributed=" + std::to_string(contributed) + ")";
			_validationDetail = a_error;
			return false;
		}

		_validationDetail.clear();
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	void DynamicCubemaps::SaveBindings()
	{
		auto* context = cs::engine::GetImmediateContext();
		if (!_engineBindings.Save(context, kDynamicCubemapPSSlot) &&
			_engineBindings.IsSaved()) {
			CS_LOG_ONCE(
				L,
				spdlog::level::err,
				"DynamicCubemaps t16-t17 binding scopes overlap.");
		}
	}

	void DynamicCubemaps::RestoreBindings()
	{
		_engineBindings.Restore(cs::engine::GetImmediateContext());
	}

	void DynamicCubemaps::BindCubemaps(ID3D11DeviceContext* a_context)
	{
		if (!a_context) {
			return;
		}
		std::array<ID3D11ShaderResourceView*, 2> views{};
		if (_injectionsOperational.load(std::memory_order_acquire) &&
			_resourcesReady.load(std::memory_order_acquire)) {
			views = { _environment.srv.get(), _reflections.srv.get() };
		}
		a_context->PSSetShaderResources(
			kDynamicCubemapPSSlot,
			static_cast<UINT>(views.size()),
			views.data());
	}

	DynamicCubemaps::CaptureStream& DynamicCubemaps::Stream(
		bool a_reflections)
	{
		return a_reflections ? _reflectionsStream : _baseStream;
	}

	DynamicCubemaps::CubeTexture& DynamicCubemaps::Filtered(
		bool a_reflections)
	{
		return a_reflections ? _reflections : _environment;
	}

	void DynamicCubemaps::ResetCapture()
	{
		_baseStream.reset = true;
		_reflectionsStream.reset = true;
		_nextTask.store(
			NextTask::kCaptureInferAndIrradianceA2,
			std::memory_order_relaxed);
	}

	void DynamicCubemaps::UpdateCubemap()
	{
		if (!_injectionsOperational.load(std::memory_order_acquire) ||
			!_resourcesReady.load(std::memory_order_acquire)) {
			return;
		}

		auto* state = cs::engine::GetGraphicsState();
		if (!state) {
			return;
		}
		if (_lastCallbackFrameValid &&
			state->frameCount == _lastCallbackFrame) {
			_repeatCallbacks.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		_lastCallbackFrame = state->frameCount;
		_lastCallbackFrameValid = true;

		if (_queuedReset.exchange(false, std::memory_order_acq_rel)) {
			ResetCapture();
		}
		if (auto* calendar = RE::Calendar::GetSingleton()) {
			const float currentHoursPassed = calendar->GetHoursPassed();
			const float difference =
				std::abs(currentHoursPassed - _previousHoursPassed);
			_previousHoursPassed = currentHoursPassed;
			if (difference >= 0.01f) {
				ResetCapture();
			}
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* depthSRV = cs::engine::GetSceneDepthSRV();
		auto* colorSRV = cs::engine::GetRenderTargetSRV(
			cs::engine::RenderTarget::kMain);
		const auto& frameBuffer = cs::engine::GetFrameBuffer();
		const bool cameraReady =
			frameBuffer.valid &&
			frameBuffer.frameCount == state->frameCount &&
			cs::engine::HasUsableWorldCamera(frameBuffer.data);
		_cameraReadyLastFrame.store(cameraReady, std::memory_order_relaxed);
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 inverseProjection{};
		DirectX::XMFLOAT4 ndcToViewMul{};
		DirectX::XMFLOAT4 ndcToViewAdd{};
		const bool projectionReady =
			cs::engine::TryGetWorldSceneProjection(
				projection,
				inverseProjection,
				ndcToViewMul,
				ndcToViewAdd);
		if (!context || !depthSRV || !colorSRV || !cameraReady ||
			!projectionReady) {
			return;
		}

		D3D11_TEXTURE2D_DESC sourceDesc{};
		if (!DescribeTexture(colorSRV, sourceDesc)) {
			return;
		}
		_captureSourceWidth.store(
			sourceDesc.Width, std::memory_order_relaxed);
		_captureSourceHeight.store(
			sourceDesc.Height, std::memory_order_relaxed);
		_captureSourceFormat.store(
			static_cast<std::uint32_t>(sourceDesc.Format),
			std::memory_order_relaxed);
		if (!_captureSourceLogged) {
			_captureSourceLogged = true;
			L->info(
				"Pre-composite capture source RT3 kMain: {}x{}, format {}. "
				"Verify this is the scene before composite in an in-game capture.",
				sourceDesc.Width,
				sourceDesc.Height,
				static_cast<std::uint32_t>(sourceDesc.Format));
		}

		auto* targetManager = cs::engine::GetRenderTargetManager();
		if (!targetManager) {
			return;
		}
		const std::uint32_t activeWidth = ActiveExtent(
			sourceDesc.Width, targetManager->GetDynamicWidthRatio());
		const std::uint32_t activeHeight = ActiveExtent(
			sourceDesc.Height, targetManager->GetDynamicHeightRatio());
		if (activeWidth == 0 || activeHeight == 0) {
			return;
		}

		std::array<ID3D11ShaderResourceView*, 2> nullPsViews{};
		context->PSSetShaderResources(
			kDynamicCubemapPSSlot,
			static_cast<UINT>(nullPsViews.size()),
			nullPsViews.data());

		cs::engine::OMScope omScope(context);
		cs::ComputeScope computeScope(context);

		switch (_nextTask.load(std::memory_order_relaxed)) {
		case NextTask::kCaptureInferAndIrradianceA:
			UpdateCubemapCapture(false);
			Inference(false);
			Irradiance(false, 1, 2, true);
			_nextTask.store(
				NextTask::kIrradianceBA, std::memory_order_relaxed);
			break;
		case NextTask::kIrradianceBA:
			Irradiance(false, 2, kMipLevels - 1, false);
			_nextTask.store(
				NextTask::kIrradianceBB, std::memory_order_relaxed);
			break;
		case NextTask::kIrradianceBB:
			Irradiance(false, kMipLevels - 1, kMipLevels, false);
			_nextTask.store(
				NextTask::kCaptureInferAndIrradianceA2,
				std::memory_order_relaxed);
			break;
		case NextTask::kCaptureInferAndIrradianceA2:
			UpdateCubemapCapture(true);
			Inference(true);
			Irradiance(true, 1, 2, true);
			_nextTask.store(
				NextTask::kIrradianceBA2, std::memory_order_relaxed);
			break;
		case NextTask::kIrradianceBA2:
			Irradiance(true, 2, kMipLevels - 1, false);
			_nextTask.store(
				NextTask::kIrradianceBB2, std::memory_order_relaxed);
			break;
		case NextTask::kIrradianceBB2:
			Irradiance(true, kMipLevels - 1, kMipLevels, false);
			_nextTask.store(
				NextTask::kCaptureInferAndIrradianceA2,
				std::memory_order_relaxed);
			break;
		}
		_dispatchCount.fetch_add(1, std::memory_order_relaxed);
	}

	void DynamicCubemaps::UpdateCubemapCapture(bool a_reflections)
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		auto* targetManager = cs::engine::GetRenderTargetManager();
		auto* depthSRV = cs::engine::GetSceneDepthSRV();
		auto* colorSRV = cs::engine::GetRenderTargetSRV(
			cs::engine::RenderTarget::kMain);
		const auto& frameBuffer = cs::engine::GetFrameBuffer();
		if (!context || !state || !targetManager || !depthSRV || !colorSRV ||
			!frameBuffer.valid ||
			frameBuffer.frameCount != state->frameCount) {
			return;
		}

		D3D11_TEXTURE2D_DESC sourceDesc{};
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 inverseProjection{};
		DirectX::XMFLOAT4 ndcToViewMul{};
		DirectX::XMFLOAT4 ndcToViewAdd{};
		if (!DescribeTexture(colorSRV, sourceDesc) ||
			!cs::engine::TryGetWorldSceneProjection(
				projection,
				inverseProjection,
				ndcToViewMul,
				ndcToViewAdd)) {
			return;
		}
		const std::uint32_t activeWidth = ActiveExtent(
			sourceDesc.Width, targetManager->GetDynamicWidthRatio());
		const std::uint32_t activeHeight = ActiveExtent(
			sourceDesc.Height, targetManager->GetDynamicHeightRatio());
		if (activeWidth == 0 || activeHeight == 0) {
			return;
		}

		auto& stream = Stream(a_reflections);
		std::array<ID3D11UnorderedAccessView*, 3> uavs{
			stream.color.mip0Uav.get(),
			stream.raw.mip0Uav.get(),
			stream.position.mip0Uav.get()
		};
		if (stream.reset) {
			constexpr std::array<float, 4> clear{};
			for (auto* uav : uavs) {
				context->ClearUnorderedAccessViewFloat(
					uav, clear.data());
			}
			stream.reset = false;
		}

		UpdateCubemapCB constants{};
		for (std::size_t row = 0; row < 3; ++row) {
			const auto& source = frameBuffer.data.ViewToWorld[row];
			constants.ViewToWorld[row] =
				{ source.x, source.y, source.z, 0.0f };
		}
		const auto cameraOrigin =
			cs::engine::CameraWorldOrigin(frameBuffer.data);
		constants.CameraOrigin =
			{ cameraOrigin.x, cameraOrigin.y, cameraOrigin.z, 0.0f };
		constants.CameraPreviousOrigin = {
			stream.previousCameraOrigin.x,
			stream.previousCameraOrigin.y,
			stream.previousCameraOrigin.z,
			0.0f
		};
		constants.NDCToViewMul = ndcToViewMul;
		constants.NDCToViewAdd = ndcToViewAdd;
		constants.InvProj = inverseProjection;
		constants.ActiveRatioAndExtent = {
			static_cast<float>(activeWidth) /
				static_cast<float>(sourceDesc.Width),
			static_cast<float>(activeHeight) /
				static_cast<float>(sourceDesc.Height),
			static_cast<float>(activeWidth),
			static_cast<float>(activeHeight)
		};
		UpdateBuffer(
			context,
			_updateBuffer.get(),
			&constants,
			sizeof(constants));

		std::array<ID3D11ShaderResourceView*, 2> srvs{
			depthSRV,
			colorSRV
		};
		context->CSSetShaderResources(
			0, static_cast<UINT>(srvs.size()), srvs.data());
		context->CSSetUnorderedAccessViews(
			0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
		ID3D11Buffer* buffer = _updateBuffer.get();
		context->CSSetConstantBuffers(0, 1, &buffer);
		ID3D11SamplerState* sampler = _computeSampler.get();
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(
			a_reflections ? _updateReflectionsCS.get() : _updateCS.get(),
			nullptr,
			0);
		context->Dispatch(
			DispatchGroups(kCubemapSize),
			DispatchGroups(kCubemapSize),
			6);
		UnbindCompute(context);

		stream.previousCameraOrigin =
			{ cameraOrigin.x, cameraOrigin.y, cameraOrigin.z };
	}

	void DynamicCubemaps::Inference(bool a_reflections)
	{
		auto* context = cs::engine::GetImmediateContext();
		if (!context) {
			return;
		}
		auto& stream = Stream(a_reflections);
		context->GenerateMips(stream.color.srv.get());

		ID3D11ShaderResourceView* engineReflection = nullptr;
		if (auto* rendererData = RE::BSGraphics::GetRendererData()) {
			auto* candidate =
				reinterpret_cast<ID3D11ShaderResourceView*>(
					rendererData->cubeMapRenderTargets[0].srView);
			if (IsCubeSRV(candidate)) {
				engineReflection = candidate;
			}
		}
		_usedEngineReflectionFallback.store(
			engineReflection != nullptr, std::memory_order_relaxed);
		if (!engineReflection) {
			engineReflection = _defaultCubemap.get();
		}

		std::array<ID3D11ShaderResourceView*, 3> srvs{
			stream.color.srv.get(),
			engineReflection,
			_defaultCubemap.get()
		};
		ID3D11UnorderedAccessView* uav = _inferred.mip0Uav.get();
		context->CSSetShaderResources(
			0, static_cast<UINT>(srvs.size()), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		ID3D11SamplerState* sampler = _computeSampler.get();
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(
			a_reflections ? _inferReflectionsCS.get() : _inferCS.get(),
			nullptr,
			0);
		context->Dispatch(
			DispatchGroups(kCubemapSize),
			DispatchGroups(kCubemapSize),
			6);
		UnbindCompute(context);
	}

	void DynamicCubemaps::Irradiance(
		bool a_reflections,
		std::uint32_t a_startLevel,
		std::uint32_t a_endLevel,
		bool a_doSetup)
	{
		auto* context = cs::engine::GetImmediateContext();
		if (!context) {
			return;
		}
		auto& filtered = Filtered(a_reflections);
		if (a_doSetup) {
			for (std::uint32_t face = 0; face < 6; ++face) {
				const std::uint32_t subresource =
					D3D11CalcSubresource(0, face, kMipLevels);
				context->CopySubresourceRegion(
					filtered.texture.get(),
					subresource,
					0,
					0,
					0,
					_inferred.texture.get(),
					subresource,
					nullptr);
			}
			context->GenerateMips(_inferred.srv.get());
		}

		ID3D11ShaderResourceView* source = _inferred.srv.get();
		context->CSSetShaderResources(0, 1, &source);
		ID3D11SamplerState* sampler = _computeSampler.get();
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(_irradianceCS.get(), nullptr, 0);
		ID3D11Buffer* buffer = _filterBuffer.get();
		context->CSSetConstantBuffers(0, 1, &buffer);

		constexpr float roughnessStep =
			1.0f / static_cast<float>(kMipLevels - 1);
		for (std::uint32_t level = a_startLevel;
			 level < a_endLevel;
			 ++level) {
			SpecularMapFilterSettingsCB constants{};
			constants.roughness =
				static_cast<float>(level) * roughnessStep;
			UpdateBuffer(
				context,
				_filterBuffer.get(),
				&constants,
				sizeof(constants));
			ID3D11UnorderedAccessView* uav =
				filtered.mipUavs[level].get();
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			const std::uint32_t size =
				std::max(1u, kCubemapSize >> level);
			context->Dispatch(
				DispatchGroups(size), DispatchGroups(size), 6);
			ID3D11UnorderedAccessView* nullUav = nullptr;
			context->CSSetUnorderedAccessViews(
				0, 1, &nullUav, nullptr);
		}
		UnbindCompute(context);
	}

	void DynamicCubemaps::CollectTelemetry(
		cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field(
				"operational",
				_injectionsOperational.load(std::memory_order_relaxed))
			.Field(
				"resources_ready",
				_resourcesReady.load(std::memory_order_relaxed))
			.Field(
				"camera_ready",
				_cameraReadyLastFrame.load(std::memory_order_relaxed))
			.Field("capture_source", "RT3 kMain pre-composite")
			.Field(
				"capture_width",
				static_cast<std::int64_t>(
					_captureSourceWidth.load(std::memory_order_relaxed)))
			.Field(
				"capture_height",
				static_cast<std::int64_t>(
					_captureSourceHeight.load(std::memory_order_relaxed)))
			.Field(
				"capture_format",
				static_cast<std::int64_t>(
					_captureSourceFormat.load(std::memory_order_relaxed)))
			.Field(
				"engine_reflection_fallback",
				_usedEngineReflectionFallback.load(
					std::memory_order_relaxed))
			.Field(
				"next_task",
				static_cast<std::int64_t>(
					_nextTask.load(std::memory_order_relaxed)))
			.Field(
				"pipeline_advances",
				static_cast<std::int64_t>(
					_dispatchCount.load(std::memory_order_relaxed)))
			.Field(
				"repeat_callbacks",
				static_cast<std::int64_t>(
					_repeatCallbacks.load(std::memory_order_relaxed)));
	}
}
