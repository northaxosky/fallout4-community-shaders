#include "Render/TemporalRendererInternals.h"

namespace cs::render
{
	using namespace renderer_detail;

	bool TemporalRenderer::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
	{
		L->debug("Creating texture resources for method {} ({})",
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

		if (!IsExternalUpscaler(a_upscalemethod)) {
			return true;
		}

		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc) ||
			!HasSDRUpscalingContract(frameBufferDesc)) {
			L->error("RT0 is unavailable or incompatible with the SDR upscaling contract");
			return false;
		}

		D3D11_TEXTURE2D_DESC colorDesc = frameBufferDesc;
		colorDesc.Usage = D3D11_USAGE_DEFAULT;
		colorDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		colorDesc.CPUAccessFlags = 0;
		colorDesc.MiscFlags = 0;
		auto* device = cs::engine::GetDevice();
		if (!device) {
			return false;
		}

		const auto createTexture = [device](
			const D3D11_TEXTURE2D_DESC& a_desc,
			std::string_view a_name) {
			auto texture =
				render::TemporalPipeline::Get().CreateSuperResolutionTexture(
					a_desc, a_name);
			if (!texture) {
				return static_cast<cs::buffer::Texture2D*>(nullptr);
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = a_desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = a_desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);
			if ((a_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0) {
				DX::ThrowIfFailed(device->CreateRenderTargetView(
					texture->resource.get(), nullptr, texture->rtv.put()));
				cs::render::annotation::SetName(
					texture->rtv.get(), std::string(a_name) + ".RTV");
			}
			texture->SetName(
				std::string(a_name) + ".Texture",
				std::string(a_name) + ".SRV",
				std::string(a_name) + ".UAV",
				std::string(a_name) + ".RTV");
			return texture.release();
		};

		if (!upscalingTexture) {
			upscalingTexture = createTexture(
				colorDesc, "Upscaling/InputColor");
		}
		if (!superResolutionDepthTexture) {
			auto depthDesc = colorDesc;
			depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
			superResolutionDepthTexture = createTexture(depthDesc, "Upscaling/SuperResolutionDepth");
		}

		auto maskDesc = colorDesc;
		maskDesc.Format = DXGI_FORMAT_R8_UNORM;
		if (!reactiveMaskTexture) {
			reactiveMaskTexture = createTexture(
				maskDesc, "Upscaling/ReactiveMask");
		}
		if (!transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture = createTexture(
				maskDesc, "Upscaling/TransparencyCompositionMask");
		}

		if (!motionVectorCopyTexture) {
			auto* motionVector = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
			if (motionVector) {
				D3D11_TEXTURE2D_DESC motionTexDesc{};
				motionVector->GetDesc(&motionTexDesc);
				motionTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				motionTexDesc.CPUAccessFlags = 0;
				motionTexDesc.MiscFlags = 0;
				motionVectorCopyTexture = createTexture(
					motionTexDesc, "Upscaling/MotionVectorCopy");
			}
		}

		if (!sharpenerTexture) {
			sharpenerTexture = createTexture(
				colorDesc, "Upscaling/ProviderOutput");
		}
		if (!publicationTexture) {
			auto publicationDesc = colorDesc;
			publicationDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			publicationTexture = createTexture(
				publicationDesc, "Upscaling/PublicationScratch");
		}

		return HasRequiredResources(a_upscalemethod);
	}

