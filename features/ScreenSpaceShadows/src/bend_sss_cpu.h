#pragma once

// Copyright 2023 Sony Interactive Entertainment.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// If you have feedback, or found this code useful, we'd love to hear from you.
// https://www.bendstudio.com
// https://www.twitter.com/bendstudio
//
// We are *always* looking for talented graphics and technical programmers!
// https://www.bendstudio.com/careers

// Common screen space shadow projection code (CPU).

namespace Bend
{
	// Builds 1-8 same-CS dispatches over depth into a same-size single-channel shadow texture; off-screen lights usually need 1-2, on-screen 4-6; no inter-dispatch GPU sync.

	// Produces dispatch count, per-dispatch X/Y/Z wave counts, and shader parameters.

	/** @brief Per-dispatch data containing wave counts and shader wave offsets for a single compute dispatch. */
	struct DispatchData
	{
		int WaveCount[3];          // Compute Shader Dispatch(X,Y,Z) wave counts X/Y/Z
		int WaveOffset_Shader[2];  // This value is passed in to shader. It will be different for each dispatch
	};

	/** @brief Contains the list of compute dispatches and shared light coordinate data for a screen-space shadow pass. */
	struct DispatchList
	{
		float LightCoordinate_Shader[4];  // This value is passed in to shader, this will be the same value for all dispatches for this light

		DispatchData Dispatch[8];  // List of dispatches (max count is 8)
		int DispatchCount;         // Number of compute dispatches written to the list
	};

	// Helper functions
	inline int bend_min(const int a, const int b) { return a > b ? b : a; }
	inline int bend_max(const int a, const int b) { return a > b ? a : b; }

