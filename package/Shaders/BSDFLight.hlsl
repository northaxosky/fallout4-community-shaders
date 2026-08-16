// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
#ifndef BSDF_LIGHT_FAMILY
#  error "define BSDF_LIGHT_FAMILY; no BSDFLightShader source family is assumed"
#endif

#if BSDF_LIGHT_FAMILY == 1
#  include "BSDFLight/DirSplits1Family.hlsli"
#elif BSDF_LIGHT_FAMILY == 2
#  include "BSDFLight/DirSplits2Family.hlsli"
#elif BSDF_LIGHT_FAMILY == 3
#  include "BSDFLight/DirSplits3Family.hlsli"
#elif BSDF_LIGHT_FAMILY == 4
#  include "BSDFLight/ShadowOnlyFamily.hlsli"
#elif BSDF_LIGHT_FAMILY == 5
#  include "BSDFLight/ShadowOnlyBlendSplitFamily.hlsli"
#elif BSDF_LIGHT_FAMILY == 6
#  include "BSDFLight/UnshadowedFamily.hlsli"
#elif BSDF_LIGHT_FAMILY == 7
#  include "BSDFLight/GoboFamily.hlsli"
#elif BSDF_LIGHT_FAMILY == 8
#  include "BSDFLight/AttenuationOnlyFamily.hlsli"
#elif BSDF_LIGHT_FAMILY == 9
#  include "BSDFLight/DeferredFamily.hlsli"
#else
#  error "unknown BSDF_LIGHT_FAMILY"
#endif
