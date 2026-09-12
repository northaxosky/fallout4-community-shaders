#include "Render/TemporalRendererInternals.h"

namespace cs::render
{
	using namespace renderer_detail;

	std::span<const FeatureDebugView> TemporalRenderer::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "render_subrect",
				.label = "Active render sub-rect (RT4)",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature&) {
					return TemporalRenderer::GetSingleton()->GetRenderSubrectDebugTexture();
				}
			},
			FeatureDebugView{
				.id = "render_proxy",
				.label = "Render-resolution proxy (RT4)",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature&) {
					return TemporalRenderer::GetSingleton()->GetProxyDebugTexture();
				}
			},
			FeatureDebugView{
				.id = "motion_vectors",
				.label = "Provider motion-vector input",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature&) {
					return TemporalRenderer::GetSingleton()->GetMotionVectorsDebugTexture();
				}
			},
			FeatureDebugView{
				.id = "provider_output",
				.label = "Provider output (RT0)",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature&) {
					return TemporalRenderer::GetSingleton()->GetProviderOutputDebugTexture();
				}
			}
		};
		return views;
	}

	void TemporalRenderer::SetDebugView(std::string_view a_view) noexcept
	{
		DebugView view = DebugView::kOff;
		if (a_view == "render_subrect") {
			view = DebugView::kRenderSubrect;
		} else if (a_view == "render_proxy") {
			view = DebugView::kProxy;
		} else if (a_view == "motion_vectors") {
			view = DebugView::kMotionVectors;
		} else if (a_view == "provider_output") {
			view = DebugView::kProviderOutput;
		}
		const auto previous = _debugView.exchange(view, std::memory_order_acq_rel);
		if (previous != view) {
			_superResolutionDebugSnapshot.request.Reset();
			_superResolutionDebugSnapshot.request.Select(
				view != DebugView::kOff);
			ResetDebugSnapshotResources();
		}
	}

	void TemporalRenderer::RefreshDebugSnapshot() noexcept
	{
		if (HasDebugSnapshotSelection())
			_superResolutionDebugSnapshot.request.Refresh();
	}

	bool TemporalRenderer::HasDebugSnapshotSelection() const noexcept
	{
		return _debugView.load(std::memory_order_acquire) != DebugView::kOff;
	}

	bool TemporalRenderer::DebugSnapshotPending() const noexcept
	{
		return _superResolutionDebugSnapshot.request.Pending() != 0;
	}

	void TemporalRenderer::ResetDebugSnapshotResources() noexcept
	{
		_superResolutionDebugSnapshot.ResetResources();
		_providerOutputDebugAllocated.store(false, std::memory_order_release);
		_providerOutputDebugWidth.store(0, std::memory_order_relaxed);
		_providerOutputDebugHeight.store(0, std::memory_order_relaxed);
		_providerOutputDebugFailure.store(
			ProviderOutputDebugFailure::kNotInitialized,
			std::memory_order_release);
	}

	void TemporalRenderer::FrozenTextureSnapshot::ResetResources() noexcept
	{
		texture = nullptr;
		view = nullptr;
		sourceDesc = {};
		capturedView = 0;
		caption.clear();
		failure = FrozenTextureSnapshotFailure::kNone;
		result = S_OK;
	}

	bool TemporalRenderer::CaptureFrozenTextureSnapshot(
		FrozenTextureSnapshot& a_snapshot,
		std::uint8_t a_view,
		ID3D11Texture2D* a_source,
		const D3D11_SHADER_RESOURCE_VIEW_DESC& a_viewDesc,
		std::string a_caption,
		const char* a_textureName,
		const char* a_viewName,
		std::string_view a_logName)
	{
		const auto request = a_snapshot.request.Pending();
		if (!request)
			return false;
		if (!a_source) {
			a_snapshot.failure = FrozenTextureSnapshotFailure::kNoTexture;
			return false;
		}
		auto* device = cs::engine::GetDevice();
		auto* context = cs::engine::GetImmediateContext();
		if (!device) {
			a_snapshot.failure = FrozenTextureSnapshotFailure::kNoDevice;
			return false;
		}
		if (!context) {
			a_snapshot.failure = FrozenTextureSnapshotFailure::kNoContext;
			return false;
		}

		D3D11_TEXTURE2D_DESC sourceDesc{};
		a_source->GetDesc(&sourceDesc);
		const bool needsResources = !a_snapshot.texture || !a_snapshot.view ||
			!HaveSameTextureDescription(sourceDesc, a_snapshot.sourceDesc);
		if (needsResources) {
			auto snapshotDesc = sourceDesc;
			snapshotDesc.Usage = D3D11_USAGE_DEFAULT;
			snapshotDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			snapshotDesc.CPUAccessFlags = 0;
			snapshotDesc.MiscFlags = 0;

			winrt::com_ptr<ID3D11Texture2D> snapshot;
			winrt::com_ptr<ID3D11ShaderResourceView> snapshotView;
			auto result =
				device->CreateTexture2D(&snapshotDesc, nullptr, snapshot.put());
			if (FAILED(result)) {
				a_snapshot.failure =
					FrozenTextureSnapshotFailure::kTextureCreationFailed;
			} else {
				result = device->CreateShaderResourceView(
					snapshot.get(), &a_viewDesc, snapshotView.put());
				if (FAILED(result)) {
					a_snapshot.failure =
						FrozenTextureSnapshotFailure::kViewCreationFailed;
				}
			}
			if (FAILED(result)) {
				if (!a_logName.empty() && result != a_snapshot.result) {
					L->error(
						"{} allocation failed: HRESULT {:#010x}",
						a_logName,
						static_cast<std::uint32_t>(result));
				}
				a_snapshot.result = result;
				return false;
			}
			cs::render::annotation::SetName(snapshot.get(), a_textureName);
			cs::render::annotation::SetName(snapshotView.get(), a_viewName);
			a_snapshot.texture = std::move(snapshot);
			a_snapshot.view = std::move(snapshotView);
			a_snapshot.sourceDesc = sourceDesc;
		}

		cs::render::annotation::ScopedEvent event("Temporal/CaptureDebugSnapshot");
		cs::engine::CopyResourcePreservingOM(
			context, a_snapshot.texture.get(), a_source);
		a_snapshot.capturedView = a_view;
		a_snapshot.caption = std::move(a_caption);
		a_snapshot.failure = FrozenTextureSnapshotFailure::kNone;
		a_snapshot.result = S_OK;
		a_snapshot.request.Captured(request);
		return true;
	}

	bool TemporalRenderer::CaptureDebugSnapshot(
		DebugView a_view,
		ID3D11ShaderResourceView* a_source,
		std::string a_caption)
	{
		if (_debugView.load(std::memory_order_acquire) != a_view || !a_source)
			return false;
		winrt::com_ptr<ID3D11Resource> sourceResource;
		a_source->GetResource(sourceResource.put());
		winrt::com_ptr<ID3D11Texture2D> sourceTexture;
		if (!sourceResource ||
			FAILED(sourceResource->QueryInterface(
				IID_PPV_ARGS(sourceTexture.put())))) {
			return false;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		a_source->GetDesc(&viewDesc);
		return CaptureFrozenTextureSnapshot(
			_superResolutionDebugSnapshot,
			static_cast<std::uint8_t>(a_view),
			sourceTexture.get(),
			viewDesc,
			std::move(a_caption),
			"Upscaling/DebugSnapshot.Texture",
			"Upscaling/DebugSnapshot.SRV",
			"Upscaling debug snapshot");
	}

	void TemporalRenderer::SetFrameGenerationDebugView(
		std::string_view a_view) noexcept
	{
		FrameGenerationDebugView view = FrameGenerationDebugView::kOff;
		if (a_view == "hudless_color") {
			view = FrameGenerationDebugView::kHudless;
		} else if (a_view == "final_color") {
			view = FrameGenerationDebugView::kFinal;
		} else if (a_view == "conditioned_depth") {
			view = FrameGenerationDebugView::kDepth;
		} else if (a_view == "conditioned_motion") {
			view = FrameGenerationDebugView::kMotion;
		}
		const auto previous =
			_frameGenerationDebugView.exchange(view, std::memory_order_acq_rel);
		if (previous != view) {
			_frameGenerationDebugSnapshot.request.Reset();
			_frameGenerationDebugSnapshot.request.Select(
				view != FrameGenerationDebugView::kOff);
			ResetFrameGenerationDebugSnapshotResources();
		}
	}

	void TemporalRenderer::RefreshFrameGenerationDebugSnapshot() noexcept
	{
		if (HasFrameGenerationDebugSnapshotSelection())
			_frameGenerationDebugSnapshot.request.Refresh();
	}

	bool TemporalRenderer::HasFrameGenerationDebugSnapshotSelection() const noexcept
	{
		return _frameGenerationDebugView.load(std::memory_order_acquire) !=
			FrameGenerationDebugView::kOff;
	}

	bool TemporalRenderer::FrameGenerationDebugSnapshotPending() const noexcept
	{
		return _frameGenerationDebugSnapshot.request.Pending() != 0;
	}

	void TemporalRenderer::ResetFrameGenerationDebugSnapshotResources() noexcept
	{
		_frameGenerationDebugSnapshot.ResetResources();
	}

	bool TemporalRenderer::CaptureFrameGenerationDebugSnapshot(
		FrameGenerationDebugView a_view,
		ID3D11ShaderResourceView* a_source,
		std::string a_caption)
	{
		if (_frameGenerationDebugView.load(std::memory_order_acquire) != a_view ||
			!a_source) {
			return false;
		}

		winrt::com_ptr<ID3D11Resource> sourceResource;
		a_source->GetResource(sourceResource.put());
		winrt::com_ptr<ID3D11Texture2D> sourceTexture;
		if (!sourceResource ||
			FAILED(sourceResource->QueryInterface(
				IID_PPV_ARGS(sourceTexture.put())))) {
			return false;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		a_source->GetDesc(&viewDesc);
		return CaptureFrozenTextureSnapshot(
			_frameGenerationDebugSnapshot,
			static_cast<std::uint8_t>(a_view),
			sourceTexture.get(),
			viewDesc,
			std::move(a_caption),
			"FrameGeneration/DebugSnapshot.Texture",
			"FrameGeneration/DebugSnapshot.SRV",
			"Frame-generation debug snapshot");
	}

	FeatureDebugTexture TemporalRenderer::GetFrameGenerationDebugTexture(
		std::string_view a_view) const
	{
		FrameGenerationDebugView view = FrameGenerationDebugView::kOff;
		if (a_view == "hudless_color") {
			view = FrameGenerationDebugView::kHudless;
		} else if (a_view == "final_color") {
			view = FrameGenerationDebugView::kFinal;
		} else if (a_view == "conditioned_depth") {
			view = FrameGenerationDebugView::kDepth;
		} else if (a_view == "conditioned_motion") {
			view = FrameGenerationDebugView::kMotion;
		}

		FeatureDebugTexture texture{
			.unavailableText =
				"Frozen frame-generation snapshot is pending."
		};
		if (view == FrameGenerationDebugView::kOff ||
			_frameGenerationDebugView.load(std::memory_order_acquire) != view ||
			!_frameGenerationDebugSnapshot.request.Ready() ||
			_frameGenerationDebugSnapshot.capturedView !=
				static_cast<std::uint8_t>(view) ||
			!_frameGenerationDebugSnapshot.view) {
			return texture;
		}
		texture.texture = _frameGenerationDebugSnapshot.view.get();
		texture.width = _frameGenerationDebugSnapshot.sourceDesc.Width;
		texture.height = _frameGenerationDebugSnapshot.sourceDesc.Height;
		texture.caption = _frameGenerationDebugSnapshot.caption;
		return texture;
	}

	void TemporalRenderer::CaptureFrameGenerationInputDebugSnapshot()
	{
		const auto selected =
			_frameGenerationDebugView.load(std::memory_order_acquire);
		render::FrameGenerationDebugResource resource;
		std::string_view caption;
		if (selected == FrameGenerationDebugView::kDepth) {
			resource = render::FrameGenerationDebugResource::kDepth;
			caption = "Frozen FG-conditioned depth snapshot; display resolution.";
		} else if (selected == FrameGenerationDebugView::kMotion) {
			resource = render::FrameGenerationDebugResource::kMotion;
			caption = "Frozen FG-conditioned motion snapshot; display resolution.";
		} else {
			return;
		}
		const auto source =
			render::TemporalPipeline::Get().GetFrameGenerationDebugTexture(
				resource);
		CaptureFrameGenerationDebugSnapshot(
			selected, source.srv, std::string(caption));
	}

	void TemporalRenderer::CaptureFrameGenerationHudlessDebugSnapshot()
	{
		if (_frameGenerationDebugView.load(std::memory_order_acquire) !=
			FrameGenerationDebugView::kHudless) {
			return;
		}
		const auto source =
			render::TemporalPipeline::Get().GetFrameGenerationDebugTexture(
				render::FrameGenerationDebugResource::kHudless);
		CaptureFrameGenerationDebugSnapshot(
			FrameGenerationDebugView::kHudless,
			source.srv,
			"Frozen HUD-less color snapshot; display resolution.");
	}

	void TemporalRenderer::CaptureFrameGenerationFinalDebugSnapshot()
	{
		if (_frameGenerationDebugView.load(std::memory_order_acquire) !=
			FrameGenerationDebugView::kFinal) {
			return;
		}
		const auto source =
			render::TemporalPipeline::Get().GetFrameGenerationDebugTexture(
				render::FrameGenerationDebugResource::kFinal);
		CaptureFrameGenerationDebugSnapshot(
			FrameGenerationDebugView::kFinal,
			source.srv,
			"Frozen final-color snapshot; display resolution.");
	}

	FeatureDebugTexture TemporalRenderer::GetRenderSubrectDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Frozen RT4 snapshot is pending."
		};
		if (_debugView.load(std::memory_order_acquire) != DebugView::kRenderSubrect) {
			return texture;
		}
		if (_superResolutionDebugSnapshot.request.Ready() &&
			_superResolutionDebugSnapshot.capturedView ==
				static_cast<std::uint8_t>(DebugView::kRenderSubrect) &&
			_superResolutionDebugSnapshot.view) {
			texture.texture = _superResolutionDebugSnapshot.view.get();
			texture.width = _superResolutionDebugSnapshot.sourceDesc.Width;
			texture.height = _superResolutionDebugSnapshot.sourceDesc.Height;
			texture.caption = _superResolutionDebugSnapshot.caption;
		}
		return texture;
	}

	FeatureDebugTexture TemporalRenderer::GetProxyDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Frozen RT4 proxy snapshot is pending."
		};
		if (_debugView.load(std::memory_order_acquire) != DebugView::kProxy) {
			return texture;
		}
		if (_superResolutionDebugSnapshot.request.Ready() &&
			_superResolutionDebugSnapshot.capturedView ==
				static_cast<std::uint8_t>(DebugView::kProxy) &&
			_superResolutionDebugSnapshot.view) {
			texture.texture = _superResolutionDebugSnapshot.view.get();
			texture.width = _superResolutionDebugSnapshot.sourceDesc.Width;
			texture.height = _superResolutionDebugSnapshot.sourceDesc.Height;
			texture.caption = _superResolutionDebugSnapshot.caption;
		}
		return texture;
	}

	FeatureDebugTexture TemporalRenderer::GetMotionVectorsDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Frozen motion-vector snapshot is pending."
		};
		if (_debugView.load(std::memory_order_acquire) != DebugView::kMotionVectors) {
			return texture;
		}
		if (_superResolutionDebugSnapshot.request.Ready() &&
			_superResolutionDebugSnapshot.capturedView ==
				static_cast<std::uint8_t>(DebugView::kMotionVectors) &&
			_superResolutionDebugSnapshot.view) {
			texture.texture = _superResolutionDebugSnapshot.view.get();
			texture.width = _superResolutionDebugSnapshot.sourceDesc.Width;
			texture.height = _superResolutionDebugSnapshot.sourceDesc.Height;
			texture.caption = _superResolutionDebugSnapshot.caption;
		}
		return texture;
	}

	FeatureDebugTexture TemporalRenderer::GetProviderOutputDebugTexture() const
	{
		const auto failure =
			_providerOutputDebugFailure.load(std::memory_order_acquire);
		FeatureDebugTexture texture{
			.unavailableText = ProviderOutputDebugUnavailableText(failure)
		};
		if (_debugView.load(std::memory_order_acquire) != DebugView::kProviderOutput) {
			return texture;
		}
		if (!_superResolutionDebugSnapshot.request.Ready() ||
			_superResolutionDebugSnapshot.capturedView !=
				static_cast<std::uint8_t>(DebugView::kProviderOutput) ||
			!_superResolutionDebugSnapshot.view) {
			return texture;
		}

		texture.texture = _superResolutionDebugSnapshot.view.get();
		texture.width = _superResolutionDebugSnapshot.sourceDesc.Width;
		texture.height = _superResolutionDebugSnapshot.sourceDesc.Height;
		texture.caption = _superResolutionDebugSnapshot.caption;
		return texture;
	}

	std::string_view TemporalRenderer::ProviderOutputDebugFailureName(
		ProviderOutputDebugFailure a_failure) noexcept
	{
		switch (a_failure) {
		case ProviderOutputDebugFailure::kNone:
			return "none";
		case ProviderOutputDebugFailure::kNotInitialized:
			return "not_initialized";
		case ProviderOutputDebugFailure::kNoDevice:
			return "no_device";
		case ProviderOutputDebugFailure::kNoRTV:
			return "no_rt0_rtv";
		case ProviderOutputDebugFailure::kNoTexture:
			return "no_rt0_texture";
		case ProviderOutputDebugFailure::kUnsupportedFormat:
			return "unsupported_rt0_format";
		case ProviderOutputDebugFailure::kTextureCreationFailed:
			return "preview_texture_creation_failed";
		case ProviderOutputDebugFailure::kSRVCreationFailed:
			return "preview_srv_creation_failed";
		}
		return "unknown";
	}

	std::string_view TemporalRenderer::ProviderOutputDebugUnavailableText(
		ProviderOutputDebugFailure a_failure) noexcept
	{
		switch (a_failure) {
		case ProviderOutputDebugFailure::kNoDevice:
			return "RT0 provider output preview is unavailable: no D3D11 device.";
		case ProviderOutputDebugFailure::kNoRTV:
			return "RT0 provider output preview is unavailable: RT0 has no RTV.";
		case ProviderOutputDebugFailure::kNoTexture:
			return "RT0 provider output preview is unavailable: the RT0 RTV has no texture.";
		case ProviderOutputDebugFailure::kUnsupportedFormat:
			return "RT0 provider output preview is unavailable: RT0 has no safe SRV format.";
		case ProviderOutputDebugFailure::kTextureCreationFailed:
			return "RT0 provider output preview is unavailable: preview texture creation failed.";
		case ProviderOutputDebugFailure::kSRVCreationFailed:
			return "RT0 provider output preview is unavailable: preview SRV creation failed.";
		case ProviderOutputDebugFailure::kNone:
		case ProviderOutputDebugFailure::kNotInitialized:
			return "Frozen RT0 provider-output snapshot is pending.";
		}
		return "RT0 provider output preview is unavailable for an unknown reason.";
	}

	void TemporalRenderer::SetProviderOutputDebugFailure(
		ProviderOutputDebugFailure a_failure,
		DXGI_FORMAT a_sourceFormat,
		DXGI_FORMAT a_viewFormat,
		HRESULT a_result)
	{
		const bool preserveSnapshot =
			_superResolutionDebugSnapshot.request.Ready() &&
			_superResolutionDebugSnapshot.capturedView ==
				static_cast<std::uint8_t>(DebugView::kProviderOutput) &&
			_superResolutionDebugSnapshot.view;
		if (!preserveSnapshot) {
			_providerOutputDebugAllocated.store(false, std::memory_order_release);
			_providerOutputDebugWidth.store(0, std::memory_order_relaxed);
			_providerOutputDebugHeight.store(0, std::memory_order_relaxed);
		}
		_providerOutputDebugFailure.store(a_failure, std::memory_order_release);

		switch (a_failure) {
		case ProviderOutputDebugFailure::kNoDevice:
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"RT0 provider output preview allocation failed: no D3D11 device");
			break;
		case ProviderOutputDebugFailure::kNoRTV:
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"RT0 provider output preview allocation failed: RT0 has no RTV");
			break;
		case ProviderOutputDebugFailure::kNoTexture:
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"RT0 provider output preview allocation failed: the RT0 RTV has no texture resource");
			break;
		case ProviderOutputDebugFailure::kUnsupportedFormat:
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"RT0 provider output preview allocation failed: source format {} has no supported "
				"typed SRV format (candidate {})",
				static_cast<std::uint32_t>(a_sourceFormat),
				static_cast<std::uint32_t>(a_viewFormat));
			break;
		case ProviderOutputDebugFailure::kTextureCreationFailed:
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"RT0 provider output preview texture creation failed for source format {}, "
				"view format {} (HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(a_sourceFormat),
				static_cast<std::uint32_t>(a_viewFormat),
				static_cast<std::uint32_t>(a_result));
			break;
		case ProviderOutputDebugFailure::kSRVCreationFailed:
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"RT0 provider output preview SRV creation failed for source format {}, "
				"view format {} (HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(a_sourceFormat),
				static_cast<std::uint32_t>(a_viewFormat),
				static_cast<std::uint32_t>(a_result));
			break;
		case ProviderOutputDebugFailure::kNone:
		case ProviderOutputDebugFailure::kNotInitialized:
			break;
		}
	}

	void TemporalRenderer::CaptureSelectedDebugSnapshot()
	{
		const auto request = _superResolutionDebugSnapshot.request.Pending();
		if (!request)
			return;

		const auto selected = _debugView.load(std::memory_order_acquire);
		if (selected == DebugView::kRenderSubrect) {
			auto* view = cs::engine::GetRenderTargetSRV(kSceneColorTarget);
			D3D11_TEXTURE2D_DESC desc{};
			if (!TryDescribeTextureView(view, desc))
				return;
			const auto activeExtent =
				GetActiveExtent(desc.Width, desc.Height);
			CaptureDebugSnapshot(
				selected,
				view,
				std::format(
					"Frozen RT4 snapshot {}x{}; captured active {}x{} "
					"({:.1f}% x {:.1f}%), top-left; raw HDR, outside is undefined",
					desc.Width,
					desc.Height,
					activeExtent.width,
					activeExtent.height,
					static_cast<double>(activeExtent.widthRatio) * 100.0,
					static_cast<double>(activeExtent.heightRatio) * 100.0));
			return;
		}
		if (selected == DebugView::kProxy) {
			const auto proxy = dynamicResolution.GetProxyTexture(
				static_cast<int>(kSceneColorTarget));
			if (!proxy.view)
				return;
			const auto* state = cs::engine::GetGraphicsState();
			const auto activeExtent = GetActiveExtent(
				state ? state->screenWidth : 0,
				state ? state->screenHeight : 0);
			CaptureDebugSnapshot(
				selected,
				proxy.view,
				std::format(
					"Frozen RT4 proxy snapshot {}x{}; captured expected {}x{}; "
					"raw HDR, no display transform",
					proxy.width,
					proxy.height,
					activeExtent.width,
					activeExtent.height));
			return;
		}
		if (selected == DebugView::kMotionVectors) {
			const auto method = GetUpscaleMethod();
			ID3D11ShaderResourceView* view = nullptr;
			std::string_view source;
			if (method == UpscaleMethod::kDLSS &&
				motionVectorCopyTexture) {
				view = motionVectorCopyTexture->srv.get();
				source = "DLSS conditioned copy";
			} else if (IsExternalUpscaler(method)) {
				view =
					cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
				source = "FSR RT29";
			}
			D3D11_TEXTURE2D_DESC desc{};
			if (!TryDescribeTextureView(view, desc))
				return;
			const auto activeExtent =
				GetActiveExtent(desc.Width, desc.Height);
			CaptureDebugSnapshot(
				selected,
				view,
				std::format(
					"Frozen {} snapshot {}x{}; captured active {}x{} top-left; "
					"raw signed RG, negative components clip",
					source,
					desc.Width,
					desc.Height,
					activeExtent.width,
					activeExtent.height));
			return;
		}
		if (selected != DebugView::kProviderOutput ||
			!_srPublishedToFramebuffer.load(std::memory_order_acquire)) {
			return;
		}

		auto* device = cs::engine::GetDevice();
		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!device) {
			SetProviderOutputDebugFailure(ProviderOutputDebugFailure::kNoDevice);
			return;
		}
		FrameBufferTextureFailure textureFailure =
			FrameBufferTextureFailure::kNone;
		if (!TryGetFrameBufferTexture(
				frameBuffer, frameBufferDesc, &textureFailure)) {
			SetProviderOutputDebugFailure(
				textureFailure == FrameBufferTextureFailure::kNoRTV
					? ProviderOutputDebugFailure::kNoRTV
					: ProviderOutputDebugFailure::kNoTexture);
			return;
		}

		const auto viewFormat =
			GetProviderOutputPreviewViewFormat(frameBufferDesc.Format);
		UINT formatSupport = 0;
		if (viewFormat == DXGI_FORMAT_UNKNOWN ||
			FAILED(device->CheckFormatSupport(viewFormat, &formatSupport)) ||
			(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0 ||
			(formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) == 0) {
			SetProviderOutputDebugFailure(
				ProviderOutputDebugFailure::kUnsupportedFormat,
				frameBufferDesc.Format,
				viewFormat);
			return;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		viewDesc.Format = viewFormat;
		if (frameBufferDesc.SampleDesc.Count > 1) {
			if (frameBufferDesc.ArraySize == 1) {
				viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
			} else {
				viewDesc.ViewDimension =
					D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
				viewDesc.Texture2DMSArray.ArraySize =
					frameBufferDesc.ArraySize;
			}
		} else if (frameBufferDesc.ArraySize == 1) {
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MipLevels = frameBufferDesc.MipLevels;
		} else {
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			viewDesc.Texture2DArray.MipLevels = frameBufferDesc.MipLevels;
			viewDesc.Texture2DArray.ArraySize = frameBufferDesc.ArraySize;
		}

		const auto caption = std::format(
			"Frozen RT0 snapshot {}x{}; captured after DrawWorld::Render_UI +0xC5 "
			"provider resolve; runtime method {}",
			frameBufferDesc.Width,
			frameBufferDesc.Height,
			UpscaleMethodName(GetUpscaleMethod()));
		if (!CaptureFrozenTextureSnapshot(
				_superResolutionDebugSnapshot,
				static_cast<std::uint8_t>(DebugView::kProviderOutput),
				frameBuffer.get(),
				viewDesc,
				caption,
				"Upscaling/ProviderOutputDebugCopy.Texture",
				"Upscaling/ProviderOutputDebugCopy.SRV",
				std::string_view{})) {
			switch (_superResolutionDebugSnapshot.failure) {
			case FrozenTextureSnapshotFailure::kNoDevice:
			case FrozenTextureSnapshotFailure::kNoContext:
				SetProviderOutputDebugFailure(
					ProviderOutputDebugFailure::kNoDevice);
				break;
			case FrozenTextureSnapshotFailure::kTextureCreationFailed:
				SetProviderOutputDebugFailure(
					ProviderOutputDebugFailure::kTextureCreationFailed,
					frameBufferDesc.Format,
					viewFormat,
					_superResolutionDebugSnapshot.result);
				break;
			case FrozenTextureSnapshotFailure::kViewCreationFailed:
				SetProviderOutputDebugFailure(
					ProviderOutputDebugFailure::kSRVCreationFailed,
					frameBufferDesc.Format,
					viewFormat,
					_superResolutionDebugSnapshot.result);
				break;
			case FrozenTextureSnapshotFailure::kNone:
			case FrozenTextureSnapshotFailure::kNoTexture:
				break;
			}
			return;
		}
		_providerOutputDebugWidth.store(
			frameBufferDesc.Width, std::memory_order_relaxed);
		_providerOutputDebugHeight.store(
			frameBufferDesc.Height, std::memory_order_relaxed);
		_providerOutputDebugFailure.store(
			ProviderOutputDebugFailure::kNone,
			std::memory_order_release);
		_providerOutputDebugAllocated.store(true, std::memory_order_release);
	}

	void TemporalRenderer::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto topology = render::TemporalPipeline::Get().GetStatus();
		const auto method = GetUpscaleMethod();
		const auto [renderWidth, renderHeight] = GetRenderSize();
		const auto* state = cs::engine::GetGraphicsState();
		const auto* renderTargetManager = cs::engine::GetRenderTargetManager();
		const std::uint32_t fullWidth = state ? state->screenWidth : 0;
		const std::uint32_t fullHeight = state ? state->screenHeight : 0;
		const auto activeExtent = GetActiveExtent(fullWidth, fullHeight);
		a_sink
			.Field("enabled", settings.enabled)
			.Field(
				"requested_method",
				static_cast<std::int64_t>(
					static_cast<std::uint8_t>(topology.requested.superResolution)))
			.Field(
				"effective_method",
				static_cast<std::int64_t>(
					static_cast<std::uint8_t>(topology.effective.superResolution)))
			.Field("pending_restart", topology.pending.required)
			.Field("topology_failure", topology.failure)
			.Field(
				"reset_epoch_requested",
				static_cast<std::int64_t>(
					topology.superResolutionResetRequested))
			.Field(
				"reset_epoch_consumed",
				static_cast<std::int64_t>(
					topology.superResolutionResetConsumed))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("hooks_installed", _hooksInstalled.load(std::memory_order_acquire))
			.Field("method", static_cast<std::int64_t>(static_cast<int>(method)))
			.Field("method_name", UpscaleMethodName(method))
			.Field("quality_mode", static_cast<std::int64_t>(settings.qualityMode))
			.Field("quality_mode_name", QualityModeName(settings.qualityMode))
			.Field(
				"dlss_available",
				render::TemporalPipeline::Get()
					.IsSuperResolutionRuntimeReady(
						render::temporal::SuperResolutionMethod::kDLSS))
			.Field("upscaling_active", IsUpscalingActive())
			.Field("dynamic_resolution_available", renderTargetManager != nullptr)
			.Field("proxies_ready", dynamicResolution.HasProxies())
			.Field("full_width", static_cast<std::int64_t>(fullWidth))
			.Field("full_height", static_cast<std::int64_t>(fullHeight))
			.Field("render_width", static_cast<std::int64_t>(renderWidth))
			.Field("render_height", static_cast<std::int64_t>(renderHeight))
			.Field("active_width", static_cast<std::int64_t>(activeExtent.width))
			.Field("active_height", static_cast<std::int64_t>(activeExtent.height))
			.Field(
				"input_color_format",
				static_cast<std::int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM))
			.Field("input_transfer", std::string_view{ "gamma_2_2" })
			.Field("input_color_stage", std::string_view{ "post_tonemap_lut" })
			.Field("input_lut_baked", true)
			.Field("dynamic_width_ratio", static_cast<double>(activeExtent.widthRatio))
			.Field("dynamic_height_ratio", static_cast<double>(activeExtent.heightRatio))
			.Field("scale_published", _resolutionScalePublished)
			.Field("provider_failures", static_cast<std::int64_t>(_providerFailures.load(std::memory_order_relaxed)))
			.Field("spatial_fallbacks", static_cast<std::int64_t>(_spatialFallbacks.load(std::memory_order_relaxed)))
			.Field("spatial_fallback_preflight_ready", _spatialFallbackPreflightReady.load(std::memory_order_acquire))
			.Field("spatial_fallback_this_frame", _spatialFallbackThisFrame.load(std::memory_order_acquire))
			.Field("missed_resolve_frames", static_cast<std::int64_t>(_missedResolveFrames.load(std::memory_order_relaxed)))
			.Field("gamma_only_recovery_frames", static_cast<std::int64_t>(_gammaOnlyRecoveryFrames.load(std::memory_order_relaxed)))
			.Field("render_ui_gate_valid", _renderUiPathGate.has_value())
			.Field("render_ui_full_effects_path", _renderUiFullEffectsPath.load(std::memory_order_acquire))
			.Field("render_ui_recovery_source_width", static_cast<std::int64_t>(_renderUiRecoverySourceWidth.load(std::memory_order_relaxed)))
			.Field("render_ui_recovery_source_height", static_cast<std::int64_t>(_renderUiRecoverySourceHeight.load(std::memory_order_relaxed)))
			.Field("render_ui_recovery_passthrough", _renderUiRecoveryPassthrough.load(std::memory_order_acquire))
			.Field("resolve_seam_seen", _resolveSeamSeen.load(std::memory_order_acquire))
			.Field("native_fallback_pending", _nativeSuperResolutionFallbackPending.load(std::memory_order_acquire))
			.Field("sr_published_to_framebuffer", _srPublishedToFramebuffer.load(std::memory_order_acquire))
			.Field("provider_preview_allocated", _providerOutputDebugAllocated.load(std::memory_order_acquire))
			.Field("provider_preview_width", static_cast<std::int64_t>(_providerOutputDebugWidth.load(std::memory_order_relaxed)))
			.Field("provider_preview_height", static_cast<std::int64_t>(_providerOutputDebugHeight.load(std::memory_order_relaxed)))
			.Field(
				"debug_snapshot_revision",
				static_cast<std::int64_t>(
					_superResolutionDebugSnapshot.request.Revision()))
			.Field(
				"debug_snapshot_refresh_pending",
				_superResolutionDebugSnapshot.request.Pending() != 0)
			.Field(
				"provider_preview_failure",
				ProviderOutputDebugFailureName(
					_providerOutputDebugFailure.load(std::memory_order_acquire)))
			.Field("resolution_scale_x", static_cast<double>(resolutionScale.x))
			.Field("resolution_scale_y", static_cast<double>(resolutionScale.y))
			.Field("mip_bias", static_cast<double>(_mipBias.load(std::memory_order_relaxed)))
			.Field("dispatches", static_cast<std::int64_t>(_upscaleDispatches.load(std::memory_order_relaxed)));
	}
}
