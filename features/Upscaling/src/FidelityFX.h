#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include "Buffer.h"

#include <memory>

namespace cs::features::Upscaling
{

/**
 * @class FidelityFX
 * @brief Manager for AMD FidelityFX Super Resolution 3 (FSR3) upscaling
 *
 * Handles initialization, resource management, and execution of FSR3 upscaling.
 * FSR3 provides temporal upscaling with reactive mask support for better quality
 * around particles and transparent effects.
 */
class FidelityFX
{
public:
	// ========================================
	// Singleton
	// ========================================

	/**
	 * @brief Get the singleton instance
	 * @return Pointer to the global FidelityFX instance
	 */
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	// ========================================
	// Resource Management
	// ========================================

	/**
	 * @brief Create FSR3 context and resources
	 *
	 * Initializes the FSR3 backend, allocates scratch memory, creates the FSR3 context,
	 * and allocates textures for opaque-only color and reactive mask generation.
	 *
	 * @note Should only be called when FSR is the active upscaling method
	 */
	void CreateFSRResources();

	/**
	 * @brief Destroy FSR3 context and release resources
	 *
	 * Destroys the FSR3 context, frees scratch memory, and releases all FSR-specific textures.
	 * Automatically called when switching to a different upscaling method.
	 */
	void DestroyFSRResources();

	// ========================================
	// FSR3 Operations
	// ========================================

	/**
	 * @brief Copy opaque-only color buffer for reactive mask generation
	 *
	 * Captures the scene color before transparent/alpha-blended objects are rendered.
	 * This is used to generate a reactive mask that improves FSR quality around particles,
	 * effects, and transparent surfaces.
	 *
	 * @note Must be called before rendering transparent objects
	 */
	void CopyOpaqueTexture();

	/**
	 * @brief Generate reactive mask for FSR3
	 *
	 * Compares opaque-only and final color buffers to create a reactive mask.
	 * The reactive mask tells FSR3 which pixels contain particles or effects that
	 * need special handling to avoid temporal artifacts.
	 *
	 * @note Must be called after CopyOpaqueTexture and after transparent rendering
	 */
	void GenerateReactiveMask();

	/**
	 * @brief Execute FSR3 upscaling
	 * @param a_color Input color texture at render resolution
	 * @param a_jitter Camera jitter offset for current frame
	 * @param a_renderSize Render resolution dimensions
	 *
	 * Performs temporal upscaling from render resolution to display resolution
	 * using FSR3 algorithm with motion vectors, depth, and reactive mask.
	 */
	void Upscale(Texture2D* a_color, float2 a_jitter, float2 a_renderSize);

	// ========================================
	// Resources
	// ========================================

	FfxFsr3Context fsrContext;  ///< FSR3 context handle

	std::unique_ptr<Texture2D> colorOpaqueOnlyTexture;  ///< Color before transparent objects
	std::unique_ptr<Texture2D> reactiveMaskTexture;     ///< Generated reactive mask for FSR3

private:
	void* fsrScratchBuffer = nullptr;  ///< FSR3 backend scratch memory (freed in DestroyFSRResources)
};

}