	/**
	 * @brief Builds a list of compute shader dispatches required to generate a screen-space shadow for a given light.
	 *
	 * Syncing the GPU between individual dispatches is not required.
	 *
	 * @param inLightProjection Homogeneous coordinate of the light, result of {light} * {ViewProjectionMatrix} (without W divide).
	 *                          For directional lights use float4(direction, 0); for point/spot use float4(position, 1).
	 * @param inViewportSize Width/height of the render target.
	 * @param inMinRenderBounds Minimum 2D screen bounds of the light within the viewport, inclusive.
	 * @param inMaxRenderBounds Maximum 2D screen bounds of the light within the viewport, inclusive.
	 * @param inExpandedZRange Set to true if the API expects [-1,+1] z/w output that maps to [0,1] in the depth buffer.
	 * @param inWaveSize Wavefront size of the compiled compute shader (currently only tested with 64).
	 * @return A DispatchList containing the light coordinate and up to 8 dispatches.
	 */
	DispatchList BuildDispatchList(float inLightProjection[4], int inViewportSize[2], int inMinRenderBounds[2], int inMaxRenderBounds[2], bool inExpandedZRange = false, int inWaveSize = 64)
	{
		DispatchList result = {};

		// Clamp W for light XY when the light is extremely off-screen (~1m px+) to avoid shader FP division precision loss.
		float xy_light_w = inLightProjection[3];
		float FP_limit = 0.000002f * (float)inWaveSize;

		if (xy_light_w >= 0 && xy_light_w < FP_limit)
			xy_light_w = FP_limit;
		else if (xy_light_w < 0 && xy_light_w > -FP_limit)
			xy_light_w = -FP_limit;

		// Need precise XY pixel coordinates of the light
		result.LightCoordinate_Shader[0] = ((inLightProjection[0] / xy_light_w) * +0.5f + 0.5f) * (float)inViewportSize[0];
		result.LightCoordinate_Shader[1] = ((inLightProjection[1] / xy_light_w) * -0.5f + 0.5f) * (float)inViewportSize[1];
		result.LightCoordinate_Shader[2] = inLightProjection[3] == 0 ? 0 : (inLightProjection[2] / inLightProjection[3]);
		result.LightCoordinate_Shader[3] = inLightProjection[3] > 0 ? 1 : -1;

		if (inExpandedZRange) {
			result.LightCoordinate_Shader[2] = result.LightCoordinate_Shader[2] * 0.5f + 0.5f;
		}

		int light_xy[2] = { (int)(result.LightCoordinate_Shader[0] + 0.5f), (int)(result.LightCoordinate_Shader[1] + 0.5f) };

		// Make the bounds inclusive, relative to the light
		const int biased_bounds[4] = {
			inMinRenderBounds[0] - light_xy[0],
			-(inMaxRenderBounds[1] - light_xy[1]),
			inMaxRenderBounds[0] - light_xy[0],
			-(inMinRenderBounds[1] - light_xy[1]),
		};

		// Process 4 light-centered quadrants; non-square rectangles split on the larger axis; 0=BL, 1=BR, 2=TL, 3=TR.
		for (int q = 0; q < 4; q++) {
			// Quads 0 and 3 needs to be +1 vertically, 1 and 2 need to be +1 horizontally
			bool vertical = q == 0 || q == 3;

			// Bounds relative to the quadrant
			const int bounds[4] = {
				bend_max(0, ((q & 1) ? biased_bounds[0] : -biased_bounds[2])) / inWaveSize,
				bend_max(0, ((q & 2) ? biased_bounds[1] : -biased_bounds[3])) / inWaveSize,
				bend_max(0, (((q & 1) ? biased_bounds[2] : -biased_bounds[0]) + inWaveSize * (vertical ? 1 : 2) - 1)) / inWaveSize,
				bend_max(0, (((q & 2) ? biased_bounds[3] : -biased_bounds[1]) + inWaveSize * (vertical ? 2 : 1) - 1)) / inWaveSize,
			};

			if ((bounds[2] - bounds[0]) > 0 && (bounds[3] - bounds[1]) > 0) {
				int bias_x = (q == 2 || q == 3) ? 1 : 0;
				int bias_y = (q == 1 || q == 3) ? 1 : 0;

				DispatchData& disp = result.Dispatch[result.DispatchCount++];

				disp.WaveCount[0] = inWaveSize;
				disp.WaveCount[1] = bounds[2] - bounds[0];
				disp.WaveCount[2] = bounds[3] - bounds[1];
				disp.WaveOffset_Shader[0] = ((q & 1) ? bounds[0] : -bounds[2]) + bias_x;
				disp.WaveOffset_Shader[1] = ((q & 2) ? -bounds[3] : bounds[1]) + bias_y;

				// Need the quadrant far corner relative to the light to intersect the diagonal ray with the bounds edge.
				int axis_delta = +biased_bounds[0] - biased_bounds[1];
				if (q == 1)
					axis_delta = +biased_bounds[2] + biased_bounds[1];
				if (q == 2)
					axis_delta = -biased_bounds[0] - biased_bounds[3];
				if (q == 3)
					axis_delta = -biased_bounds[2] + biased_bounds[3];

				axis_delta = (axis_delta + inWaveSize - 1) / inWaveSize;

				if (axis_delta > 0) {
					DispatchData& disp2 = result.Dispatch[result.DispatchCount++];

					// Take copy of current volume
					disp2 = disp;

					if (q == 0) {
						// Split on Y, split becomes -1 larger on x
						disp2.WaveCount[2] = bend_min(disp.WaveCount[2], axis_delta);
						disp.WaveCount[2] -= disp2.WaveCount[2];
						disp2.WaveOffset_Shader[1] = disp.WaveOffset_Shader[1] + disp.WaveCount[2];
						disp2.WaveOffset_Shader[0]--;
						disp2.WaveCount[1]++;
					}
					if (q == 1) {
						// Split on X, split becomes +1 larger on y
						disp2.WaveCount[1] = bend_min(disp.WaveCount[1], axis_delta);
						disp.WaveCount[1] -= disp2.WaveCount[1];
						disp2.WaveOffset_Shader[0] = disp.WaveOffset_Shader[0] + disp.WaveCount[1];
						disp2.WaveCount[2]++;
					}
					if (q == 2) {
						// Split on X, split becomes -1 larger on y
						disp2.WaveCount[1] = bend_min(disp.WaveCount[1], axis_delta);
						disp.WaveCount[1] -= disp2.WaveCount[1];
						disp.WaveOffset_Shader[0] += disp2.WaveCount[1];
						disp2.WaveCount[2]++;
						disp2.WaveOffset_Shader[1]--;
					}
					if (q == 3) {
						// Split on Y, split becomes +1 larger on x
						disp2.WaveCount[2] = bend_min(disp.WaveCount[2], axis_delta);
						disp.WaveCount[2] -= disp2.WaveCount[2];
						disp.WaveOffset_Shader[1] += disp2.WaveCount[2];
						disp2.WaveCount[1]++;
					}

					// Remove if too small
					if (disp2.WaveCount[1] <= 0 || disp2.WaveCount[2] <= 0) {
						disp2 = result.Dispatch[--result.DispatchCount];
					}
					if (disp.WaveCount[1] <= 0 || disp.WaveCount[2] <= 0) {
						disp = result.Dispatch[--result.DispatchCount];
					}
				}
			}
		}

		// Scale the shader values by the wave count, the shader expects this
		for (int i = 0; i < result.DispatchCount; i++) {
			result.Dispatch[i].WaveOffset_Shader[0] *= inWaveSize;
			result.Dispatch[i].WaveOffset_Shader[1] *= inWaveSize;
		}

		return result;
	}
}

/*
* Common problems:
* - Compile: tested with HLSL/DXC and PS5 HLSL->PS5 wrapper; DX11/FXC may work only after removing unsupported features such as early-out wave intrinsics; other shader languages need manual conversion.
* - Nonsense output: enable DebugOutputWaveIndex; wavefronts should align/project toward the light, otherwise inLightProjection is wrong.
* - inLightProjection: treat as VS SV_POSITION export; use float4(position,1) * ViewProjectionMatrix for positional lights, float4(direction,0) * ViewProjectionMatrix for directional.
* - Excess/halo/cut-out shadows: FarDepthValue/NearDepthValue are wrong (typically far=0, near=1); enable inExpandedZRange only when VS z is [-1,+1] mapped to [0,1] depth.
* - Glitch lines: try toggling USE_HALF_PIXEL_OFFSET, enabled by default for HLSL.
* - Offscreen/edge invalid shadows: PointBorderSampler must clamp-to-border with an invalid value such as FarDepthValue because the shader intentionally samples offscreen.
* - Light fixed under camera rotation: using ProjectionMatrix instead of ViewProjectionMatrix.
* - Thick/faded shadow: scale SurfaceThickness from 0.005 by powers of 2 and scale BilinearThreshold similarly.
* - Striated flats: tune BilinearThreshold with DebugOutputEdgeMask or enable IgnoreEdgePixels; more common in occluded/visualized output with BilinearSamplingOffsetMode=false, less noticeable lit.
*/
