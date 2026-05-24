#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss_g.h>
#include <sl_matrix_helpers.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <sl_version.h>
#pragma warning(pop)

namespace cs::features::framegeneration
{

// DLSS-G / Reflex / PCL dispatch helper. SDK plumbing lives in cs::Streamline.
class StreamlineFG
{
public:
	static StreamlineFG* GetSingleton()
	{
		static StreamlineFG singleton;
		return &singleton;
	}

	// Binds the D3D12 proxy device for DLSS-G dispatch.
	void SetD3DDevice(ID3D12Device* a_device);

	bool CheckAndEnableDLSSG();
	void SetEnabled(bool a_enabled);

	// Re-applies the default ReflexOptions (eLowLatency). Called from CheckAndEnableDLSSG
	// at init, and again from DXGISwapChainProxy::ResizeBuffers/ResizeBuffers1 after the
	// D3D12 swap chain is rebuilt - Reflex state can be invalidated by the underlying
	// swap rebuild on resolution / HDR / windowed-fullscreen changes.
	void ReapplyReflexOptions();

	// Approach B post-FG FPS source. Returns DLSSGState::numFramesActuallyPresented (which
	// is "since the last slDLSSGGetState call" per sl_dlss_g.h). Returns 0 when DLSS-G is
	// not enabled for this session or telemetry is unavailable. Call exactly once per
	// engine tick to avoid losing counts.
	uint32_t ConsumeFramesPresented();

	void AcquireFrameToken();
	void SetPCLMarker(sl::PCLMarker marker);

	struct CameraData
	{
		const __m128* viewMat;
		const __m128* viewProjUnjittered;
		const __m128* currentViewProjUnjittered;
		const __m128* previousViewProjUnjittered;
		const __m128* viewUp;
		const __m128* viewRight;
		const __m128* viewDir;
		float posX, posY, posZ;
	};

	void Present(
		ID3D12GraphicsCommandList* a_cmdList,
		ID3D12Resource* a_depth,
		ID3D12Resource* a_motionVectors,
		ID3D12Resource* a_hudlessColor,
		ID3D12Resource* a_uiColorAlpha,
		ID3D12Resource* a_uiAlpha,
		float2 a_screenSize,
		float2 a_jitter,
		float a_cameraNear, float a_cameraFar,
		const CameraData& a_camera);

	// Active for this session, distinct from cs::Streamline::featureDLSSG (loaded + supported).
	bool sessionActive = false;
	uint32_t configuredFrameCount = 1;
	ID3D12Device* d3d12Device = nullptr;
	sl::ViewportHandle viewport{ 0 };
	sl::FrameToken*    frameToken{};

	PFun_slDLSSGSetOptions* slDLSSGSetOptions{};
	PFun_slDLSSGGetState*   slDLSSGGetState{};

	PFun_slReflexSetOptions* slReflexSetOptions{};
	PFun_slReflexSleep*      slReflexSleep{};

	using PFun_slReflexSetMarker = sl::Result(sl::PCLMarker marker, const sl::FrameToken& frame);
	PFun_slReflexSetMarker* slReflexSetMarker{};
};

}
