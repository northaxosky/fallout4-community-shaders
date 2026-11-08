#include "HDRCapture.h"

#include "Log.h"
#include "Render/Engine.h"
#include "Render/InterfaceLayer.h"
#include "Render/TemporalPipeline.h"

namespace cs::features::hdr_display
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.hdrdisplay.capture");

		constexpr auto kSdrFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		constexpr auto kHdrFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
		constexpr UINT kBind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
	}

	bool CaptureComposer::EnsureReady()
	{
		if (_compose.IsReady())
			return true;
		auto* device = engine::GetDevice();
		// A failed compile is not retried on every capture.
		if (_failed || !device)
			return false;
		_failed = !_compose.Initialize(*device);
		return !_failed;
	}

	std::unique_ptr<buffer::Texture2D>& CaptureComposer::Target(bool a_hdr, UINT a_width, UINT a_height)
	{
		auto& target = a_hdr ? _hdr : _sdr;
		if (!target || target->desc.Width != a_width || target->desc.Height != a_height) {
			target = buffer::CreateRenderSizeTexture(a_width, a_height, a_hdr ? kHdrFormat : kSdrFormat, kBind,
				a_hdr ? "HDRDisplay/CaptureHdr" : "HDRDisplay/CaptureSdr");
		}
		return target;
	}

	const buffer::Texture2D* CaptureComposer::Compose(const render::hdr::OutputSettings& a_settings, bool a_interface)
	{
		// While the frame is split the frame buffer holds only the interface.
		if (!render::InterfaceLayer::Get().IsDrawingInterface() || !EnsureReady())
			return nullptr;
		const auto scene = render::TemporalPipeline::Get().GetHdrSceneTarget();
		auto* interfaceSrv = engine::GetRenderTargetSRV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		auto* context = engine::GetImmediateContext();
		if (!scene || !context || (a_interface && !interfaceSrv))
			return nullptr;
		D3D11_TEXTURE2D_DESC sceneDesc{};
		scene.texture->GetDesc(&sceneDesc);
		try {
			auto& target = Target(a_settings.enabled, sceneDesc.Width, sceneDesc.Height);
			_compose.Dispatch(*context, *scene.srv, a_interface ? interfaceSrv : nullptr, *target->uav,
				sceneDesc.Width, sceneDesc.Height, a_settings);
			return target.get();
		} catch (const std::exception& error) {
			L->error("Capture target creation failed: {}", error.what());
			_failed = true;
			return nullptr;
		}
	}
}
