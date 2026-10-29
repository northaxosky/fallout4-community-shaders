#include "RemoteControl.h"
#include "RemoteControlTools.h"

#include "RenderDoc.h"

#include "Render/FrameProfiler.h"
#include "Utils/PathUtf8.h"

#include <algorithm>
#include <string_view>

namespace cs::features::remote_control
{
	namespace
	{
		struct Device
		{
			std::string_view name;
			RenderDoc::CaptureTarget target;
		};

		constexpr Device kDevices[]{
			{ "d3d11", RenderDoc::CaptureTarget::kEngineD3D11 },
			{ "d3d12", RenderDoc::CaptureTarget::kTemporalD3D12 }
		};

		std::string_view DeviceName(RenderDoc::CaptureTarget a_target)
		{
			return std::ranges::find(kDevices, a_target, &Device::target)->name;
		}

		Json RequestRenderDoc(const Device& a_device, std::uint32_t a_frames)
		{
			auto* renderDoc = RenderDoc::GetSingleton();
			if (!renderDoc->IsLoaded() || !renderDoc->IsHealthy()) {
				return Error("RenderDoc feature is not loaded",
					{ { "hint", "communityshaders.feature(action='list') shows RenderDoc.loaded" } });
			}
			const auto frame = render::profiling::FrameSequence();
			const auto request = renderDoc->RequestCapture({ a_device.target, a_frames });
			if (!request)
				return Error(request.error().message, { { "hint", request.error().hint } });
			return {
				{ "queued", true },
				{ "requestId", *request },
				{ "kind", "renderdoc" },
				{ "device", a_device.name },
				{ "frames", a_frames },
				{ "enqueued_at_frame", frame }
			};
		}

		Json Handle(const Json& a_args)
		{
			const auto kind = a_args.value("kind", std::string{});
			if (kind.empty())
				return Error("missing required parameter 'kind'");
			if (kind == "screenshot")
				return Error("screenshot feature not available", { { "hint", "use devbench capture" } });
			if (kind != "renderdoc")
				return Error("unknown kind", { { "kind", kind }, { "supported", Json::array({ "renderdoc", "screenshot" }) } });

			const auto deviceName = a_args.value("device", std::string("d3d11"));
			const auto* device = std::ranges::find(kDevices, deviceName, &Device::name);
			if (device == std::end(kDevices))
				return Error("unknown device", { { "device", deviceName }, { "supported", Json::array({ "d3d11", "d3d12" }) } });
			std::uint32_t frames = 1;
			if (a_args.contains("frames") && a_args["frames"].is_number())
				frames = static_cast<std::uint32_t>(std::clamp(a_args["frames"].get<int>(), 1, 120));
			// The request touches UI-owned buffers, so it is made at the frame boundary.
			return OnFrame([device, frames] { return RequestRenderDoc(*device, frames); });
		}
	}

	void SubscribeCaptureEvents()
	{
		RenderDoc::GetSingleton()->AddCaptureObserver([](const RenderDoc::CaptureCompletion& a_done) {
			Json payload{ { "requestId", a_done.id }, { "device", DeviceName(a_done.target) } };
			if (a_done.error.empty()) {
				auto captures = Json::array();
				for (const auto& file : a_done.captures) {
					captures.push_back({ { "path", util::PathToUtf8(file.path) }, { "bytes", file.bytes } });
				}
				payload["frames"] = a_done.frames;
				payload["captures"] = std::move(captures);
			} else {
				payload["error"] = a_done.error;
			}
			RemoteControl::GetSingleton()->EmitEvent("communityshaders.capture.completed", payload);
		});
	}

	const Tool kCaptureTool{
		"communityshaders.capture",
		R"({"description":"Trigger a frame capture. Kind-dispatched. kind=renderdoc: a RenderDoc capture of 'frames' frames (1-120, default 1), one .rdc per frame, bracketed on the chosen 'device' (d3d11 default: the engine device; d3d12: the temporal pipeline device, available only while the D3D12 proxy is active, i.e. with Upscaling or Frame Generation outside exclusive fullscreen). Returns immediately with {queued,requestId,kind,device,frames,enqueued_at_frame}; when RenderDoc has finished the file the event communityshaders.capture.completed carries {requestId,device,frames,captures:[{path,bytes}]} with one entry per frame, or {requestId,device,error} on failure. One capture runs at a time. kind=screenshot is not available in Fallout 4; use the devbench capture tool.","inputSchema":{"type":"object","properties":{"kind":{"type":"string","enum":["renderdoc","screenshot"]},"frames":{"type":"number"},"device":{"type":"string","enum":["d3d11","d3d12"]}},"required":["kind"]}})",
		&Handle
	};
}
