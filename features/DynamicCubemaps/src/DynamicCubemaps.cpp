#include "DynamicCubemaps.h"

#include <DDSTextureLoader.h>
#include <d3d11.h>
#include <imgui.h>

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
#include "FeatureBuffer.h"
#include "Menu/Menu.h"
#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
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
		constexpr const wchar_t* kBc6hPath =
			L"Data\\Shaders\\DynamicCubemaps\\BC6HEncodeCS.hlsl";
		constexpr const wchar_t* kPreviewPath =
			L"Data\\Shaders\\DynamicCubemaps\\CubemapPreviewCS.hlsl";
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

		std::string_view DebugVisualizationName(
			DynamicCubemaps::DebugVisualization a_visualization)
		{
			switch (a_visualization) {
			case DynamicCubemaps::DebugVisualization::kCaptureInput:
				return "capture_input";
			case DynamicCubemaps::DebugVisualization::kFilteredReflections:
				return "filtered_reflections";
			case DynamicCubemaps::DebugVisualization::kReflectionContribution:
				return "reflection_contribution";
			default:
				return "off";
			}
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			DynamicCubemaps::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}
			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}
			switch (feature_config::ReadBool(
				*settingsTable, "enabled", a_candidate.enabled)) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = "settings.enabled: expected boolean";
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = "settings.enabled: invalid value";
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = "settings.enabled: value is out of range";
				break;
			}
			return false;
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

	std::span<const FeatureDebugView>
		DynamicCubemaps::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "capture_input",
				.label = "Capture input",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const DynamicCubemaps&>(a_feature)
						.GetCubemapDebugTexture();
				}
			},
			FeatureDebugView{
				.id = "filtered_reflections",
				.label = "Filtered reflections",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const DynamicCubemaps&>(a_feature)
						.GetCubemapDebugTexture();
				}
			},
			FeatureDebugView{
				.id = "reflection_contribution",
				.label = "Dynamic reflection contribution",
				.kind = FeatureDebugViewKind::kFullscreen
			}
		};
		return views;
	}

	void DynamicCubemaps::SetDebugView(std::string_view a_view) noexcept
	{
		DebugVisualization visualization = DebugVisualization::kOff;
		if (a_view == "capture_input") {
			visualization = DebugVisualization::kCaptureInput;
		} else if (a_view == "filtered_reflections") {
			visualization = DebugVisualization::kFilteredReflections;
		} else if (a_view == "reflection_contribution") {
			visualization = DebugVisualization::kReflectionContribution;
		}
		const auto previous = _debugVisualization.exchange(
			visualization, std::memory_order_acq_rel);
		if (previous != visualization) {
			_previewPopulated.store(false, std::memory_order_release);
		}
	}

	FeatureDebugTexture DynamicCubemaps::GetCubemapDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Cubemap preview not populated."
		};
		const auto visualization =
			_debugVisualization.load(std::memory_order_acquire);
		if ((visualization != DebugVisualization::kCaptureInput &&
			 visualization != DebugVisualization::kFilteredReflections) ||
			!_resourcesReady.load(std::memory_order_acquire) ||
			!_previewPopulated.load(std::memory_order_acquire) ||
			!_previewSRV) {
			return texture;
		}
		texture.texture = _previewSRV.get();
		texture.width = kPreviewWidth;
		texture.height = kPreviewHeight;
		texture.caption = std::format(
			"{} mip 0, equirectangular +X center/+Z up; screen-right is -Y; Reinhard display; polar stretching is expected",
			visualization == DebugVisualization::kCaptureInput ?
				"Distance-validated inference input" :
				"Filtered reflections");
		return texture;
	}

	bool DynamicCubemaps::Configure(
		const toml::table& a_config,
		std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}
		_settings = candidate;
		return true;
	}

	void DynamicCubemaps::PublishSettings() noexcept
	{
		const bool wasEnabled = _enabled.exchange(
			_settings.enabled, std::memory_order_acq_rel);
		if (!wasEnabled && _settings.enabled) {
			_queuedReset.store(true, std::memory_order_release);
		}
	}

	void DynamicCubemaps::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		if (const auto result = feature_config::UpdateFeatureSettings(
				GetConfigKey(), settings);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void DynamicCubemaps::Load()
	{
		PublishSettings();
		const auto registerConsumer = [this](
			cs::engine::ShaderInjectionTarget a_target,
			bool a_fullscreenDebug) {
			cs::engine::ShaderInjectionDefines defines{
				{
					cs::engine::shader_injection_defines::kDynamicCubemaps,
					"1"
				}
			};
			if (a_fullscreenDebug) {
				defines.emplace(
					cs::engine::shader_injection_defines::
						kDynamicCubemapsFullscreenDebug,
					"1");
			}
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
			return cs::engine::RegisterReplacement({
				.targetId = a_target,
				.contributor = "DynamicCubemaps",
				.defines = std::move(defines),
				.isReady = [this] {
					return _registrationsReady.load(
						std::memory_order_acquire);
				},
				.bind = [this](ID3D11DeviceContext* a_context) {
					BindCubemaps(a_context);
				},
				.slotClaims = std::move(slotClaims)
			});
		};

		if (!registerConsumer(
				cs::engine::ShaderInjectionTarget::kBsdfComposite,
				true)) {
			FailLoad(
				"DynamicCubemaps requires the reconstructed BSDFComposite "
				"shader; registering that replacement failed");
			return;
		}
		if (!registerConsumer(
				cs::engine::ShaderInjectionTarget::kBsLighting,
				false)) {
			FailLoad(
				"DynamicCubemaps requires the reconstructed BSLighting "
				"shader; registering that replacement failed");
			return;
		}
		if (!registerConsumer(
				cs::engine::ShaderInjectionTarget::kBsWater,
				false)) {
			FailLoad(
				"DynamicCubemaps requires the reconstructed BSWater "
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
					DynamicCubemaps::GetSingleton()->
						RestoreBindings();
				},
				cs::engine::HookPriority::Late)) {
			FailLoad(
				"DynamicCubemaps could not register its composite binding "
				"restore hook");
			return;
		}

		_registrationsReady.store(true, std::memory_order_release);
		L->info(
			"Registered pre-composite capture and BSDFComposite, "
			"BSLighting, and BSWater consumption at t16-t17.");
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
			compile(
				_bc6hEncodeCS,
				kBc6hPath,
				{},
				"DynamicCubemaps/BC6HEncode.CS");
			compile(
				_previewCS,
				kPreviewPath,
				{},
				"DynamicCubemaps/CubemapPreview.CS");

			if (!CreateResources(a_device)) {
				throw std::runtime_error("resource creation failed");
			}
			_resourcesReady.store(true, std::memory_order_release);
			L->info(
				"Resources ready ({}x{}, {} source mips, {} BC6H mips).",
				kCubemapSize,
				kCubemapSize,
				kMipLevels,
				kBc6hMipLevels);
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

		const auto createArraySRV = [a_device](
			CubeTexture& a_cube,
			winrt::com_ptr<ID3D11ShaderResourceView>& a_srv,
			std::string_view a_name) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.MipLevels = kBc6hMipLevels;
			srvDesc.Texture2DArray.FirstArraySlice = 0;
			srvDesc.Texture2DArray.ArraySize = 6;
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(
				a_cube.texture.get(), &srvDesc, a_srv.put()));
			cs::render::annotation::SetName(a_srv.get(), a_name);
		};
		createArraySRV(
			_environment,
			_environmentArraySRV,
			"DynamicCubemaps/Environment.ArraySRV");
		createArraySRV(
			_reflections,
			_reflectionsArraySRV,
			"DynamicCubemaps/Reflections.ArraySRV");

		D3D11_TEXTURE2D_DESC scratchDesc{};
		scratchDesc.Width = kCubemapSize / 4;
		scratchDesc.Height = kCubemapSize / 4;
		scratchDesc.MipLevels = kBc6hMipLevels;
		scratchDesc.ArraySize = 6;
		scratchDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
		scratchDesc.SampleDesc.Count = 1;
		scratchDesc.Usage = D3D11_USAGE_DEFAULT;
		scratchDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		DX::ThrowIfFailed(a_device->CreateTexture2D(
			&scratchDesc, nullptr, _bc6hScratchTexture.put()));
		cs::render::annotation::SetName(
			_bc6hScratchTexture.get(),
			"DynamicCubemaps/BC6H.Scratch.Texture");
		D3D11_UNORDERED_ACCESS_VIEW_DESC scratchUavDesc{};
		scratchUavDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
		scratchUavDesc.ViewDimension =
			D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		scratchUavDesc.Texture2DArray.FirstArraySlice = 0;
		scratchUavDesc.Texture2DArray.ArraySize = 6;
		for (std::uint32_t mip = 0; mip < kBc6hMipLevels; ++mip) {
			scratchUavDesc.Texture2DArray.MipSlice = mip;
			DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(
				_bc6hScratchTexture.get(),
				&scratchUavDesc,
				_bc6hScratchUAVs[mip].put()));
			cs::render::annotation::SetName(
				_bc6hScratchUAVs[mip].get(),
				std::format("DynamicCubemaps/BC6H.Scratch.Mip{}.UAV", mip));
		}

		const auto createCompressedCube = [a_device](
			CompressedCube& a_cube,
			std::string_view a_name) {
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = kCubemapSize;
			textureDesc.Height = kCubemapSize;
			textureDesc.MipLevels = kBc6hMipLevels;
			textureDesc.ArraySize = 6;
			textureDesc.Format = DXGI_FORMAT_BC6H_UF16;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
			DX::ThrowIfFailed(a_device->CreateTexture2D(
				&textureDesc, nullptr, a_cube.texture.put()));
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = textureDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = kBc6hMipLevels;
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(
				a_cube.texture.get(), &srvDesc, a_cube.srv.put()));
			cs::render::annotation::SetName(
				a_cube.texture.get(), std::format("{}.Texture", a_name));
			cs::render::annotation::SetName(
				a_cube.srv.get(), std::format("{}.SRV", a_name));
		};
		createCompressedCube(
			_environmentBC6H,
			"DynamicCubemaps/Environment.BC6H");
		createCompressedCube(
			_reflectionsBC6H,
			"DynamicCubemaps/Reflections.BC6H");

		D3D11_TEXTURE2D_DESC previewDesc{};
		previewDesc.Width = kPreviewWidth;
		previewDesc.Height = kPreviewHeight;
		previewDesc.MipLevels = 1;
		previewDesc.ArraySize = 1;
		previewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		previewDesc.SampleDesc.Count = 1;
		previewDesc.Usage = D3D11_USAGE_DEFAULT;
		previewDesc.BindFlags =
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		DX::ThrowIfFailed(a_device->CreateTexture2D(
			&previewDesc, nullptr, _previewTexture.put()));
		DX::ThrowIfFailed(a_device->CreateShaderResourceView(
			_previewTexture.get(), nullptr, _previewSRV.put()));
		DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(
			_previewTexture.get(), nullptr, _previewUAV.put()));
		cs::render::annotation::SetName(
			_previewTexture.get(), "DynamicCubemaps/Preview.Texture");
		cs::render::annotation::SetName(
			_previewSRV.get(), "DynamicCubemaps/Preview.SRV");
		cs::render::annotation::SetName(
			_previewUAV.get(), "DynamicCubemaps/Preview.UAV");

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
		bufferDesc = cs::buffer::ConstantBufferDesc<BC6HEncodeCB>();
		DX::ThrowIfFailed(a_device->CreateBuffer(
			&bufferDesc, nullptr, _bc6hBuffer.put()));
		cs::render::annotation::SetName(
			_bc6hBuffer.get(), "DynamicCubemaps/BC6H.Buffer");

		const HRESULT loadResult = DirectX::CreateDDSTextureFromFile(
			a_device,
			kDefaultCubemapPath,
			_defaultCubemapResource.put(),
			_defaultCubemap.put());
		DX::ThrowIfFailed(loadResult);
		cs::render::annotation::SetName(
			_defaultCubemap.get(), "DynamicCubemaps/Default.SRV");
		CompressToBC6H(false);
		CompressToBC6H(true);
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
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable for fullscreen debug";
			_validationDetail = a_error;
			return false;
		}

		const auto validateTarget = [&a_error](
			cs::engine::ShaderInjectionTarget a_target,
			bool a_requiresDebug) {
			const auto snapshot =
				cs::engine::GetShaderInjectionTargetSnapshot(a_target);
			const auto define = snapshot.defines.find(
				cs::engine::shader_injection_defines::kDynamicCubemaps);
			const bool contributed =
				define != snapshot.defines.end() && define->second == "1";
			const auto debugDefine = snapshot.defines.find(
				cs::engine::shader_injection_defines::
					kDynamicCubemapsFullscreenDebug);
			const bool debugContributed =
				debugDefine != snapshot.defines.end() &&
				debugDefine->second == "1";
			if (snapshot.requested &&
				snapshot.compileComplete &&
				snapshot.swappable &&
				!snapshot.slotCollision &&
				contributed &&
				(!a_requiresDebug || debugContributed)) {
				return true;
			}
			a_error =
				"'" + snapshot.name +
				"' cannot deliver dynamic cubemaps (requested=" +
				std::to_string(snapshot.requested) +
				" compile_complete=" +
				std::to_string(snapshot.compileComplete) +
				" swappable=" + std::to_string(snapshot.swappable) +
				" slot_collision=" +
				std::to_string(snapshot.slotCollision) +
				" contributed=" + std::to_string(contributed) +
				" debug_contributed=" +
				std::to_string(debugContributed) + ")";
			return false;
		};
		if (!validateTarget(
				cs::engine::ShaderInjectionTarget::kBsdfComposite,
				true) ||
			!validateTarget(
				cs::engine::ShaderInjectionTarget::kBsLighting,
				false) ||
			!validateTarget(
				cs::engine::ShaderInjectionTarget::kBsWater,
				false)) {
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
		auto* context = cs::engine::GetImmediateContext();
		_engineBindings.Restore(context);
	}

	void DynamicCubemaps::BindCubemaps(ID3D11DeviceContext* a_context)
	{
		if (!a_context) {
			return;
		}
		std::array<ID3D11ShaderResourceView*, 2> views{};
		if (_injectionsOperational.load(std::memory_order_acquire) &&
			_resourcesReady.load(std::memory_order_acquire)) {
			views = {
				_environmentBC6H.srv.get(),
				_reflectionsBC6H.srv.get()
			};
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

	ID3D11ShaderResourceView*
		DynamicCubemaps::ResolveReflectionFallback() const noexcept
	{
		// FO4 has no global reflection cube, so inference uses the bundled fallback.
		return _defaultCubemap.get();
	}

	void DynamicCubemaps::ResetCapture()
	{
		_baseStream.reset = true;
		_reflectionsStream.reset = true;
		_nextTask.store(
			NextTask::kCaptureInferAndIrradianceA,
			std::memory_order_relaxed);
	}

	void DynamicCubemaps::UpdateCubemap()
	{
		if (!_injectionsOperational.load(std::memory_order_acquire) ||
			!_resourcesReady.load(std::memory_order_acquire) ||
			!_enabled.load(std::memory_order_acquire)) {
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
				NextTask::kIrradianceBBAndBC6H,
				std::memory_order_relaxed);
			break;
		case NextTask::kIrradianceBBAndBC6H:
			Irradiance(false, kMipLevels - 1, kMipLevels, false);
			CompressToBC6H(false);
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
				NextTask::kIrradianceBBAndBC6H2,
				std::memory_order_relaxed);
			break;
		case NextTask::kIrradianceBBAndBC6H2:
			Irradiance(true, kMipLevels - 1, kMipLevels, false);
			CompressToBC6H(true);
			_nextTask.store(
				NextTask::kCaptureInferAndIrradianceA,
				std::memory_order_relaxed);
			break;
		}
		RenderCubemapPreview();
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

		auto* reflectionFallback = ResolveReflectionFallback();
		_usedEngineReflectionFallback.store(false, std::memory_order_relaxed);
		_reflectionFallbackResolved.store(true, std::memory_order_relaxed);

		std::array<ID3D11ShaderResourceView*, 3> srvs{
			stream.color.srv.get(),
			reflectionFallback,
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

	void DynamicCubemaps::CompressToBC6H(bool a_reflections)
	{
		auto* context = cs::engine::GetImmediateContext();
		if (!context || !_bc6hEncodeCS || !_bc6hBuffer) {
			return;
		}

		ID3D11ShaderResourceView* source =
			a_reflections ?
				_reflectionsArraySRV.get() :
				_environmentArraySRV.get();
		context->CSSetShaderResources(0, 1, &source);
		context->CSSetShader(_bc6hEncodeCS.get(), nullptr, 0);
		ID3D11Buffer* buffer = _bc6hBuffer.get();
		context->CSSetConstantBuffers(0, 1, &buffer);

		for (std::uint32_t level = 0;
			 level < kBc6hMipLevels;
			 ++level) {
			const std::uint32_t sourceSize =
				std::max(1u, kCubemapSize >> level);
			const std::uint32_t blocks =
				std::max(1u, sourceSize / 4);
			const BC6HEncodeCB constants{
				.textureSizeInBlocksX = blocks,
				.textureSizeInBlocksY = blocks,
				.mipLevel = level
			};
			UpdateBuffer(
				context,
				_bc6hBuffer.get(),
				&constants,
				sizeof(constants));
			ID3D11UnorderedAccessView* output =
				_bc6hScratchUAVs[level].get();
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->Dispatch(
				DispatchGroups(blocks),
				DispatchGroups(blocks),
				6);
			_compressionDispatchCount.fetch_add(
				1, std::memory_order_relaxed);
		}
		UnbindCompute(context);

		auto& destination =
			a_reflections ? _reflectionsBC6H : _environmentBC6H;
		context->CopyResource(
			destination.texture.get(),
			_bc6hScratchTexture.get());
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

	void DynamicCubemaps::RenderCubemapPreview()
	{
		const auto visualization =
			_debugVisualization.load(std::memory_order_acquire);
		if (visualization != DebugVisualization::kCaptureInput &&
			visualization != DebugVisualization::kFilteredReflections) {
			return;
		}
		auto* context = cs::engine::GetImmediateContext();
		if (!context || !_previewCS || !_previewUAV) {
			return;
		}

		ID3D11ShaderResourceView* source =
			visualization == DebugVisualization::kCaptureInput ?
				_reflectionsStream.color.srv.get() :
				_reflections.srv.get();
		if (!source) {
			return;
		}
		context->CSSetShaderResources(0, 1, &source);
		ID3D11UnorderedAccessView* output = _previewUAV.get();
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		ID3D11SamplerState* sampler = _computeSampler.get();
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(_previewCS.get(), nullptr, 0);
		context->Dispatch(
			DispatchGroups(kPreviewWidth),
			DispatchGroups(kPreviewHeight),
			1);
		UnbindCompute(context);
		_previewDispatchCount.fetch_add(1, std::memory_order_relaxed);
		_previewPopulated.store(true, std::memory_order_release);
	}

	void DynamicCubemaps::CollectTelemetry(
		cs::telemetry::Sink& a_sink) const
	{
		const auto task = _nextTask.load(std::memory_order_relaxed);
		const auto taskName = [task]() -> std::string_view {
			switch (task) {
			case NextTask::kCaptureInferAndIrradianceA:
				return "base_capture_infer_mip_1";
			case NextTask::kIrradianceBA:
				return "base_mips_2_7";
			case NextTask::kIrradianceBBAndBC6H:
				return "base_mip_8_bc6h";
			case NextTask::kCaptureInferAndIrradianceA2:
				return "reflections_capture_infer_mip_1";
			case NextTask::kIrradianceBA2:
				return "reflections_mips_2_7";
			case NextTask::kIrradianceBBAndBC6H2:
				return "reflections_mip_8_bc6h";
			}
			return "unknown";
		}();
		const bool fallbackResolved =
			_reflectionFallbackResolved.load(std::memory_order_relaxed);
		const bool engineFallback =
			_usedEngineReflectionFallback.load(std::memory_order_relaxed);
		a_sink
			.Field(
				"operational",
				_injectionsOperational.load(std::memory_order_relaxed))
			.Field(
				"configured_enabled",
				_enabled.load(std::memory_order_relaxed))
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
				engineFallback)
			.Field(
				"reflection_fallback_source",
				fallbackResolved ?
					(engineFallback ? "engine_cube_0" : "bundled_default") :
					"unresolved")
			.Field(
				"next_task",
				static_cast<std::int64_t>(task))
			.Field("task_state", taskName)
			.Field(
				"pipeline_advances",
				static_cast<std::int64_t>(
					_dispatchCount.load(std::memory_order_relaxed)))
			.Field(
				"task_dispatches",
				static_cast<std::int64_t>(
					_dispatchCount.load(std::memory_order_relaxed)))
			.Field(
				"compression_dispatches",
				static_cast<std::int64_t>(
					_compressionDispatchCount.load(
						std::memory_order_relaxed)))
			.Field(
				"preview_dispatches",
				static_cast<std::int64_t>(
					_previewDispatchCount.load(std::memory_order_relaxed)))
			.Field(
				"debug_visualization",
				DebugVisualizationName(
					_debugVisualization.load(std::memory_order_relaxed)))
			.Field(
				"repeat_callbacks",
				static_cast<std::int64_t>(
					_repeatCallbacks.load(std::memory_order_relaxed)));
	}

	cs::DynamicCubemapsFeatureData
		DynamicCubemaps::GetCommonBufferData() const
	{
		cs::DynamicCubemapsFeatureData data{};
		if (_injectionsOperational.load(std::memory_order_acquire)) {
			data.Enabled =
				_enabled.load(std::memory_order_acquire) ? 1u : 0u;
			data.DebugVisualization = static_cast<std::uint32_t>(
				_debugVisualization.load(std::memory_order_acquire));
		}
		return data;
	}

	void DynamicCubemaps::DrawSettings()
	{
		const bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);
		ImGui::TextDisabled(
			"Off restores native probe reflections and pauses capture.");
		if (changed) {
			PublishSettings();
			SaveSettings();
		}
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void DynamicCubemaps::RestoreDefaultSettings()
	{
		_settings = Settings{};
		PublishSettings();
		SaveSettings();
	}
}
