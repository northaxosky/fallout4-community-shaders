#pragma once

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>
#include <sl_nis.h>
#include <sl_version.h>
#pragma warning(pop)
#include "Buffer.h"

namespace cs::features::Upscaling
{

using PFun_slSetTag2 = sl::Result(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);

/**
 * @class Streamline
 * @brief Manager for NVIDIA Streamline integration and DLSS upscaling
 *
 * Handles initialization of NVIDIA Streamline SDK, feature detection,
 * and execution of DLSS (Deep Learning Super Sampling) upscaling.
 * Uses manual hooking mode to integrate with Fallout 4's rendering pipeline.
 */
class Streamline
{
public:
	// ========================================
	// Singleton & Lifecycle
	// ========================================

	/**
	 * @brief Get the singleton instance
	 * @return Pointer to the global Streamline instance
	 */
	static Streamline* GetSingleton()
	{
		static Streamline singleton;
		return &singleton;
	}

	/**
	 * @brief Destructor - frees Streamline interposer DLL
	 *
	 * Ensures proper cleanup of the loaded interposer library
	 */
	~Streamline()
	{
		if (interposer) {
			FreeLibrary(interposer);
			interposer = nullptr;
		}
	}

	/**
	 * @brief Get short name for logging
	 * @return "Streamline"
	 */
	inline std::string GetShortName() { return "Streamline"; }

	// ========================================
	// Initialization
	// ========================================

	/**
	 * @brief Load Streamline interposer DLL
	 *
	 * Loads sl.interposer.dll from Data/F4SE/Plugins/Upscaling/Streamline/
	 * The interposer provides the Streamline SDK interface.
	 */
	void LoadInterposer();

	/**
	 * @brief Initialize Streamline SDK
	 *
	 * Sets up Streamline preferences, initializes the SDK, and queries for
	 * available features (DLSS). Uses manual hooking mode to integrate with
	 * the game's D3D11 device.
	 */
	void Initialize();

	/**
	 * @brief Check Streamline plugin compat
	 */
	void CheckFeatures(IDXGIAdapter* a_adapte);

	/**
	 * @brief Initialise features after device
	 */
	void PostDevice();

	/**
	 * @brief Create D3D11 device and swap chain with Streamline integration
	 * @param pAdapter GPU adapter to use
	 * @param DriverType Driver type
	 * @param Software Software rasterizer module (if applicable)
	 * @param Flags Device creation flags
	 * @param pFeatureLevels Array of feature levels to try
	 * @param FeatureLevels Number of feature levels
	 * @param SDKVersion D3D11 SDK version
	 * @param pSwapChainDesc Swap chain description
	 * @param ppSwapChain Output swap chain pointer
	 * @param ppDevice Output device pointer
	 * @param pFeatureLevel Output feature level
	 * @param ppImmediateContext Output device context pointer
	 * @return HRESULT indicating success or failure
	 *
	 * Wraps D3D11CreateDeviceAndSwapChain to inject Streamline initialization
	 * and feature detection. Checks for DLSS availability on the current GPU.
	 */
	HRESULT CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext);

	// ========================================
	// DLSS Operations
	// ========================================

	/**
	 * @brief Execute DLSS upscaling
	 * @param a_color Input/output color texture at render resolution
	 * @param a_dilatedMotionVectorTexture Dilated motion vectors for better temporal stability
	 * @param a_jitter Camera jitter offset for current frame
	 * @param a_renderSize Render resolution dimensions
	 * @param a_qualityMode DLSS quality mode (0=DLAA, 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance)
	 *
	 * Performs DLSS upscaling from render resolution to display resolution.
	 * Uses dilated motion vectors and depth buffer for temporal reconstruction.
	 * The upscaled result is written back to a_color texture.
	 */
	void Upscale(Texture2D* a_color, Texture2D* a_dilatedMotionVectorTexture, float2 a_jitter, float2 a_renderSize, uint a_qualityMode);

	/**
	 * @brief Update Streamline constants for current frame
	 * @param a_jitter Camera jitter offset
	 *
	 * Sets frame-specific constants like jitter offset, motion vector scale,
	 * and camera parameters. Must be called before Upscale().
	 */
	void UpdateConstants(float2 a_jitter);

	/**
	 * @brief Destroy DLSS resources and disable DLSS
	 *
	 * Disables DLSS mode and frees Streamline resources for the current viewport.
	 * Called when switching to a different upscaling method.
	 */
	void DestroyDLSSResources();

	// ========================================
	// State
	// ========================================

	bool initialized = false;       ///< True if Streamline SDK is initialized
	bool alreadyInitialized = false; ///< True if another plugin already called slInit
	bool featureDLSS = false;       ///< True if DLSS is available on current GPU

	sl::ViewportHandle viewport{ 0 };  ///< Streamline viewport handle
	sl::FrameToken* frameToken = nullptr;  ///< Current frame token for Streamline

	HMODULE interposer = NULL;  ///< Handle to sl.interposer.dll

	// ========================================
	// SL Interposer Function Pointers
	// ========================================

	// Core Functions
	PFun_slInit* slInit{};                                  ///< Initialize Streamline
	PFun_slShutdown* slShutdown{};                          ///< Shutdown Streamline
	PFun_slIsFeatureSupported* slIsFeatureSupported{};      ///< Check if feature is supported
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};            ///< Check if feature is loaded
	PFun_slSetFeatureLoaded* slSetFeatureLoaded{};          ///< Set feature loaded state
	PFun_slEvaluateFeature* slEvaluateFeature{};            ///< Execute feature (e.g., DLSS)
	PFun_slAllocateResources* slAllocateResources{};        ///< Allocate feature resources
	PFun_slFreeResources* slFreeResources{};                ///< Free feature resources
	PFun_slSetTag2* slSetTag{};                             ///< Tag resources for Streamline
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};  ///< Get feature requirements
	PFun_slGetFeatureVersion* slGetFeatureVersion{};        ///< Get feature version
	PFun_slUpgradeInterface* slUpgradeInterface{};          ///< Upgrade interface version
	PFun_slSetConstants* slSetConstants{};                  ///< Set frame constants
	PFun_slGetNativeInterface* slGetNativeInterface{};      ///< Get native interface
	PFun_slGetFeatureFunction* slGetFeatureFunction{};      ///< Get feature-specific function
	PFun_slGetNewFrameToken* slGetNewFrameToken{};          ///< Get new frame token
	PFun_slSetD3DDevice* slSetD3DDevice{};                  ///< Set D3D11 device

	// DLSS Specific Functions
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};  ///< Get optimal DLSS settings
	PFun_slDLSSGetState* slDLSSGetState{};                      ///< Get DLSS state
	PFun_slDLSSSetOptions* slDLSSSetOptions{};                  ///< Set DLSS options
};

}