	bool TemporalRenderer::HasRequiredResources(UpscaleMethod a_upscalemethod) const noexcept
	{
		if (!IsExternalUpscaler(a_upscalemethod)) {
			return true;
		}

		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc) ||
			!HasSDRUpscalingContract(frameBufferDesc) ||
			!MatchesTextureContract(
				upscalingTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM) ||
			!MatchesTextureContract(
				superResolutionDepthTexture,
				frameBufferDesc,
				DXGI_FORMAT_R32_FLOAT) ||
			!MatchesTextureContract(
				motionVectorCopyTexture,
				frameBufferDesc,
				DXGI_FORMAT_R16G16_FLOAT) ||
			!MatchesTextureContract(
				reactiveMaskTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8_UNORM) ||
			!MatchesTextureContract(
				transparencyCompositionMaskTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8_UNORM)) {
			return false;
		}

		if (a_upscalemethod == UpscaleMethod::kFSR ||
			a_upscalemethod == UpscaleMethod::kFSR4) {
			return render::TemporalPipeline::Get()
					   .IsSuperResolutionRuntimeReady(
						   a_upscalemethod == UpscaleMethod::kFSR4
							   ? render::temporal::SuperResolutionMethod::kFSR4
							   : render::temporal::SuperResolutionMethod::kFSR3) &&
				sharpenerTexture &&
				publicationTexture &&
				sharpenerTexture->resource.get() != upscalingTexture->resource.get() &&
				publicationTexture->resource.get() != upscalingTexture->resource.get() &&
				MatchesTextureContract(
					sharpenerTexture,
					frameBufferDesc,
					DXGI_FORMAT_R8G8B8A8_UNORM) &&
				MatchesTextureContract(
					publicationTexture,
					frameBufferDesc,
					DXGI_FORMAT_R8G8B8A8_UNORM);
		}
		return sharpenerTexture &&
			publicationTexture &&
			sharpenerTexture->resource.get() != upscalingTexture->resource.get() &&
			publicationTexture->resource.get() != upscalingTexture->resource.get() &&
			MatchesTextureContract(
				sharpenerTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM) &&
			MatchesTextureContract(
				publicationTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM);
	}

	void TemporalRenderer::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
	{
		L->debug("Destroying texture resources for method {} ({})",
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

		const auto destroy = [](cs::buffer::Texture2D*& a_texture) {
			if (!a_texture) {
				return;
			}
			a_texture->Reset();
			delete a_texture;
			a_texture = nullptr;
		};

		destroy(reactiveMaskTexture);
		destroy(transparencyCompositionMaskTexture);
		destroy(motionVectorCopyTexture);
		destroy(superResolutionDepthTexture);
		destroy(upscalingTexture);
		destroy(sharpenerTexture);
		destroy(publicationTexture);
	}

	bool TemporalRenderer::CheckResources(UpscaleMethod a_upscalemethod)
	{
		static auto previousUpscaleMode = UpscaleMethod::kTAA;
		static auto previousQualityMode = std::numeric_limits<std::uint32_t>::max();
		static std::uint32_t previousRenderWidth = 0;
		static std::uint32_t previousRenderHeight = 0;
		static std::uint32_t previousOutputWidth = 0;
		static std::uint32_t previousOutputHeight = 0;
		const auto* currentState = cs::engine::GetGraphicsState();
		const auto [currentRenderWidth, currentRenderHeight] = GetRenderSize();
		const std::uint32_t currentOutputWidth =
			currentState ? currentState->screenWidth : 0;
		const std::uint32_t currentOutputHeight =
			currentState ? currentState->screenHeight : 0;

		// Provider contexts are quality-sized.
		if (previousUpscaleMode == a_upscalemethod &&
			previousQualityMode == settings.qualityMode &&
			previousRenderWidth == currentRenderWidth &&
			previousRenderHeight == currentRenderHeight &&
			previousOutputWidth == currentOutputWidth &&
			previousOutputHeight == currentOutputHeight &&
			HasRequiredResources(a_upscalemethod)) {
			return true;
		}

		L->debug("Resource change detected - Upscale: {} ({}) -> {} ({}), quality: {} -> {}",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode),
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod),
			previousQualityMode, settings.qualityMode);

		render::temporal::ProviderResult release{
			.code = render::temporal::ProviderResultCode::kSuccess
		};
		if (previousUpscaleMode == UpscaleMethod::kDLSS) {
			release =
				render::TemporalPipeline::Get().DestroySuperResolutionResources(
					render::temporal::SuperResolutionMethod::kDLSS);
		} else if (previousUpscaleMode == UpscaleMethod::kFSR ||
			previousUpscaleMode == UpscaleMethod::kFSR4) {
			release =
				render::TemporalPipeline::Get().DestroySuperResolutionResources(
					previousUpscaleMode == UpscaleMethod::kFSR4
						? render::temporal::SuperResolutionMethod::kFSR4
						: render::temporal::SuperResolutionMethod::kFSR3);
		}
		if (!release.Succeeded()) {
			render::TemporalPipeline::Get().PostFailure(
				release.failureDomain ==
						render::temporal::FailureDomain::kNone
					? render::temporal::FailureDomain::kSuperResolution
					: release.failureDomain,
				release.message.empty()
					? "Super-resolution resources could not be retired."
					: release.message);
			return false;
		}

		DestroyUpscalingTextureResources(a_upscalemethod);

		const bool ok = CreateUpscalingTextureResources(a_upscalemethod);

		previousUpscaleMode = a_upscalemethod;
		previousQualityMode = settings.qualityMode;
		previousRenderWidth = currentRenderWidth;
		previousRenderHeight = currentRenderHeight;
		previousOutputWidth = currentOutputWidth;
		previousOutputHeight = currentOutputHeight;
		render::TemporalPipeline::Get().RequestSuperResolutionReset();
		render::TemporalPipeline::Get().RequestFrameGenerationReset();
		return ok;
	}

	ID3D11ComputeShader* TemporalRenderer::GetEncodeTexturesCS()
	{
		auto upscaleMethod = GetUpscaleMethod();
		uint methodIndex = (uint)upscaleMethod;

		if (!encodeTexturesCS[methodIndex]) {
			L->debug("Compiling EncodeTexturesCS.hlsl for upscale method {}", methodIndex);

			std::vector<std::pair<const char*, const char*>> defines = {
				{ "FO4CS_SUBSTRATE", "1" }, { "DEPTH_OUTPUT", "1" }
			};

			switch (upscaleMethod) {
			case UpscaleMethod::kDLSS:
				defines.push_back({ "DLSS", "" });
				break;
			case UpscaleMethod::kFSR:
			case UpscaleMethod::kFSR4:
				defines.push_back({ "FSR", "" });
				break;
			default:
				break;
			}

			encodeTexturesCS[methodIndex].attach(
				(ID3D11ComputeShader*)cs::util::CompileShader(kEncodeTexturesPath, defines, "cs_5_0"));
			cs::render::annotation::SetName(
				encodeTexturesCS[methodIndex].get(),
				std::format("Upscaling/EncodeTextures[{}].CS", methodIndex));
		}
		return encodeTexturesCS[methodIndex].get();
	}

	ID3D11PixelShader* TemporalRenderer::GetDepthRefractionUpscalePS()
	{
		if (!depthRefractionUpscalePS) {
			L->debug("Compiling DepthRefractionUpscalePS.hlsl");
			std::vector<std::pair<const char*, const char*>> defines = {
				{ "PSHADER", "" },
				{ "FO4CS_SUBSTRATE", "1" }
			};
			depthRefractionUpscalePS.attach(
				(ID3D11PixelShader*)cs::util::CompileShader(kDepthRefractionUpscalePath, defines, "ps_5_0"));
			cs::render::annotation::SetName(
				depthRefractionUpscalePS.get(),
				"Upscaling/DepthRefractionUpscale.PS");
		}

		return depthRefractionUpscalePS.get();
	}

	ID3D11VertexShader* TemporalRenderer::GetUpscaleVS()
	{
		if (!upscaleVS) {
			L->debug("Compiling UpscaleVS.hlsl");
			upscaleVS.attach(
				(ID3D11VertexShader*)cs::util::CompileShader(kUpscaleVSPath, { { "VSHADER", "" } }, "vs_5_0"));
			cs::render::annotation::SetName(
				upscaleVS.get(), "Upscaling/Fullscreen.VS");
		}

		return upscaleVS.get();
	}

	ID3D11PixelShader* TemporalRenderer::GetSpatialFallbackPS()
	{
		if (!spatialFallbackPS) {
			L->debug("Compiling SpatialFallbackPS.hlsl");
			spatialFallbackPS.attach(
				static_cast<ID3D11PixelShader*>(cs::util::CompileShader(
					kSpatialFallbackPath, {}, "ps_5_0")));
			cs::render::annotation::SetName(
				spatialFallbackPS.get(), "Upscaling/SpatialFallback.PS");
		}
		return spatialFallbackPS.get();
	}

	ID3D11PixelShader* TemporalRenderer::GetSSLRRaytracingPS()
	{
		if (!sslrRaytracingPS && !_sslrCompileFailed) {
			L->debug("Compiling BSImagespaceShaderSSLRRaytracing.hlsl");
			sslrRaytracingPS.attach((ID3D11PixelShader*)cs::util::CompileShader(
				kSSLRRaytracingPath, {}, "ps_5_0"));
			cs::render::annotation::SetName(
				sslrRaytracingPS.get(), "Upscaling/SSLRRaytracing.PS");
			if (!sslrRaytracingPS) {
				_sslrCompileFailed = true;
				L->error("BSImagespaceShaderSSLRRaytracing.hlsl failed to compile; SSR runs unpatched");
			}
		}
		return sslrRaytracingPS.get();
	}

	void TemporalRenderer::PatchSSRShader()
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* shader = GetSSLRRaytracingPS();
		if (context && shader) {
			context->PSSetShader(shader, nullptr, 0);
		}
	}

	void TemporalRenderer::SetupResources()
	{
		auto* device = cs::engine::GetDevice();
		if (!device) {
			L->error("Renderer device is not ready; resources were not created");
			return;
		}

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilDesc.StencilEnable = false;

		DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));
		cs::render::annotation::SetName(
			upscaleDepthStencilState.get(), "Upscaling/DepthUpscale.DepthStencilState");

		if (!jitterCB) {
			jitterCB = new cs::buffer::ConstantBuffer(cs::buffer::ConstantBufferDesc<JitterCB>());
			jitterCB->SetName("Upscaling/JitterConstants.Buffer");
		}

		if (!upscalingDataCB) {
			upscalingDataCB =
				new cs::buffer::ConstantBuffer(cs::buffer::ConstantBufferDesc<UpscalingDataCB>());
			upscalingDataCB->SetName("Upscaling/Constants.Buffer");
		}

		if (!_frameGenerationCopyCB) {
			_frameGenerationCopyCB =
				new cs::buffer::ConstantBuffer(cs::buffer::ConstantBufferDesc<FrameGenerationCopyCB>());
			_frameGenerationCopyCB->SetName(
				"Upscaling/FrameGenerationCopyConstants.Buffer");
		}

		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = false;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, upscaleBlendState.put()));
		cs::render::annotation::SetName(
			upscaleBlendState.get(), "Upscaling/DepthUpscale.BlendState");

		D3D11_RASTERIZER_DESC rasterizerDesc = {};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.FrontCounterClockwise = false;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.0f;
		rasterizerDesc.SlopeScaledDepthBias = 0.0f;
		rasterizerDesc.DepthClipEnable = false;
		rasterizerDesc.ScissorEnable = false;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = false;
		DX::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDesc, upscaleRasterizerState.put()));
		cs::render::annotation::SetName(
			upscaleRasterizerState.get(), "Upscaling/DepthUpscale.RasterizerState");

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));
		cs::render::annotation::SetName(
			linearSampler.get(), "Upscaling/LinearClamp.Sampler");

		const auto method = GetUpscaleMethod();
		const auto sizeResult =
			PrepareRenderSize(cs::engine::GetGraphicsState(), method);
		const bool resourcesReady =
			sizeResult.Succeeded() && CheckResources(method);
		const bool ok =
			resourcesReady && PreflightExternalResolve(method);
		_spatialFallbackPreflightReady.store(ok, std::memory_order_release);

		if (IsFrameGenerationDx12PathActive() && !_copyDepthForFrameGenerationCS) {
			_copyDepthForFrameGenerationCS.attach(
				static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(
					kCopyDepthForFrameGenerationPath,
					{},
					"cs_5_0")));
			if (!_copyDepthForFrameGenerationCS) {
				render::TemporalPipeline::Get().FailFrameGenerationFrame(
					"depth capture shader compilation failed");
			} else {
				cs::render::annotation::SetName(
					_copyDepthForFrameGenerationCS.get(),
					"Upscaling/CopyDepthForFrameGeneration.CS");
			}
		}

		// Failed setup prevents the plugin from committing reduced-resolution rendering.
		_resourcesReady.store(ok, std::memory_order_release);
		if (ok) {
			_renderSize.CommitRequested();
			resolutionScale = {
				_renderSize.WidthRatio(),
				_renderSize.HeightRatio()
			};
			L->info("Created upscaling resources after render-target creation");
		} else if (IsExternalUpscaler(method)) {
			if (!sizeResult.Succeeded()) {
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kSuperResolution,
					std::format(
						"Super-resolution render-size query failed: {} (SDK {})",
						sizeResult.message,
						sizeResult.sdkResult));
			} else {
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kEngine,
					"External super resolution resources failed preflight before reduced-resolution commitment.");
			}
			RestoreNativeFrameState();
			L->error(
				"Upscaling resources were not created after render-target creation; "
				"the game renders natively");
		}
	}

	void TemporalRenderer::InvalidateEngineDerivedResources()
	{
		render::TemporalPipeline::Get().AdvanceEngineResourceGeneration();
		InvalidateFirstPersonAlphaState();
		dynamicResolution.Release();
		samplerBias.Release();
		_superResolutionDebugSnapshot.request.Invalidate();
		ResetDebugSnapshotResources();
		_frameGenerationDebugSnapshot.request.Invalidate();
		ResetFrameGenerationDebugSnapshotResources();
		_imagespaceRatiosNeutralized = false;
		// Restore native ratios/offsets and clear the latch so a later non-driving frame can't strand a sub-rect.
		RestoreNativeFrameState();
		_resourcesReady.store(false, std::memory_order_release);
		_spatialFallbackPreflightReady.store(false, std::memory_order_release);

		const auto method = GetUpscaleMethod();
		render::temporal::ProviderResult release{
			.code = render::temporal::ProviderResultCode::kSuccess
		};
		if (method == UpscaleMethod::kDLSS) {
			release =
				render::TemporalPipeline::Get().DestroySuperResolutionResources(
				render::temporal::SuperResolutionMethod::kDLSS);
		} else if (method == UpscaleMethod::kFSR ||
			method == UpscaleMethod::kFSR4) {
			release =
				render::TemporalPipeline::Get().DestroySuperResolutionResources(
				method == UpscaleMethod::kFSR4
					? render::temporal::SuperResolutionMethod::kFSR4
					: render::temporal::SuperResolutionMethod::kFSR3);
		}
		if (!release.Succeeded()) {
			render::TemporalPipeline::Get().PostFailure(
				release.failureDomain ==
						render::temporal::FailureDomain::kNone
					? render::temporal::FailureDomain::kSuperResolution
					: release.failureDomain,
				release.message.empty()
					? "Super-resolution resources could not be retired."
					: release.message);
			return;
		}

		DestroyUpscalingTextureResources(method);
		_upscaledThisFrame = false;
		_spatialFallbackThisFrame.store(false, std::memory_order_release);
		_srPublishedToFramebuffer.store(false, std::memory_order_release);

		auto* state = cs::engine::GetGraphicsState();
		const auto sizeResult = PrepareRenderSize(state, method);
		bool recreated = sizeResult.Succeeded();
		recreated = recreated &&
			CreateUpscalingTextureResources(method);
		recreated = recreated && PreflightExternalResolve(method);
		if (recreated) {
			_renderSize.CommitRequested();
			resolutionScale = {
				_renderSize.WidthRatio(),
				_renderSize.HeightRatio()
			};
		} else {
			if (IsExternalUpscaler(method)) {
				if (!sizeResult.Succeeded()) {
					render::TemporalPipeline::Get().PostFailure(
						render::temporal::FailureDomain::kSuperResolution,
						std::format(
							"Super-resolution render-size query failed: {} (SDK {})",
							sizeResult.message,
							sizeResult.sdkResult));
				} else {
					render::TemporalPipeline::Get().PostFailure(
						render::temporal::FailureDomain::kEngine,
						"External super resolution resources failed resize preflight before reduced-resolution commitment.");
				}
			}
			RestoreNativeFrameState();
		}
		_resourcesReady.store(recreated, std::memory_order_release);
		render::TemporalPipeline::Get().RequestSuperResolutionReset();
		render::TemporalPipeline::Get().RequestFrameGenerationReset();
	}
}
