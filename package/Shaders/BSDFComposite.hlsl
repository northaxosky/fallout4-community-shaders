// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Family IDs are append-only and shared with the C++ catalog.

#ifndef BSDF_COMPOSITE_FAMILY
#  error "define BSDF_COMPOSITE_FAMILY; no composite source family is assumed"
#endif

#if BSDF_COMPOSITE_FAMILY == 1
#  include "BSDFComposite/AmbientIblCb31Family.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 2
#  include "BSDFComposite/AmbientIblCb47Family.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 3
#  include "BSDFComposite/AmbientIblCompactFamily.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 4
#  include "BSDFComposite/AmbientIblMinimalFamily.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 6
#  ifndef COMPOSITE_CB2_COUNT
#    error "the 2D accumulator family sizes its CB2 array from COMPOSITE_CB2_COUNT; the native count is never assumed"
#  endif
#  include "BSDFComposite/Composite2DAccumulator.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 7
#  ifndef COMPOSITE_CB2_COUNT
#    error "the 2D fog family is registered per native CB2 count; define COMPOSITE_CB2_COUNT"
#  endif
#  include "BSDFComposite/Composite2DFog.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 8
#  include "BSDFComposite/Composite5c4bf49d.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 9
#  include "BSDFComposite/Composite94c8634b.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 10
#  include "BSDFComposite/CompositeCubeIbl.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 11
#  include "BSDFComposite/CompositeNoT0Accumulator.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 12
#  include "BSDFComposite/CompositeNoT0Fog.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 15
#  include "BSDFComposite/CompositeSssMrtRecordNormal.hlsli"
#elif BSDF_COMPOSITE_FAMILY == 16
#  include "BSDFComposite/CompositeSssMrtSurfaceContact.hlsli"
#else
#  error "unknown BSDF_COMPOSITE_FAMILY"
#endif
