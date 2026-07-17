#include "Streamline.h"
#include "FrameGeneration.h"

#include "Log.h"
#include "Render/StreamlineCore.h"

namespace cs::features::framegeneration
{
	namespace { auto* L = cs::log::Get("cs.feature.framegeneration.dlssg"); }

void StreamlineFG::SetD3DDevice(ID3D12Device* a_device)
{
	d3d12Device = a_device;
	auto* core = cs::Streamline::GetSingleton();
	if (core->slSetD3DDevice && core->IsInitialized() && a_device) {
		core->slSetD3DDevice(a_device);
		core->MarkD3DDeviceRegistered();
		L->info("D3D12 device set");
	}
}

bool StreamlineFG::CheckAndEnableDLSSG()
{
	auto* core = cs::Streamline::GetSingleton();
	if (!core->IsInitialized()) return false;

	if (core->slGetFeatureFunction) {
		core->slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions",  (void*&)slDLSSGSetOptions);
		core->slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState",    (void*&)slDLSSGGetState);
		core->slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
		core->slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep",      (void*&)slReflexSleep);
		core->slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetMarker",  (void*&)slReflexSetMarker);
		L->info("Feature functions loaded: SetOptions={:#x}, GetState={:#x}, ReflexMarker={:#x}",
			(uintptr_t)slDLSSGSetOptions, (uintptr_t)slDLSSGGetState, (uintptr_t)slReflexSetMarker);
	}

	if (!slDLSSGSetOptions || !slReflexSetMarker) {
		L->warn("Missing required function pointers");
		return false;
	}

	uint32_t maxFrames = 1;
	if (slDLSSGGetState) {
		sl::DLSSGState state{};
		slDLSSGGetState(viewport, state, nullptr);
		maxFrames = state.numFramesToGenerateMax;
	}

	auto frameGen = FrameGeneration::GetSingleton();
	uint32_t requestedFrames = std::clamp((uint32_t)frameGen->settings.frameGenFrames, 1u, maxFrames);

	configuredFrameCount = requestedFrames;

	sl::DLSSGOptions options{};
	options.mode = sl::DLSSGMode::eOn;
	options.numFramesToGenerate = requestedFrames;
	options.enableUserInterfaceRecomposition = sl::Boolean::eTrue;

	auto result = slDLSSGSetOptions(viewport, options);
	if (result != sl::Result::eOk) {
		L->warn("Failed to enable DLSS-G: {}", (int)result);
		return false;
	}

	sessionActive = true;
	L->info("DLSS-G enabled: {}x frame gen (requested {}, hardware max {})",
		requestedFrames + 1, frameGen->settings.frameGenFrames, maxFrames);

	ReapplyReflexOptions();

	if (slDLSSGGetState) {
		sl::DLSSGState state{};
		slDLSSGGetState(viewport, state, nullptr);
		L->info("Status: {}, minDim: {}, maxFrames: {}",
			(int)state.status, state.minWidthOrHeight, state.numFramesToGenerateMax);
	}

	return true;
}

void StreamlineFG::SetEnabled(bool a_enabled)
{
	if (!slDLSSGSetOptions || !sessionActive) return;

	sl::DLSSGOptions options{};
	options.mode = a_enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
	options.numFramesToGenerate = configuredFrameCount;
	options.enableUserInterfaceRecomposition = sl::Boolean::eTrue;
	slDLSSGSetOptions(viewport, options);
}

uint32_t StreamlineFG::ConsumeFramesPresented()
{
	if (!sessionActive || !slDLSSGGetState)
		return 0;
	sl::DLSSGState state{};
	if (slDLSSGGetState(viewport, state, nullptr) != sl::Result::eOk)
		return 0;
	return state.numFramesActuallyPresented;
}

void StreamlineFG::ReapplyReflexOptions()
{
	if (!slReflexSetOptions || !sessionActive)
		return;
	sl::ReflexOptions reflexOptions{};
	reflexOptions.mode = sl::ReflexMode::eLowLatency;
	if (slReflexSetOptions(reflexOptions) != sl::Result::eOk)
		L->warn("slReflexSetOptions failed during reapply");
}

void StreamlineFG::AcquireFrameToken()
{
	auto* core = cs::Streamline::GetSingleton();
	if (!core->slGetNewFrameToken || !sessionActive) return;

	if (SL_FAILED(res, core->slGetNewFrameToken(frameToken, nullptr))) {
		static bool loggedOnce = false;
		if (!loggedOnce) { L->error("Failed to get frame token"); loggedOnce = true; }
	}
}

void StreamlineFG::SetPCLMarker(sl::PCLMarker marker)
{
	if (!slReflexSetMarker || !frameToken) return;
	slReflexSetMarker(marker, *frameToken);
}

void StreamlineFG::Present(
	ID3D12GraphicsCommandList* a_cmdList,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	ID3D12Resource* a_hudlessColor,
	ID3D12Resource* a_uiColorAlpha,
	ID3D12Resource* a_uiAlpha,
	float2 a_screenSize,
	float2 a_jitter,
	float a_cameraNear, float a_cameraFar,
	const CameraData& a_camera)
{
	if (!sessionActive || !frameToken) return;

	auto* core = cs::Streamline::GetSingleton();

	// DLSS-G constants require unjittered matrices.
	if (core->slSetConstants) {
		sl::Constants constants = cs::engine::BuildSLConstants(
			a_camera, (uint32_t)a_screenSize.x, (uint32_t)a_screenSize.y,
			a_cameraNear, a_cameraFar, a_jitter.x, a_jitter.y, sl::Boolean::eFalse);

		if (SL_FAILED(res, core->slSetConstants(constants, *frameToken, viewport))) {
			static bool loggedOnce = false;
			if (!loggedOnce) { L->error("Failed to set constants"); loggedOnce = true; }
		}
	}

	if (a_depth && a_motionVectors && a_hudlessColor && core->slSetTagForFrame) {
		sl::Resource depth = { sl::ResourceType::eTex2d, a_depth, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, a_motionVectors, 0 };
		sl::Resource hudless = { sl::ResourceType::eTex2d, a_hudlessColor, 0 };

		sl::Extent fullExtent = { 0, 0, (uint32_t)a_screenSize.x, (uint32_t)a_screenSize.y };

		// UIAlpha (single-channel) drives recomposition; DLSS-G prefers it over UIColorAndAlpha when both are tagged.
		sl::Resource uiColorRes = { sl::ResourceType::eTex2d, a_uiColorAlpha, 0 };
		sl::Resource uiAlphaRes = { sl::ResourceType::eTex2d, a_uiAlpha,      0 };

		sl::ResourceTag tags[] = {
			{ &depth,                                sl::kBufferTypeDepth,           sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
			{ &mvec,                                 sl::kBufferTypeMotionVectors,   sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
			{ &hudless,                              sl::kBufferTypeHUDLessColor,    sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
			{ a_uiColorAlpha ? &uiColorRes : nullptr, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, a_uiColorAlpha ? &fullExtent : nullptr },
			{ a_uiAlpha      ? &uiAlphaRes : nullptr, sl::kBufferTypeUIAlpha,         sl::ResourceLifecycle::eValidUntilPresent, a_uiAlpha      ? &fullExtent : nullptr },
		};
		core->slSetTagForFrame(*frameToken, viewport, tags, _countof(tags), (sl::CommandBuffer*)a_cmdList);
	}
}

}
