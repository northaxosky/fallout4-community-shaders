#pragma once

#include <array>
#include <memory>
#include <string_view>

#include "Render/FrameGenerationOrchestration.h"
#include "Render/IPresenter.h"
#include "Render/PresentationCallbacks.h"

namespace cs::render
{
	// Presents the facade's frames through a vendor-owned D3D12 swap chain on the
	// bridge's device, which is what frame generation needs.
	class D3D12FrameGenPresenter final : public IPresenter
	{
	public:
		~D3D12FrameGenPresenter() override;

		// The provider's presentation hooks stay armed only while this lives.
		[[nodiscard]] static temporal::ProviderResult Create(InteropBridge& a_bridge,
			temporal::IFrameGenerationProvider& a_provider,
			const TemporalPresentationCallbacks& a_callbacks, const ChainRequest& a_request,
			bool a_hdrContainer, std::unique_ptr<D3D12FrameGenPresenter>& a_presenter) noexcept;

		[[nodiscard]] IDXGISwapChain4* Chain() const noexcept override { return _chain.chain.get(); }
		[[nodiscard]] const DXGI_SWAP_CHAIN_DESC1& Desc() const noexcept override { return _chain.desc; }
		[[nodiscard]] bool IsReady() const noexcept override;
		[[nodiscard]] bool NeedsBridgeTextures() const noexcept override { return true; }
		[[nodiscard]] bool FramePending() const noexcept override { return _presentPrepared; }
		[[nodiscard]] HRESULT PresentChain(const PresentArgs& a_args) noexcept override { return _chain.Present(a_args); }
		[[nodiscard]] HRESULT Present(const PresentFrame& a_frame, const PresentArgs& a_args) noexcept override;
		[[nodiscard]] ResizeOutcome Resize(UINT a_width, UINT a_height, UINT a_flags) noexcept override;
		void ApplyHdrColorSpace(const hdr::OutputSettings& a_settings) noexcept override { _chain.ApplyHdrColorSpace(a_settings); }
		void ClearInputs(ID3D11DeviceContext& a_context) noexcept override;
		[[nodiscard]] HRESULT Release() noexcept override;

		[[nodiscard]] temporal::IFrameGenerationProvider& Provider() const noexcept { return _provider; }
		[[nodiscard]] bool IsFrameGenerationReady() const noexcept;
		[[nodiscard]] UINT GetFrameSlot() const noexcept { return _frameSlot; }
		[[nodiscard]] SharedD3D11D3D12Texture* GetHudlessTexture() const noexcept { return Slot(_buffers.hudless); }
		[[nodiscard]] SharedD3D11D3D12Texture* GetDepthTexture() const noexcept { return Slot(_buffers.depth); }
		[[nodiscard]] SharedD3D11D3D12Texture* GetMotionTexture() const noexcept { return Slot(_buffers.motion); }
		[[nodiscard]] temporal::PresentInputRetirementDiagnostics
		GetInputRetirementDiagnostics() const noexcept { return _retirementDiagnostics.Snapshot(); }
		// A DXGI_PRESENT_TEST attempt belongs to the frame the transaction prepared.
		void RecordTestPresent(UINT a_flags, HRESULT a_result) noexcept;
		void SetFrameGenerationInputsReady(bool a_ready) noexcept { _frameGenerationInputsReady = a_ready; }
		[[nodiscard]] bool AcquireFrameGenerationInputWrite() noexcept;
		void DisableFrameGeneration(const char* a_reason) noexcept;
		void DisableFrameGeneration(std::string_view a_operation,
			const temporal::ProviderResult& a_result) noexcept;

	private:
		using SlotTextures = std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2>;
		struct FrameGenerationBuffers
		{
			SlotTextures hudless;
			// HDR container only: the encoded interface layer frame generation recomposes.
			SlotTextures ui;
			SlotTextures depth;
			SlotTextures motion;
		};

		D3D12FrameGenPresenter(InteropBridge& a_bridge, temporal::IFrameGenerationProvider& a_provider,
			const TemporalPresentationCallbacks& a_callbacks, bool a_hdrContainer) noexcept;

		[[nodiscard]] SharedD3D11D3D12Texture* Slot(const SlotTextures& a_textures) const noexcept
		{
			return _frameSlot < a_textures.size() ? a_textures[_frameSlot].get() : nullptr;
		}
		[[nodiscard]] HRESULT CreateChain(const ChainRequest& a_request, temporal::ProviderResult& a_failure);
		[[nodiscard]] SlotTextures CreateSlotBuffers(const D3D11_TEXTURE2D_DESC& a_desc, std::string_view a_name) const;
		[[nodiscard]] HRESULT CreateBuffers(UINT a_width, UINT a_height, FrameGenerationBuffers& a_buffers) const;
		// Takes the new buffers and has the provider size its display resources to them.
		[[nodiscard]] HRESULT CommitBuffers(FrameGenerationBuffers&& a_buffers, UINT a_width, UINT a_height);
		[[nodiscard]] HRESULT RestoreProvider(UINT a_width, UINT a_height);
		[[nodiscard]] temporal::ColorContract PresentContract() const;
		[[nodiscard]] HRESULT RefreshBackBuffers() noexcept;
		void ResetSubmissions() noexcept;
		void ReleaseResources() noexcept;
		[[nodiscard]] HRESULT PresentImpl(const PresentFrame& a_frame, const PresentArgs& a_args);
		// Writes this slot's inputs, returns the encoded back buffer; null skips.
		[[nodiscard]] const SharedD3D11D3D12Texture* ComposeHdrFrameGenerationInputs(const PresentFrame& a_frame) noexcept;
		void RecordGlobalDrain(std::string_view a_reason, const temporal::ProviderResult& a_result) noexcept;

		InteropBridge& _bridge;
		temporal::IFrameGenerationProvider& _provider;
		const TemporalPresentationCallbacks& _callbacks;
		PresenterChain _chain;
		winrt::com_ptr<ID3D12Resource> _backBuffers[2];
		SubmissionRing _presentSubmissions;
		FrameGenerationBuffers _buffers;
		temporal::PresentInputReuseGate _inputReuseGate;
		temporal::AtomicPresentInputRetirementDiagnostics _retirementDiagnostics;
		std::array<std::uint64_t, 2> _allocatorFenceValues{};
		std::uint64_t _preparedRealFrame = 0;
		std::uint64_t _inputResourceGeneration = 0;
		UINT _frameIndex = 0;
		UINT _frameSlot = 0;
		UINT _nextPresentSubmission = 0;
		bool _hdrContainer = false;
		bool _faulted = false;
		bool _frameGenerationInputsReady = false;
		bool _frameGenerationDisabled = false;
		bool _providerGenerationEnabled = false;
		bool _providerPresentationActive = false;
		bool _presentPrepared = false;
		bool _presentSubmissionMayBeInFlight = false;
		bool _preparedFrameGeneration = false;
		bool _vendorConsumptionPossible = false;
		bool _preparedTransaction = false;
	};
}  // namespace cs::render
