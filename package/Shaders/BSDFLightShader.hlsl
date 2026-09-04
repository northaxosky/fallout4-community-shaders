// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#if defined(DIRECTIONAL) && defined(SCREEN_SPACE_SHADOWS)
#include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#endif

#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
#include "TerrainShadows/TerrainShadows.hlsli"
#endif

#ifdef WETNESS_EFFECTS
#include "WetnessEffects/WetnessEffects.hlsli"
#endif

#ifdef INVERSE_SQUARE_LIGHTING
#include "InverseSquareLighting/InverseSquareLighting.hlsli"
#endif

#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
#include "WaterEffects/WaterCausticsSampler.hlsli"
#include "WaterEffects/WaterCaustics.hlsli"
#endif

#ifdef SKYLIGHTING
#if defined(AMBIENT_IBL_IN_LIGHT) || defined(AMBIENT) \
    || defined(BSDFLIGHT_PS_AMBIENT)
#define FO4_SKYLIGHTING_AMBIENT_CONSUMER 1
#endif
#include "Skylighting/SkylightingResources.hlsli"
#include "Skylighting/Skylighting.hlsli"
#endif

#ifdef BSDFLIGHT_PS_DEFERRED

#define LIGHT_TYPE_DIRECTIONAL 1
#define LIGHT_TYPE_POINT       2
#define LIGHT_TYPE_SPOT        3

#ifndef LIGHT_TYPE
#  define LIGHT_TYPE LIGHT_TYPE_DIRECTIONAL
#endif

#if LIGHT_TYPE != LIGHT_TYPE_DIRECTIONAL \
    && LIGHT_TYPE != LIGHT_TYPE_POINT \
    && LIGHT_TYPE != LIGHT_TYPE_SPOT
#  error "LIGHT_TYPE must be DIRECTIONAL (1), POINT (2), or SPOT (3)"
#endif

#if defined(HALFOMNI) && !defined(POINTOMNI)
#  error "HALFOMNI only ever occurs with POINTOMNI; this macro set is malformed"
#endif

#if LIGHT_TYPE == LIGHT_TYPE_POINT && defined(POINTOMNI) && defined(SHADOW)
#  error "POINTOMNI with SHADOW requires LIGHT_TYPE=3"
#endif

#if LIGHT_TYPE == LIGHT_TYPE_SPOT
#  if defined(SPOT) && defined(POINTSPOT)
#    error "LIGHT_TYPE=3 takes exactly one of SPOT or POINTSPOT, never both"
#  endif
#  if defined(POINTOMNI) && (defined(SPOT) || defined(POINTSPOT))
#    error "no native blob carries POINTOMNI together with SPOT or POINTSPOT"
#  endif
#  if defined(POINTOMNI) && !defined(SHADOW)
#    error "POINTOMNI without SHADOW is a LIGHT_TYPE=2 ABI; do not route it here"
#  endif
#  if !defined(SPOT) && !defined(POINTSPOT) && !defined(POINTOMNI)
#    error "LIGHT_TYPE=3 requires SPOT=1, POINTSPOT=1, or POINTOMNI=1 with SHADOW=1"
#  endif
#  if defined(SPOT) && defined(SHADOW)
#    error "no native SPOT blob carries SHADOW"
#  endif
#  if defined(POINTSPOT) && !defined(SHADOW)
#    error "POINTSPOT implies SHADOW=1 in every native blob"
#  endif
#  if defined(SPOT) && defined(ATTENUATION_ONLY)
#    error "no native SPOT blob carries ATTENUATION_ONLY"
#  endif
#  if defined(POINTOMNI) && defined(ATTENUATION_ONLY)
#    error "no native POINTOMNI blob carries ATTENUATION_ONLY"
#  endif
#  if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
        + defined(FILTER_POISSON) + defined(FILTER_PCSSPOISSON)) > 1
#    error "FILTER_* macros are mutually exclusive"
#  endif
#  if defined(SPOT) \
      && (defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
          || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON))
#    error "no native SPOT blob carries a FILTER_* macro"
#  endif

#  if defined(POINTSPOT) || (defined(POINTOMNI) && defined(SHADOW))
#    define FO4_PROJECTED_SHADOW_FAMILY 1
#  endif
#  if (defined(POINTSPOT) || (defined(POINTOMNI) && defined(SHADOW))) \
      && defined(FILTER_PCF1) \
      && defined(IGNOREROUGHNESS) && defined(IGNORERIM) \
      && !defined(SPECULAR)
#    define FO4_DEFERRED_PCF1_IGNORE_PAIR 1
#  endif
#  if defined(SPOT) && defined(IGNOREROUGHNESS) \
      && !defined(SPECULAR)
#    define FO4_DEFERRED_SPOT_IGNORE_ROUGHNESS 1
#  endif
#  if defined(SPOT) && defined(IGNORERIM) \
      && !defined(IGNOREROUGHNESS) && !defined(GOBOPROJECTION) \
      && !defined(SPECULAR)
#    define FO4_DEFERRED_SPOT_IGNORE_RIM 1
#  endif
#  if defined(SPOT) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_SPOT_BASE 1
#  endif
#  if defined(SPOT) && defined(GOBOPROJECTION) \
      && !defined(SPECULAR) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_SPOT_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF1) \
      && !defined(SPECULAR) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM) && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_PCF1_POINTSPOT_BASE 1
#  endif
#  if (defined(POINTSPOT) || (defined(POINTOMNI) && defined(SHADOW))) \
      && defined(FILTER_PCF1) \
      && defined(SPECULAR) && defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF1_SPEC_IGNORE 1
#  endif
#  if defined(SPOT) && defined(SPECULAR) \
      && defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_SPOT_SPEC_IGNORE 1
#  endif
#  if defined(SPOT) && defined(SPECULAR) \
      && defined(GOBOPROJECTION) && defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_SPOT_SPEC_GOBO_IGNORE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(SPECULAR) && defined(IGNOREROUGHNESS) \
      && !defined(HALFOMNI) && !defined(GOBOPROJECTION) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_OMNI_SPEC_IGNORE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(SPECULAR) && defined(IGNOREROUGHNESS) \
      && !defined(GOBOPROJECTION) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_POINTSPOT_SPEC_IGNORE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(SPECULAR) && defined(IGNOREROUGHNESS) \
      && !defined(GOBOPROJECTION) && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_POINTSPOT_SPEC_IGNORE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(SPECULAR) && defined(IGNOREROUGHNESS) \
      && !defined(HALFOMNI) && !defined(GOBOPROJECTION) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_OMNI_SPEC_IGNORE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(SPECULAR) && defined(GOBOPROJECTION) \
      && !defined(HALFOMNI) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(SPECULAR) && defined(GOBOPROJECTION) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(SPECULAR) && !defined(GOBOPROJECTION) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(SPECULAR) && !defined(HALFOMNI) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_OMNI_SPEC_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(SPECULAR) && defined(GOBOPROJECTION) \
      && defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_IGNORE 1
#  endif
#  if defined(SPOT) && defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_SPOT_SPEC_BASE 1
#  endif
#  if defined(SPOT) && defined(SPECULAR) \
      && defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_SPOT_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF1) \
      && defined(SPECULAR) && defined(GOBOPROJECTION) \
      && !defined(HALFOMNI) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF1_OMNI_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF1) \
      && defined(SPECULAR) && defined(GOBOPROJECTION) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF1_POINTSPOT_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF1) \
      && defined(SPECULAR) && !defined(GOBOPROJECTION) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF1_POINTSPOT_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF1) \
      && defined(SPECULAR) && !defined(HALFOMNI) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF1_OMNI_SPEC_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) \
      && !defined(FILTER_PCF1) && !defined(FILTER_PCF9) \
      && !defined(FILTER_POISSON) && defined(SPECULAR) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && !defined(HALFOMNI) \
      && !defined(FILTER_PCF1) && !defined(FILTER_PCF9) \
      && !defined(FILTER_POISSON) && defined(SPECULAR) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_RAW_OMNI_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(SPECULAR) && !defined(HALFOMNI) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_OMNI_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(SPECULAR) && defined(GOBOPROJECTION) \
      && !defined(HALFOMNI) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(SPECULAR) && !defined(GOBOPROJECTION) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(SPECULAR) && defined(GOBOPROJECTION) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE 1
#  endif
#  if defined(FO4_DEFERRED_PCF1_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_GOBO_IGNORE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_IGNORE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_IGNORE)
#    define FO4_DEFERRED_SPEC_IGNORE 1
#  endif
#  if defined(FO4_DEFERRED_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_BASE)
#    define FO4_DEFERRED_SPEC_ORDER 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF1) \
      && !defined(SPECULAR) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM) && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_PCF1_OMNI_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF1) \
      && defined(GOBOPROJECTION) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF1_POINTSPOT_GOBO_BASE 1
#  endif
#  if defined(FO4_DEFERRED_PCF1_POINTSPOT_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_GOBO_BASE)
#    define FO4_DEFERRED_PCF1_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) \
      && !defined(FILTER_PCF1) && !defined(FILTER_PCF9) \
      && !defined(FILTER_POISSON) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_RAW_OMNI_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) \
      && !defined(FILTER_PCF1) && !defined(FILTER_PCF9) \
      && !defined(FILTER_POISSON) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_RAW_POINTSPOT_BASE 1
#  endif
#  if defined(FO4_DEFERRED_RAW_OMNI_BASE) \
      || defined(FO4_DEFERRED_RAW_POINTSPOT_BASE)
#    define FO4_DEFERRED_RAW_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && !defined(SPECULAR) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM) && !defined(GOBOPROJECTION) \
      && !defined(FILTER_POISSON)
#    define FO4_DEFERRED_HALFOMNI_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_PCF1) && defined(SPECULAR) \
      && defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_HALFOMNI_PCF1_SPEC_IGNORE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_PCF9) && defined(SPECULAR) \
      && defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_HALFOMNI_PCF9_SPEC_IGNORE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_PCF1) && defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_HALFOMNI_PCF1_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_PCF1) && defined(SPECULAR) \
      && defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_PCF9) && defined(SPECULAR) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_PCF9) && defined(SPECULAR) \
      && defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_POISSON) && !defined(SPECULAR) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_HALFOMNI_POISSON_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_POISSON) && defined(SPECULAR) \
      && !defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI) \
      && defined(FILTER_POISSON) && defined(SPECULAR) \
      && defined(GOBOPROJECTION) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE 1
#  endif
#  if defined(FO4_DEFERRED_HALFOMNI_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE)
#    define FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH 1
#  endif
#  if defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
#    define FO4_DEFERRED_SPEC_ORDER 1
#  endif
#  if defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
#    define FO4_DEFERRED_HALFOMNI_GOBO_NATIVE_BRANCH 1
#  endif
#  if defined(FO4_DEFERRED_HALFOMNI_POISSON_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
#    define FO4_DEFERRED_HALFOMNI_POISSON_LIVE_MASK 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF9) \
      && !defined(SPECULAR) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM) && !defined(GOBOPROJECTION)
#    define FO4_DEFERRED_PCF9_OMNI_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(GOBOPROJECTION) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_OMNI_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && !defined(GOBOPROJECTION) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_POINTSPOT_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(GOBOPROJECTION) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_PCF9_POINTSPOT_GOBO_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_POISSON) \
      && !defined(GOBOPROJECTION) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_POINTSPOT_BASE 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_POISSON) \
      && !defined(HALFOMNI) && !defined(GOBOPROJECTION) \
      && !defined(SPECULAR) && !defined(IGNOREROUGHNESS) \
      && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_OMNI_BASE 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(GOBOPROJECTION) && !defined(SPECULAR) \
      && !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
#    define FO4_DEFERRED_POISSON_POINTSPOT_GOBO_BASE 1
#  endif
#  if defined(FO4_DEFERRED_PCF1_BASE) \
      || defined(FO4_DEFERRED_SPOT_BASE) \
      || defined(FO4_DEFERRED_SPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_RAW_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_BASE)
#    define FO4_DEFERRED_BASE_ROUGHNESS 1
#  endif
#  if defined(FO4_DEFERRED_BASE_ROUGHNESS) \
      || defined(FO4_DEFERRED_SPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
#    define FO4_DEFERRED_PACKED_RIM 1
#  endif
#  if defined(FO4_DEFERRED_SPOT_IGNORE_ROUGHNESS) \
      || defined(FO4_DEFERRED_SPOT_IGNORE_RIM) \
      || defined(FO4_DEFERRED_SPOT_BASE) \
      || defined(FO4_DEFERRED_SPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_GOBO_IGNORE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_GOBO_BASE)
#    define FO4_DEFERRED_SPOT_NATIVE_CONE 1
#  endif
#  if defined(FO4_DEFERRED_SPOT_IGNORE_RIM) \
      || defined(FO4_DEFERRED_BASE_ROUGHNESS) \
      || defined(FO4_DEFERRED_SPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
#    define FO4_DEFERRED_NATIVE_ROUGHNESS_BRDF 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(IGNOREROUGHNESS) && !defined(IGNORERIM) \
      && !defined(GOBOPROJECTION) && !defined(SPECULAR)
#    define FO4_DEFERRED_PCF9_OMNI_IGNORE_ROUGHNESS 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(GOBOPROJECTION) && defined(IGNOREROUGHNESS) \
      && defined(IGNORERIM) && !defined(SPECULAR)
#    define FO4_DEFERRED_PCF9_OMNI_GOBO_IGNORE_PAIR 1
#  endif
#  if defined(POINTOMNI) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(GOBOPROJECTION) && defined(IGNOREROUGHNESS) \
      && defined(IGNORERIM) && !defined(HALFOMNI) \
      && !defined(SPECULAR)
#    define FO4_DEFERRED_POISSON_OMNI_GOBO_IGNORE_PAIR 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(IGNOREROUGHNESS) && defined(IGNORERIM) \
      && !defined(GOBOPROJECTION) && !defined(SPECULAR)
#    define FO4_DEFERRED_PCF9_POINTSPOT_IGNORE_PAIR 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(IGNOREROUGHNESS) && defined(IGNORERIM) \
      && !defined(GOBOPROJECTION) && !defined(SPECULAR)
#    define FO4_DEFERRED_POISSON_POINTSPOT_IGNORE_PAIR 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_POISSON) \
      && defined(GOBOPROJECTION) && defined(IGNOREROUGHNESS) \
      && defined(IGNORERIM) && !defined(SPECULAR)
#    define FO4_DEFERRED_POISSON_POINTSPOT_GOBO_IGNORE_PAIR 1
#  endif
#  if defined(POINTSPOT) && defined(SHADOW) && defined(FILTER_PCF9) \
      && defined(GOBOPROJECTION) && defined(IGNOREROUGHNESS) \
      && defined(IGNORERIM) && !defined(SPECULAR)
#    define FO4_DEFERRED_PCF9_POINTSPOT_GOBO_IGNORE_PAIR 1
#  endif
#  if defined(FO4_DEFERRED_PCF1_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_PCF9_OMNI_GOBO_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_GOBO_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_POISSON_OMNI_GOBO_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_GOBO_IGNORE_PAIR)
#    define FO4_DEFERRED_PROJECTED_IGNORE_PAIR 1
#  endif
#  if defined(FO4_DEFERRED_PROJECTED_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_IGNORE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_GOBO_BASE) \
      || (defined(FO4_DEFERRED_PCF1_SPEC_IGNORE) \
          && defined(POINTSPOT) && defined(GOBOPROJECTION))
#    define FO4_DEFERRED_POINTSPOT_NATIVE_GOBO 1
#  endif
#  if defined(FO4_DEFERRED_PROJECTED_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_PCF9_OMNI_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE)
#    define FO4_DEFERRED_OMNI_NATIVE_PACK 1
#  endif
#  if defined(FO4_DEFERRED_PROJECTED_IGNORE_PAIR) \
      || defined(FO4_DEFERRED_SPOT_IGNORE_ROUGHNESS) \
      || defined(FO4_DEFERRED_PCF9_OMNI_IGNORE_ROUGHNESS)
#    define FO4_DEFERRED_BRANCH_LOCAL_DIFFUSE 1
#  endif
#  if defined(FO4_DEFERRED_BRANCH_LOCAL_DIFFUSE) \
      || defined(FO4_DEFERRED_SPOT_IGNORE_RIM) \
      || defined(FO4_DEFERRED_BASE_ROUGHNESS)
#    define FO4_DEFERRED_BRANCH_LOCAL_NDOTL 1
#  endif

#  if defined(FO4_PROJECTED_SHADOW_FAMILY) \
      && (defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON))
#    error "POINTSPOT and POINTOMNI do not support PCSS filters"
#  endif

#endif

#include "Common/DeferredContracts.hlsli"

#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_sss_params;

    float4 cb12_idx29_sss_angles;

    float4 cb12_idx30;
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 SunDirection_and_padding;

    float4 SunColor_HDR;

#ifdef AMBIENT_IBL_IN_LIGHT

    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;

    float4 cb2_pad_9;
#else

    float4 cb2_pad_3_9[7];
#endif

    float4 cb2_idx10_cascade_range;

    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;

    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;

    float4 cb2_pad_17_19[3];

    float4 cb2_idx20_pcf_kernel_scale;

    float4 cb2_idx21_cascade0_depth_range;

    float4 cb2_idx22_cascade1_depth_range;

    float4 cb2_pad_23;

    float4 cb2_idx24_distance_fade;
};

Texture2D<float4> g_tGbufferAlbedo : register(t0);

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);

Texture2D<float4> g_tMainDepth : register(t3);

Texture2DArray<float4> g_tCascadeShadowAtlas : register(t5);

SamplerState g_sGbufferAlbedo  : register(s0);
SamplerState g_sGbufferNormal  : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth      : register(s3);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);

static const float2 SUN_SHADOW_POISSON[32] =
{
    float2(0.493393, 0.394269), float2(0.798547, 0.885922),
    float2(0.247322, 0.926450), float2(0.051454, 0.140782),
    float2(0.831843, 0.009552), float2(0.428632, 0.017151),
    float2(0.015656, 0.749779), float2(0.758385, 0.496170),
    float2(0.223487, 0.562151), float2(0.011628, 0.406995),
    float2(0.241462, 0.304636), float2(0.430311, 0.727226),
    float2(0.981811, 0.278359), float2(0.407056, 0.500534),
    float2(0.123478, 0.463546), float2(0.809534, 0.682272),
    float2(0.675802, 0.653920), float2(0.238014, 0.069338),
    float2(0.000671, 0.611103), float2(0.621876, 0.499039),
    float2(0.712882, 0.115299), float2(0.913663, 0.819391),
    float2(0.295450, 0.809687), float2(0.985015, 0.117801),
    float2(0.630757, 0.313211), float2(0.362621, 0.185705),
    float2(0.164464, 0.787591), float2(0.003845, 0.938841),
    float2(0.522752, 0.146275), float2(0.987518, 0.938994),
    float2(0.770104, 0.315531), float2(0.044832, 0.268838),

};

static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.141593;

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    float  scale = sqrt(recon);
    return float3(enc * scale, z);
}

#ifdef AMBIENT_IBL_IN_LIGHT
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(cb2_ambient_gradient_row0, directionH);
    encoded.y = dot(cb2_ambient_gradient_row1, directionH);
    encoded.z = dot(cb2_ambient_gradient_row2, directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

float ComputeCascadePCF(float3 posView, float4 row0, float4 row1, float4 row2,
                        float cascadeIdx, float cascadeDepthRcp, float kernelScale,
                        float biasScale)
{
    float4 posLightH;
    posLightH.x = dot(row0, float4(posView, 1.0));
    posLightH.y = dot(row1, float4(posView, 1.0));
    posLightH.z = dot(row2, float4(posView, 1.0));

    float zRef = posLightH.z - cascadeDepthRcp * biasScale;

    float accum = 0.0;
    [loop]
    for (int r = 0; r < 8; ++r)
    {
        float2 jitter0 = (SUN_SHADOW_POISSON[r * 2 + 0] - 0.5) * kernelScale;
        float2 jitter1 = (SUN_SHADOW_POISSON[r * 2 + 1] - 0.5) * kernelScale;

        float2 uv0 = posLightH.xy + jitter0 * 2.0;
        float2 uv1 = posLightH.xy + jitter1 * 2.0;
        accum += g_tCascadeShadowAtlas.SampleCmpLevelZero(g_sCascadeShadowCmp,
                                                          float3(uv0, cascadeIdx),
                                                          zRef);
        accum += g_tCascadeShadowAtlas.SampleCmpLevelZero(g_sCascadeShadowCmp,
                                                          float3(uv1, cascadeIdx),
                                                          zRef);
    }
    return accum * 0.0625;
}

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv4.x);
    float ddy_ = ddy_coarse(uv4.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    float2 uvNDC = uv4.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float4 albedoSample = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

#ifdef AMBIENT_IBL_IN_LIGHT
    float3 ambientDiffuse = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

    float roughness01 = 1.0 - matSample.x;
    float posViewLenSq = dot(-posView, -posView);
    float posViewLen   = rsqrt(posViewLenSq);
    float3 viewDirNeg  = -posView * posViewLen;

    bool cascade0Active = (linearizedDepth < cb2_idx10_cascade_range.y);
    bool cascade1Active = (cb2_idx10_cascade_range.x < linearizedDepth);

    float kernelScale = cb2_idx20_pcf_kernel_scale.z * 3.0;

    float cascade0Pcf = 1.0;
    if (cascade0Active)
    {
        float c0DepthRcp = 1.0 / (cb2_idx21_cascade0_depth_range.w
                                   - cb2_idx21_cascade0_depth_range.z);
        cascade0Pcf = ComputeCascadePCF(posView,
                                         cb2_cascade0_row0, cb2_cascade0_row1,
                                         cb2_cascade0_row2,
                                         0.0, c0DepthRcp, kernelScale, 0.275);
    }

    float cascade1Pcf = 1.0;
    if (cascade1Active)
    {
        float c1DepthRcp = 1.0 / (cb2_idx22_cascade1_depth_range.w
                                   - cb2_idx22_cascade1_depth_range.z);
        cascade1Pcf = ComputeCascadePCF(posView,
                                         cb2_cascade1_row0, cb2_cascade1_row1,
                                         cb2_cascade1_row2,
                                         1.0, c1DepthRcp, kernelScale, 1.0);
    }

    float blendRange = cb2_idx10_cascade_range.y - cb2_idx10_cascade_range.x;
    float t = saturate((linearizedDepth - cb2_idx10_cascade_range.x) / blendRange);
    float blendW = t * t * (3.0 - 2.0 * t);
    float shadowPcf = lerp(cascade0Pcf, cascade1Pcf, blendW);

    if (!cascade1Active) shadowPcf = cascade0Pcf;
    if (!cascade0Active) shadowPcf = cascade1Pcf;

    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    shadowPcf = fadeFactor * (shadowPcf - 1.0) + 1.0;

    float3 albedoPremult = albedoSample.w * albedoSample.xyz;
    float  NdotL_raw     = dot(normalView, SunDirection_and_padding.xyz);
    float  NdotL_pos     = max(NdotL_raw, 0.0);
    float  NdotL_clamped = min(NdotL_pos, 1.0);
    float  oneMinusGloss = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres   = 1.0 - oneMinusGloss * oneMinusGloss4;

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {

        float skinNdotL = dot(matSample.xyz, SunDirection_and_padding.xyz);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity = saturate(cb12_idx28_sss_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoSample.w, sssIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2 = exp2(log2(vis2) * cb12_idx28_sss_params.y) * cb12_idx28_sss_params.x;

        brdfSpecular  = NdotL_clamped * (pow2 * SunColor_HDR.xyz);
        brdfModulator = 0.0;
    }
    else
    {

        float depthScale = matSample.z * 100.0;
        float specExpBase = exp2(matSample.x * 10.0 + 1.0);
        float specExpScale = 1.0 - schlickFres * 0.98;
        float specExp = specExpScale * specExpBase;

        float NdotV_raw = dot(viewDirNeg, normalView);
#ifdef AMBIENT_IBL_IN_LIGHT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
        float oneMinusNdotV = 1.0 - saturate(NdotV_raw);
        float ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
        ambientSpecular =
            matSample.y * ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir);
#endif
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = SunDirection_and_padding.xyz - normalView * NdotL_raw;
        float tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA = roughSq / (roughSq + 0.57);
        float visB = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix = NdotL_pos * visibilityGeom;

        float3 halfVec = SunDirection_and_padding.xyz - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));

        float distributionNorm = (specExpBase * specExpScale + 2.0) * 0.159155;
        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, 0.0);
        float minN = min(NdotL_clamped, NdotV_sat);
        float twoNdotH = NdotH + NdotH;
        bool usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_DIRECTIONAL_SPECULAR_SCALE;

        brdfSpecular = NdotL_clamped * (specMag * SunColor_HDR.xyz);
        brdfModulator = depthScale;
    }

    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float ambientFres = 1.0 - NdotV_view;
    ambientFres = exp2(log2(ambientFres) * 0.01);

    float fresEdge = saturate(dot(viewDirNeg, -SunDirection_and_padding.xyz));
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;

    float3 finalDiffuse  = SunColor_HDR.xyz * ambientTerm;
    finalDiffuse += SunColor_HDR.xyz * brdfShadowMix;

    float backfaceWrap = saturate(-NdotL_raw);
    finalDiffuse += SunColor_HDR.xyz * (albedoPremult * backfaceWrap);

    float forwardBlend = saturate((brdfModulator + NdotL_raw) / (brdfModulator + 1.0));
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * SunColor_HDR.xyz) * albedoSample.xyz;

#ifdef AMBIENT_IBL_IN_LIGHT
#ifdef SKYLIGHTING
    Skylighting::Evaluation skylighting =
        Skylighting::Evaluate(posView, normalView);
    Skylighting::ApplyAmbient(
        ambientDiffuse, ambientSpecular, skylighting);
#endif
#endif

    float specMix = (1.0 - schlickFres * 0.5);
    output.specular.xyz = shadowPcf * specMix * brdfSpecular;
#ifdef AMBIENT_IBL_IN_LIGHT
    output.specular.xyz += ambientSpecular;
#endif
    output.specular.w   = 1.0;

    output.diffuse.xyz = shadowPcf * finalDiffuse;
#ifdef AMBIENT_IBL_IN_LIGHT
    output.diffuse.xyz += ambientDiffuse;
#endif
    output.diffuse.xyz /= 3.0;
    output.diffuse.w   = 0.0;

#ifdef AMBIENT_IBL_IN_LIGHT
#ifdef SKYLIGHTING
    Skylighting::ApplyFullscreenDebug(
        output.diffuse, output.specular, skylighting);
#endif
#endif
    return output;
}

#endif

#if LIGHT_TYPE == LIGHT_TYPE_POINT

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_sss_params;

    float4 cb12_idx29_sss_angles;
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 LightPos_and_Radius;

    float4 LightColor_HDR;

    float4 cb2_idx3_attenuation_curve;

    float4 cb2_pad_4_10[7];

    float4 cb2_lightspace_row0;
    float4 cb2_lightspace_row1;
    float4 cb2_lightspace_row2;
    float4 cb2_lightspace_row3;

};

Texture2D<float4> g_tGbufferAlbedo : register(t0);

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);

Texture2D<float4> g_tMainDepth : register(t3);

Texture2D<float4> g_tLightCookie : register(t7);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);
SamplerState g_sLightCookie     : register(s7);

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    float  scale = sqrt(recon);
    return float3(enc * scale, z);
}

float2 ProjectCookieUV(float3 dirLightSpace, float unprojectedZ)
{
    float3 d = normalize(dirLightSpace);
    bool negativeHemisphere = (unprojectedZ * 0.5 + 0.5) < 0.0;
    d.z += negativeHemisphere ? -1.0 : 1.0;
    d = normalize(d);
    float2 uv = d.xy / d.zz;
    uv = uv * 0.5 + 0.5;
    uv.y = negativeHemisphere ? (1.0 - uv.y) * 0.5 + 0.5 : uv.y * 0.5;
    return uv;
}

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    float2 uvNDC = uv4.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float3 toLight    = LightPos_and_Radius.xyz - posView;
    float  toLightLenSq = dot(toLight, toLight);
    float  d           = sqrt(toLightLenSq);
    float  dNorm       = saturate(d / LightPos_and_Radius.w);
    float  dPowZ       = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float  falloffLin  = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                  + cb2_idx3_attenuation_curve.x);
    float  attenuation = exp2(log2(1.0 - falloffLin) * 2.2);
#ifdef INVERSE_SQUARE_LIGHTING
    attenuation = InverseSquareLighting::GetAttenuation(
        attenuation, d, LightPos_and_Radius.w, input.position.x);
#endif

    bool nearZero = (attenuation <= 0.001);

    if (nearZero)
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(toLightLenSq);

    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

    float roughness01 = 1.0 - matSample.x;

    float posViewLenInv = rsqrt(dot(posView, posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;

    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_sat     = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float albedoW = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
        float skinNdotL = dot(matSample.xyz, lightDir);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity =
            saturate(cb12_idx28_sss_params.z * pow1 + NdotL_sat);
        brdfShadowMix = min(albedoW, sssIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2 =
            exp2(log2(vis2) * cb12_idx28_sss_params.y) *
            cb12_idx28_sss_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
    }
    else
    {
        float specExp = exp2(matSample.x * 10.0 + 1.0);
        float NdotV_raw = dot(viewDirNeg, normalView);
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = lightDir - normalView * NdotL_raw;
        float tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA = roughSq / (roughSq + 0.57);
        float visB = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix = NdotL_sat * visibilityGeom;

        float3 halfVec = lightDir + viewDirNeg;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));

        float distributionNorm = (specExp + 2.0) * 0.159155;
        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, 0.0);
        float minN = min(NdotL_clamped, NdotV_sat);
        float twoNdotH = NdotH + NdotH;
        bool usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm =
            (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= 3.141593;

        brdfSpecular =
            NdotL_clamped * (specMag * LightColor_HDR.xyz);
    }

    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;

    float3 diffuseAccum = LightColor_HDR.xyz * ambientTerm;
    diffuseAccum += LightColor_HDR.xyz * brdfShadowMix;

    float4 posViewHomog = float4(posView, 1.0);
    float3 lsDir;
    lsDir.x = dot(cb2_lightspace_row0, posViewHomog);
    lsDir.y = dot(cb2_lightspace_row1, posViewHomog);
    lsDir.z = dot(cb2_lightspace_row2, posViewHomog);
    float lsW = dot(cb2_lightspace_row3, posViewHomog);
    float2 cookieUV = ProjectCookieUV(lsDir / lsW.xxx, lsDir.z);
    float3 cookieRGB = g_tLightCookie.Sample(g_sLightCookie, cookieUV).xyz;

    diffuseAccum *= cookieRGB;
    float3 specAccum   = cookieRGB * brdfSpecular;

    output.specular.xyz = attenuation * specAccum;
    output.specular.w   = 1.0;
    output.diffuse.xyz  = (attenuation * diffuseAccum) / 3.0;
    output.diffuse.w    = 0.0;

    return output;
}

#endif

#if LIGHT_TYPE == LIGHT_TYPE_SPOT

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

#ifndef ATTENUATION_ONLY

    float4 cb12_idx28_sss_params;

    float4 cb12_idx29_sss_angles;

#endif
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 LightPos_and_Radius;

    float4 LightColor_HDR;

    float4 cb2_idx3_attenuation_curve;

#ifdef SPOT

    float4 cb2_pad_4;

    float4 SpotData;

    float4 cb2_pad_6_10[5];

#  ifdef GOBOPROJECTION

    float4 cb2_gobo_row0;
    float4 cb2_gobo_row1;
    float4 cb2_gobo_row2_unread;
    float4 cb2_gobo_row3;
#  else

    float4 cb2_pad_11_14[4];
#  endif

    float4 cb2_pad_15_19[5];
#endif

#ifdef FO4_PROJECTED_SHADOW_FAMILY

    float4 cb2_pad_4_10[7];

    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;
    float4 cb2_shadowproj_row3;

    float4 cb2_idx15_shadow_sample_param;

    float4 cb2_pad_16_18[3];

    float4 cb2_idx19_shadow_fade;
#endif

    float4 ShadowLightParam;
};

#ifndef ATTENUATION_ONLY

Texture2D<float4> g_tGbufferAlbedo : register(t0);

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);
#endif

Texture2D<float4> g_tMainDepth : register(t3);

#ifdef FO4_PROJECTED_SHADOW_FAMILY
#  if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_POISSON)

Texture2DArray<float4> g_tSpotShadowAtlas : register(t5);
SamplerComparisonState g_sSpotShadowCmp : register(s5);
#  else

Texture2DArray<float4> g_tSpotShadowAtlas : register(t4);
SamplerState g_sSpotShadow : register(s4);
#  endif
#endif

#ifdef GOBOPROJECTION

Texture2D<float4> g_tLightCookie : register(t7);
SamplerState g_sLightCookie : register(s7);
#endif

#ifndef ATTENUATION_ONLY
SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
#endif
SamplerState g_sMainDepth       : register(s3);

#ifdef FILTER_POISSON
#include "Common/ShadowPoissonKernel.hlsli"
#endif

#ifndef ATTENUATION_ONLY

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  recon = 1.0 - encLenSq * 0.25;
    float  z = 1.0 - encLenSq * 0.5;
    float  scale = sqrt(recon);
#ifdef SPECULAR
    float3 normal;
    normal.xy = enc * scale;
    normal.z = -z;
    return normal;
#else
    return float3(enc * scale, -z);
#endif
}
#endif

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    float linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 uvScreen = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float2 uvNDC = uvScreen * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float3 toLight      = LightPos_and_Radius.xyz - posView;
    float  toLightLenSq = dot(toLight, toLight);
    float  d            = sqrt(toLightLenSq);
#ifdef SPOT
    float3 lightDir = toLight * rsqrt(toLightLenSq);
#endif
    float  dNorm        = saturate(d / LightPos_and_Radius.w);
    float  dPowZ        = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float  falloffLin   = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                   + cb2_idx3_attenuation_curve.x);
    float  attenuation  = exp2(log2(1.0 - falloffLin) * 2.2);
#ifdef INVERSE_SQUARE_LIGHTING
    attenuation = InverseSquareLighting::GetAttenuation(
        attenuation, d, LightPos_and_Radius.w, input.position.x);
#endif

#ifdef SPOT

    float coneCos   = saturate(dot(-lightDir, SpotData.xyz));
#  ifdef FO4_DEFERRED_SPOT_NATIVE_CONE
    float coneNumerator = 1.0 - coneCos;
    float coneDenom = 1.0 - SpotData.w;
    float coneT = coneNumerator / coneDenom;
#  else
    float coneDenom = 1.0 - SpotData.w;
    float coneT     = (1.0 - coneCos) / coneDenom;
#  endif
    float coneEdge  = saturate(1.0 - coneT);
    float coneFall  = exp2(log2(coneEdge) * ShadowLightParam.x);
    coneFall = min(coneFall, 1.0);

#  ifdef FO4_DEFERRED_SPOT_NATIVE_CONE
    attenuation = attenuation * coneFall;
#  else
    attenuation = coneFall * attenuation;
#  endif
#endif

    bool nearZero = (attenuation <= 0.001);

    if (nearZero)
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

#ifdef FO4_PROJECTED_SHADOW_FAMILY

    float3 lightDir = toLight * rsqrt(toLightLenSq);
#endif

#ifndef ATTENUATION_ONLY

    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

    float roughness01 = 1.0 - matSample.x;
#endif

#ifndef FO4_DEFERRED_BRANCH_LOCAL_DIFFUSE

    float posViewLenInv = rsqrt(dot(-posView, -posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;
#endif

#ifdef FO4_PROJECTED_SHADOW_FAMILY
    float4 posViewHomog = float4(posView, 1.0);
#  ifndef FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH
    float4 shadowCoord;
#  endif
    float shadowRef;
    float shadowFactor;

#  if defined(POINTOMNI) && defined(SHADOW)
    float3 shadowProj;
#    ifdef FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH
    shadowProj.z = dot(cb2_shadowproj_row2, posViewHomog);
    float zHalf = shadowProj.z * 0.5 + 0.5;
    bool halfAccepted = (zHalf >= 0.0);
    if (halfAccepted)
    {
    float4 shadowCoord;
    shadowProj.x = dot(cb2_shadowproj_row0, posViewHomog);
    shadowProj.y = dot(cb2_shadowproj_row1, posViewHomog);
    float shadowProjW = dot(cb2_shadowproj_row3, posViewHomog);
#    else
    shadowProj.x = dot(cb2_shadowproj_row0, posViewHomog);
    shadowProj.y = dot(cb2_shadowproj_row1, posViewHomog);
    shadowProj.z = dot(cb2_shadowproj_row2, posViewHomog);
    float shadowProjW = dot(cb2_shadowproj_row3, posViewHomog);
    float zHalf = shadowProj.z * 0.5 + 0.5;
#    endif
    shadowProj /= shadowProjW.xxx;

#    ifdef FO4_DEFERRED_HALFOMNI_GOBO_NATIVE_BRANCH
    float shadowProjLenSq = dot(shadowProj, shadowProj);
    float shadowProjInvLen = rsqrt(shadowProjLenSq);
    bool halfAccepted = (zHalf >= 0.0);
    if (halfAccepted)
    {
    float radial = sqrt(shadowProjLenSq);
    float3 paraboloid = normalize(
        shadowProj * shadowProjInvLen + float3(0.0, 0.0, 1.0));
    float2 omniUV = paraboloid.xy / paraboloid.zz;
    shadowRef = saturate(radial / LightPos_and_Radius.w);
    omniUV = omniUV * 0.5 + 0.5;
    float selectedY = 1.0 - omniUV.y;
#      ifdef FO4_DEFERRED_HALFOMNI_POISSON_LIVE_MASK
    shadowCoord.xz = float2(omniUV.x, selectedY) * ShadowLightParam.zz;
#      else
    shadowCoord.x = omniUV.x * ShadowLightParam.z;
#      endif
#      ifdef FILTER_POISSON
    float poissonScale = cb2_idx15_shadow_sample_param.z * 3.0;
#      endif
    shadowRef -= cb2_idx15_shadow_sample_param.x;
#      ifdef FO4_DEFERRED_HALFOMNI_POISSON_LIVE_MASK
    shadowCoord.z = 1.0 - shadowCoord.z;
#      else
    shadowCoord.z = 1.0 - selectedY * ShadowLightParam.z;
#      endif
    shadowCoord.w = 0.0;
#    else
    float radial = length(shadowProj);
    float3 normalizedShadowProj = normalize(shadowProj);
#    if defined(HALFOMNI) && !defined(FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH)

    bool halfAccepted = (zHalf >= 0.0);
float3 pole = float3(0.0, 0.0, 1.0);
#    elif defined(FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH)
float3 pole = float3(0.0, 0.0, 1.0);
#    else
bool backHemisphere = (zHalf < 0.0);
float3 pole = backHemisphere ? float3(0.0, 0.0, -1.0)
                             : float3(0.0, 0.0, 1.0);
#    endif
float3 paraboloid = normalize(normalizedShadowProj + pole);
#    ifdef FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH
shadowCoord.xy = paraboloid.xy / paraboloid.zz;
shadowRef = saturate(radial / LightPos_and_Radius.w);
shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
float selectedY = 1.0 - shadowCoord.y;
#      ifdef FO4_DEFERRED_HALFOMNI_POISSON_LIVE_MASK
shadowCoord.xz = float2(shadowCoord.x, selectedY) * ShadowLightParam.zz;
#      else
shadowCoord.x *= ShadowLightParam.z;
#      endif
#      ifdef FILTER_POISSON
float poissonScale = cb2_idx15_shadow_sample_param.z * 3.0;
#      endif
#      ifndef FO4_DEFERRED_RAW_BASE
shadowRef -= cb2_idx15_shadow_sample_param.x;
#      endif
#      ifdef FO4_DEFERRED_HALFOMNI_POISSON_LIVE_MASK
shadowCoord.z = 1.0 - shadowCoord.z;
#      else
shadowCoord.z = 1.0 - selectedY * ShadowLightParam.z;
#      endif
shadowCoord.w = 0.0;
#    else
float2 omniUV = paraboloid.xy / paraboloid.zz;
shadowRef = saturate(radial / LightPos_and_Radius.w);
omniUV = omniUV * 0.5 + 0.5;

#    ifndef HALFOMNI
bool frontHemisphere = (zHalf >= 0.0);
#    endif
#    ifdef HALFOMNI
float selectedY = omniUV.y;
shadowCoord.x = omniUV.x * ShadowLightParam.z;
shadowCoord.z = 1.0 - selectedY * ShadowLightParam.z;
shadowCoord.w = 0.0;
#    else
float mirroredY = 1.0 - omniUV.y;
float selectedY = frontHemisphere ? mirroredY : omniUV.y;
float2 scaledUV = float2(omniUV.x, selectedY) * ShadowLightParam.z;
#    ifdef FO4_DEFERRED_OMNI_NATIVE_PACK
selectedY = 1.0 - selectedY * ShadowLightParam.z;
float finalY = frontHemisphere ? selectedY : scaledUV.y;
#    else
float alternateY = 1.0 - selectedY * ShadowLightParam.z;
float finalY = frontHemisphere ? alternateY : scaledUV.y;
#    endif
shadowCoord.x = scaledUV.x;
float2 backCoord = float2(1.0 - finalY, 1.0);
float2 frontCoord = float2(finalY, 0.0);
shadowCoord.zw = backHemisphere ? backCoord : frontCoord;
#    endif
#    endif
#    if defined(FILTER_POISSON) \
        && !defined(FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH) \
        && !defined(FO4_DEFERRED_HALFOMNI_GOBO_NATIVE_BRANCH)
    float poissonScale = cb2_idx15_shadow_sample_param.z * 3.0;
#    endif
#    if !defined(FO4_DEFERRED_RAW_BASE) \
        && !defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
        && !defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
        && !defined(FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH)
shadowRef -= cb2_idx15_shadow_sample_param.x;
#    endif
#    endif
#  else

    float3 shadowProj;
    shadowProj.x = dot(cb2_shadowproj_row0, posViewHomog);
    shadowProj.y = dot(cb2_shadowproj_row1, posViewHomog);
    shadowProj.z = dot(cb2_shadowproj_row2, posViewHomog);
    float shadowProjW = dot(cb2_shadowproj_row3, posViewHomog);
    shadowProj /= shadowProjW.xxx;

    shadowCoord.xz = shadowProj.xy * 0.5 + 0.5;
    shadowCoord.w = 0.0;
#    ifdef FILTER_POISSON
    float poissonScale = cb2_idx15_shadow_sample_param.z * 3.0;
#    endif
#    if defined(FO4_DEFERRED_RAW_POINTSPOT_BASE) \
        || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE)
    shadowRef = shadowProj.z;
#    else
    shadowRef = shadowProj.z - cb2_idx15_shadow_sample_param.x;
#    endif
#  endif

#  if defined(FILTER_POISSON)

    float poissonSum = 0.0;
    [loop]
    for (int p = 0; p < 8; ++p)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[p * 2] - 0.5) * poissonScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[p * 2 + 1] - 0.5) * poissonScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowCoord.xzxz;
        float partial = poissonSum + g_tSpotShadowAtlas.SampleCmpLevelZero(
            g_sSpotShadowCmp, float3(tapUV.xy, shadowCoord.w), shadowRef);
        poissonSum = partial + g_tSpotShadowAtlas.SampleCmpLevelZero(
            g_sSpotShadowCmp, float3(tapUV.zw, shadowCoord.w), shadowRef);
    }
    shadowFactor = poissonSum * 0.0625;
#  elif defined(FILTER_PCF9)

    float pcfSum = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
#    if defined(ATTENUATION_ONLY) \
        || defined(FO4_DEFERRED_PCF9_OMNI_IGNORE_ROUGHNESS) \
        || defined(FO4_DEFERRED_PROJECTED_IGNORE_PAIR) \
        || defined(FO4_DEFERRED_PCF9_OMNI_BASE) \
        || defined(FO4_DEFERRED_PCF9_OMNI_GOBO_BASE) \
        || defined(FO4_DEFERRED_PCF9_POINTSPOT_BASE) \
        || defined(FO4_DEFERRED_PCF9_POINTSPOT_GOBO_BASE) \
        || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_IGNORE) \
        || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_IGNORE) \
        || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_IGNORE) \
        || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_IGNORE) \
        || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
        || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
        || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
        || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
        || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
        || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
        || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE)
            float tapI = (float)i - 1.0;
            float tapJ = (float)j - 1.0;
            float2 tapOffset = float2(tapI, tapJ)
                             * cb2_idx15_shadow_sample_param.zw;
#    else
            float2 tapOffset = float2(i - 1, j - 1)
                             * cb2_idx15_shadow_sample_param.zw;
#    endif
            pcfSum += g_tSpotShadowAtlas.SampleCmpLevelZero(
                g_sSpotShadowCmp, float3(tapOffset + shadowCoord.xz, shadowCoord.w), shadowRef);
        }
    }
    shadowFactor = pcfSum / 9.0;
#  elif defined(FILTER_PCF1)
    shadowFactor = g_tSpotShadowAtlas.SampleCmpLevelZero(
        g_sSpotShadowCmp, shadowCoord.xzw, shadowRef);
#  else

    float shadowDepth = g_tSpotShadowAtlas.Sample(
        g_sSpotShadow, shadowCoord.xzw).x;
#    if defined(FO4_DEFERRED_RAW_BASE) \
        || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
        || defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE)
    shadowRef -= cb2_idx15_shadow_sample_param.x;
#    endif
    shadowFactor = (shadowDepth >= shadowRef) ? 1.0 : 0.0;
#  endif

#  ifdef FO4_DEFERRED_HALFOMNI_GOBO_NATIVE_BRANCH
    }
    else
    {
        shadowFactor = 0.0;
    }
#  elif defined(FO4_DEFERRED_HALFOMNI_NATIVE_BRANCH)
    }
    else
    {
        shadowFactor = 0.0;
    }
#  elif defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI)
    shadowFactor = halfAccepted ? shadowFactor : 0.0;
#  endif

#  if !(defined(POINTOMNI) && defined(SHADOW))

    float2 projFade = (float2(shadowCoord.x, 1.0 - shadowCoord.z) - ShadowLightParam.y)
                    / ShadowLightParam.z;
    float  projDist = sqrt(dot(projFade, projFade));
    float  edgeFall = exp2(log2(projDist) * ShadowLightParam.x);
    edgeFall = min(edgeFall, 1.0);
    shadowFactor *= (1.0 - edgeFall);
#  endif

    float shadowDistNorm = saturate(dot(posView, posView)
                                    / cb2_idx19_shadow_fade.x);
    float shadowDist2 = shadowDistNorm * shadowDistNorm;
    float shadowDist4 = shadowDist2 * shadowDist2;
#  if (defined(ATTENUATION_ONLY) \
       && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
           || defined(FILTER_POISSON))) \
      || defined(FO4_DEFERRED_PCF9_OMNI_IGNORE_ROUGHNESS) \
      || defined(FO4_DEFERRED_PCF1_BASE) \
      || defined(FO4_DEFERRED_RAW_BASE) \
      || defined(FO4_DEFERRED_PCF1_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_IGNORE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_IGNORE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
    float shadowDistanceFactor = 1.0 - shadowDist4 * shadowDist4;
    shadowFactor = shadowDistanceFactor * shadowFactor;
#  else
    shadowFactor *= (1.0 - shadowDist4 * shadowDist4);
#  endif

#  if (defined(ATTENUATION_ONLY) \
       && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
           || defined(FILTER_POISSON))) \
       || defined(FO4_DEFERRED_PROJECTED_IGNORE_PAIR) \
       || defined(FO4_DEFERRED_PCF9_OMNI_IGNORE_ROUGHNESS) \
       || defined(FO4_DEFERRED_PCF1_BASE) \
       || defined(FO4_DEFERRED_RAW_BASE) \
       || defined(FO4_DEFERRED_PCF1_SPEC_IGNORE) \
       || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
       || defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
       || defined(FO4_DEFERRED_PCF9_OMNI_BASE) \
       || defined(FO4_DEFERRED_PCF9_OMNI_GOBO_BASE) \
       || defined(FO4_DEFERRED_PCF9_POINTSPOT_BASE) \
       || defined(FO4_DEFERRED_PCF9_POINTSPOT_GOBO_BASE) \
       || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_IGNORE) \
       || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_IGNORE) \
       || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_IGNORE) \
       || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_IGNORE) \
       || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
       || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
       || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
       || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
       || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
       || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
       || defined(FO4_DEFERRED_POISSON_POINTSPOT_BASE) \
       || defined(FO4_DEFERRED_POISSON_OMNI_BASE) \
       || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_IGNORE) \
       || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_IGNORE) \
       || defined(FO4_DEFERRED_POISSON_POINTSPOT_GOBO_BASE) \
       || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE) \
       || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
       || defined(FO4_DEFERRED_HALFOMNI_POISSON_BASE) \
       || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE) \
       || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_BASE) \
       || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
       || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
    attenuation = shadowFactor * attenuation;
#  else
    attenuation = attenuation * shadowFactor;
#  endif
#endif

#ifndef ATTENUATION_ONLY
#  ifndef FO4_DEFERRED_BRANCH_LOCAL_NDOTL
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_sat     = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);
#  endif

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float albedoW = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
#  ifdef FO4_DEFERRED_BRANCH_LOCAL_DIFFUSE
        float posViewLenInv = rsqrt(dot(-posView, -posView));
        float3 viewDirNeg = -posView * posViewLenInv.xxx;
#  endif
        float skinNdotL = dot(matSample.xyz, lightDir);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));
#  ifdef FO4_DEFERRED_BRANCH_LOCAL_NDOTL
        float NdotL_sat = max(dot(normalView, lightDir), 0.0);
#  endif

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
#  if defined(FO4_DEFERRED_BRANCH_LOCAL_NDOTL) \
      || defined(FO4_DEFERRED_SPEC_ORDER)
        float vis1 = max(rot1 * skinNdotV + rot1Perp * sinScaleV, 0.0);
#  else
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
#  endif
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity =
            saturate(cb12_idx28_sss_params.z * pow1 + NdotL_sat);
        brdfShadowMix = min(albedoW, sssIntensity);

#  ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
#    ifdef FO4_DEFERRED_SPEC_ORDER
        float vis2 = max(rot2 * skinNdotV + rot2Perp * sinScaleV, 0.0);
#    else
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
#    endif
        float pow2 =
            exp2(log2(vis2) * cb12_idx28_sss_params.y) *
            cb12_idx28_sss_params.x;

#    ifdef FO4_DEFERRED_SPEC_ORDER
        brdfSpecular = (pow2 * LightColor_HDR.xyz) * NdotL_clamped;
#    else
        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
#    endif
#  endif
    }
    else
    {
#  if defined(FO4_DEFERRED_SPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_SPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF1_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
      || defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE) \
      || defined(FO4_DEFERRED_POISSON_OMNI_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
      || defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
        float specExp = exp2(matSample.x * 10.0 + 1.0);
#  endif
#  ifdef IGNOREROUGHNESS

        brdfShadowMix = max(dot(lightDir, normalView), 0.0);
#  else
#    ifdef FO4_DEFERRED_NATIVE_ROUGHNESS_BRDF
        float NdotV_raw = dot(viewDirNeg, normalView);
        float NdotL_raw = dot(lightDir, normalView);
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = lightDir - normalView * NdotL_raw;
        float tangentVLDot = dot(tangentV, tangentL);

        float roughSq = roughness01 * roughness01;
        float2 visAB =
            roughSq.xx / (roughSq.xx + float2(0.57, 0.09));
        float visA = visAB.x;
        float visB = visAB.y;
        visA = 1.0 - 0.5 * visA;
        visB *= 0.45;

        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float tangentDenom = max(NdotV_raw, NdotL_raw);
        float tangentRatio = tangentSin / tangentDenom;
#    if defined(FO4_DEFERRED_SPOT_BASE) \
        || defined(FO4_DEFERRED_SPOT_GOBO_BASE)
        float NdotL_sat = max(NdotL_raw, 0.0);
        float tangentVL = max(tangentVLDot, 0.0);
#    else
        float tangentVL = max(tangentVLDot, 0.0);
        float NdotL_sat = max(NdotL_raw, 0.0);
#    endif
        float visibilityGeom = visB * tangentVL;
        visibilityGeom = visibilityGeom * tangentRatio + visA;
        brdfShadowMix = NdotL_sat * visibilityGeom;
#    else
        float NdotV_raw = dot(viewDirNeg, normalView);
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = lightDir - normalView * NdotL_raw;
        float tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA = roughSq / (roughSq + 0.57);
        float visB = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix = NdotL_sat * visibilityGeom;
#    endif
#  endif

#  ifdef SPECULAR
#    if !defined(FO4_DEFERRED_SPOT_SPEC_BASE) \
        && !defined(FO4_DEFERRED_SPOT_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_PCF1_OMNI_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_PCF1_POINTSPOT_SPEC_BASE) \
        && !defined(FO4_DEFERRED_PCF1_OMNI_SPEC_BASE) \
        && !defined(FO4_DEFERRED_RAW_POINTSPOT_SPEC_BASE) \
        && !defined(FO4_DEFERRED_RAW_OMNI_SPEC_BASE) \
        && !defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_BASE) \
        && !defined(FO4_DEFERRED_PCF9_OMNI_SPEC_BASE) \
        && !defined(FO4_DEFERRED_PCF9_OMNI_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_BASE) \
        && !defined(FO4_DEFERRED_HALFOMNI_PCF1_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_BASE) \
        && !defined(FO4_DEFERRED_HALFOMNI_PCF9_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_PCF9_POINTSPOT_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_POISSON_OMNI_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_GOBO_BASE) \
        && !defined(FO4_DEFERRED_POISSON_POINTSPOT_SPEC_BASE) \
        && !defined(FO4_DEFERRED_POISSON_OMNI_SPEC_BASE) \
        && !defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_BASE) \
        && !defined(FO4_DEFERRED_HALFOMNI_POISSON_SPEC_GOBO_BASE)
        float specExp = exp2(matSample.x * 10.0 + 1.0);
#    endif

#    ifdef FO4_DEFERRED_SPEC_ORDER
        float3 halfVec = lightDir - posView * posViewLenInv;
#    else
        float3 halfVec = lightDir + viewDirNeg;
#    endif
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_spec = dot(viewDirNeg, normalView);
        float NdotV_sat = saturate(NdotV_spec);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));

#    ifdef FO4_DEFERRED_SPEC_ORDER
        float distributionNorm = (specExp + 2.0) * 0.15915494;
#    else
        float distributionNorm = (specExp + 2.0) * 0.159155;
#    endif
        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, asfloat(0x34000000));
#    ifdef FO4_DEFERRED_SPEC_ORDER
        float minN = min(NdotV_sat, NdotL_clamped);
#    else
        float minN = min(NdotL_clamped, NdotV_sat);
#    endif
        float twoNdotH = NdotH + NdotH;
#    ifdef FO4_DEFERRED_SPEC_ORDER
        bool usePeakRatio = (VdotH_nonneg >= minN * twoNdotH);
        bool useUnityRatio = (minN == NdotV_sat);
#    else
        bool usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool useUnityRatio = (NdotV_sat == minN);
#    endif
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
#    ifdef FO4_DEFERRED_SPEC_ORDER
        float visibility = (ratio * twoNdotH) / VdotH_nonneg;
#    else
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
#    endif
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm =
            (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

#    ifdef FO4_DEFERRED_SPEC_ORDER
        float specMag = fresnelTerm * visibility;
        specMag *= distributionNorm;
#    else
        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
#    endif
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= 3.1415927;

#    ifdef FO4_DEFERRED_SPEC_ORDER
        brdfSpecular = (specMag * LightColor_HDR.xyz) * NdotL_clamped;
#    else
        brdfSpecular = NdotL_clamped * (specMag * LightColor_HDR.xyz);
#    endif
#  endif
    }

#  ifdef FO4_DEFERRED_BASE_ROUGHNESS
    float NdotL_raw = dot(normalView, lightDir);
    float NdotL_clamped = saturate(NdotL_raw);
#  endif
#  ifndef FO4_DEFERRED_PACKED_RIM
    float3 diffuseAccum = LightColor_HDR.xyz * brdfShadowMix;
#  endif

#  if !defined(IGNORERIM) && !defined(IGNOREROUGHNESS)

    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
#    ifdef FO4_DEFERRED_PACKED_RIM
    float2 rimTerms = float2(edge, toLightDotView);
    float ambientTerm =
        rimTerms.x * rimTerms.y * NdotL_clamped * roughness01;
#    else
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;
#    endif
#    ifdef FO4_DEFERRED_PACKED_RIM
    float3 diffuseAccum = LightColor_HDR.xyz * brdfShadowMix;
    diffuseAccum += LightColor_HDR.xyz * ambientTerm;
#    else
    diffuseAccum = LightColor_HDR.xyz * ambientTerm + diffuseAccum;
#    endif
#  endif
#else

    float3 diffuseAccum = LightColor_HDR.xyz;
    float3 brdfSpecular = float3(0, 0, 0);
#endif

#ifdef FO4_DEFERRED_HALFOMNI_GOBO_NATIVE_BRANCH
    bool halfRejected = (zHalf < 0.0);
    diffuseAccum = halfRejected ? float3(0.0, 0.0, 0.0) : diffuseAccum;
#endif

#ifdef GOBOPROJECTION
#  ifdef SPOT

    float4 goboHomog = float4(posView, 1.0);
    float2 goboProj;
    goboProj.x = dot(cb2_gobo_row0, goboHomog);
    goboProj.y = dot(cb2_gobo_row1, goboHomog);
    float goboW = dot(cb2_gobo_row3, goboHomog);
    float2 goboUV = (goboProj / goboW.xx) * 0.5 + 0.5;
#  elif defined(POINTOMNI) && defined(SHADOW)
#    ifdef FO4_DEFERRED_HALFOMNI_GOBO_NATIVE_BRANCH
    float3 goboParaboloid = normalize(
        shadowProj * shadowProjInvLen + float3(0.0, 0.0, 1.0));
    float2 goboUV = (goboParaboloid.xy / goboParaboloid.zz) * 0.5 + 0.5;
#    elif defined(HALFOMNI)

    float2 goboUV = omniUV;
#    else

#      ifdef FO4_DEFERRED_OMNI_NATIVE_PACK
    float backGoboY = mirroredY * 0.5 + 0.5;
    float frontGoboY = omniUV.y * 0.5;
    float goboY = backHemisphere ? backGoboY : frontGoboY;
    float2 goboUV = float2(omniUV.x, goboY);
#      else
    float2 goboUV = float2(omniUV.x,
                           backHemisphere ? 1.0 - 0.5 * omniUV.y
                                          : 0.5 * omniUV.y);
#      endif
#    endif
#  else

#    ifdef FO4_DEFERRED_POINTSPOT_NATIVE_GOBO
    float goboCenter = ShadowLightParam.y - 0.5;
    float2 goboUV =
        shadowProj.xy - goboCenter.xx * float2(2.0, -2.0);
    float goboScale = ShadowLightParam.z + ShadowLightParam.z;
    goboUV /= goboScale;
    goboUV = goboUV * 0.5 + 0.5;
#    else
    float2 goboUV = float2(projFade.x, -projFade.y) * 0.5 + 0.5;
#    endif
#  endif
    float3 cookieRGB = g_tLightCookie.Sample(g_sLightCookie, goboUV).xyz;

    diffuseAccum *= cookieRGB;
    brdfSpecular *= cookieRGB;
#endif

#if defined(WETNESS_EFFECTS) && !defined(ATTENUATION_ONLY)
    float wetness = WetnessEffects::GetWetness(
        normalView,
        float4(ViewToWorld_row2.xyz, 1.0));
    float3 wetViewDir = -posView * rsqrt(dot(posView, posView));
    float3 wetLightColor = LightColor_HDR.xyz * attenuation;
#  ifdef GOBOPROJECTION
    wetLightColor *= cookieRGB;
#  endif
    float3 wetDiffuse = attenuation * diffuseAccum;
    float3 wetSpecular = attenuation * brdfSpecular;
    WetnessEffects::ApplyDirectCoat(
        normalView,
        wetViewDir,
        lightDir,
        wetLightColor,
        wetness,
        wetDiffuse,
        wetSpecular);
    output.specular = float4(wetSpecular, 1.0);
    output.diffuse = float4(wetDiffuse, 0.0);
    output.diffuse /= 3.0;
#else
    output.specular.xyz = attenuation * brdfSpecular;
    output.specular.w   = 1.0;
#if (defined(ATTENUATION_ONLY) \
     && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
         || defined(FILTER_POISSON))) \
    || defined(FO4_DEFERRED_BRANCH_LOCAL_DIFFUSE) \
    || defined(FO4_DEFERRED_SPOT_IGNORE_RIM) \
    || defined(FO4_DEFERRED_BASE_ROUGHNESS) \
    || defined(FO4_DEFERRED_SPEC_ORDER)
    output.diffuse = float4(diffuseAccum, 0.0);
    output.diffuse *= attenuation;
    output.diffuse /= 3.0;
#else
    output.diffuse.xyz = attenuation * diffuseAccum;
    output.diffuse.w = 0.0;
    output.diffuse /= asfloat(0x40400000);
#endif
#endif

    return output;
}

#endif

#endif

#ifdef BSDFLIGHT_PS_ATTENUATION_ONLY

#if !defined(POINTOMNI) || POINTOMNI != 1
#  error "define POINTOMNI=1 for this source"
#endif
#if !defined(ATTENUATION_ONLY) || ATTENUATION_ONLY != 1
#  error "define ATTENUATION_ONLY=1 for this source"
#endif
#if !defined(RGBSPEC) || RGBSPEC != 1
#  error "define RGBSPEC=1 for this source"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 2
#  error "this source reconstructs DIRSPLITS=2 only"
#endif
#if defined(LIGHT_TYPE) || defined(DIRECTIONAL) || defined(POINTSPOT) \
    || defined(SPOT) || defined(SHADOW) || defined(SHADOW_ONLY) \
    || defined(SPECULAR) || defined(AMBIENT) || defined(BLENDSPLIT) \
    || defined(OVERDRAW) \
    || defined(CHARACTER_LIGHT) \
    || defined(AMBIENT_IBL_IN_LIGHT) \
    || defined(IGNORERIM) || defined(IGNOREROUGHNESS) \
    || defined(GOBOPROJECTION) || defined(HALFOMNI) \
    || defined(FILTER_PCF1) || defined(FILTER_PCF9) \
    || defined(FILTER_PCSS) || defined(FILTER_POISSON) \
    || defined(FILTER_PCSSPOISSON)
#  error "unsupported macro for the POINTOMNI attenuation-only family"
#endif

#include "Common/DeferredContracts.hlsli"

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 LightPos_and_Radius;
    float4 LightColor_HDR;
    float4 LightAttenuation;
};

Texture2D<float4> g_tMainDepth : register(t3);
SamplerState g_sMainDepth : register(s3);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float depth = g_tMainDepth.SampleGrad(
        g_sMainDepth,
        uv4.xy,
        ddx_coarse(uv4.x).xx,
        ddy_coarse(uv4.y).xx).x;

    float linearizedDepth;
    float4 reprojRow0;
    float4 reprojRow1;
    float4 reprojRow2;
    float4 reprojRow3;
    if (depth <= asfloat(0x3c23d70a))
    {
        linearizedDepth = depth * asfloat(0x42c80000);
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth =
            depth * asfloat(0x3f8147ae) + asfloat(0xbc23d70a);
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 screen = uv4.zw * float2(
        asfloat(0x3f800000),
        asfloat(0xbf800000)) + float2(
        asfloat(0x00000000),
        asfloat(0x3f800000));
    float2 ndc = screen * asfloat(0x40000000) - asfloat(0x3f800000);
    float4 position = float4(ndc, linearizedDepth, asfloat(0x3f800000));

    float4 positionViewH;
    positionViewH.x = dot(reprojRow0, position);
    positionViewH.y = dot(reprojRow1, position);
    positionViewH.z = dot(reprojRow2, position);
    positionViewH.w = dot(reprojRow3, position);
    float3 positionView = positionViewH.xyz / positionViewH.www;

    float3 delta = LightPos_and_Radius.xyz - positionView;
    float distance = length(delta);
    float normalizedDistance = saturate(distance / LightPos_and_Radius.w);
    float falloff = pow(normalizedDistance, LightAttenuation.z);
    float biased = saturate(
        LightAttenuation.y * falloff + LightAttenuation.x);
    float attenuation = pow(
        asfloat(0x3f800000) - biased,
        asfloat(0x400ccccd));
#ifdef INVERSE_SQUARE_LIGHTING
    attenuation = InverseSquareLighting::GetAttenuation(
        attenuation, distance, LightPos_and_Radius.w, input.position.x);
#endif

    if (attenuation <= asfloat(0x3a83126f))
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    output.diffuse = float4(LightColor_HDR.xyz, 0.0);
    output.diffuse *= attenuation;
    output.diffuse /= 3.0;
    output.specular = float4(0, 0, 0, asfloat(0x3f800000));
    return output;
}
#endif

#ifdef BSDFLIGHT_PS_DIRSPLITS1

#if !defined(DIRECTIONAL)
#  error "this source is the native DIRECTIONAL family; define DIRECTIONAL"
#endif
#if defined(POINTOMNI) || defined(POINTSPOT) || defined(SPOT) || defined(HALFOMNI)
#  error "DIRECTIONAL is exclusive with the punctual light families"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the shadow-term family in bsdf_light_deferred_shadow_only.hlsl"
#endif
#ifndef SHADOW
#  error "the reconstructed DIRSPLITS=1 full-BRDF family is SHADOW only"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the split count is a native axis and is never assumed"
#endif
#if DIRSPLITS != 1
#  error "this source reconstructs DIRSPLITS=1 only; 2 and 3 are separate native families"
#endif
#if !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native DIRSPLITS=1 full-BRDF blob carries both SPECULAR and RGBSPEC"
#endif
#if defined(BLENDSPLIT)
#  error "DIRSPLITS=1 does not support BLENDSPLIT"
#endif
#if defined(IGNOREROUGHNESS) || defined(IGNORERIM)
#  error "no DIRSPLITS=1 full-BRDF blob carries IGNOREROUGHNESS or IGNORERIM"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_PCSSPOISSON) + defined(FILTER_POISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif
#ifdef FILTER_PCSSPOISSON
#  error "FILTER_PCSSPOISSON is not a DIRSPLITS=1 full-BRDF permutation"
#endif
#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_PCSS) \
    && !defined(FILTER_POISSON)
#  error "DIRSPLITS=1 requires one FILTER_* macro"
#endif

#include "Common/DeferredContracts.hlsli"

#ifdef FILTER_POISSON
#include "Common/ShadowPoissonKernel.hlsli"
#endif

#if defined(FILTER_PCSS) || defined(FILTER_POISSON)
#  define FO4_DS1_USES_WORLD_SCALE 1
#endif

#if defined(FO4_DS1_USES_WORLD_SCALE) && !defined(AMBIENT)
#  define FO4_DS1_TIGHT_WORLD_SCALE 1
#endif
#if defined(FO4_DS1_USES_WORLD_SCALE) && defined(AMBIENT)
#  define FO4_DS1_AMBIENT_WORLD_SCALE 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_hair_spec_params;

    float4 cb12_idx29_hair_spec_shifts;

    float4 cb12_idx30;

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 cb12_pad_31_34[4];
    float4 CameraPosAdjust;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 SunDirection;

    float4 SunColor_HDR;

#ifdef AMBIENT

    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#else
    float4 cb2_pad_3_8[6];
#endif

    float4 cb2_idx9_cascade_slice;

    float4 cb2_pad_10;

    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;

    float4 cb2_pad_14_19[6];

    float4 cb2_idx20_shadow_sample_param;

    float4 cb2_idx21_cascade_world_scale[3];

    float4 cb2_idx24_distance_fade;
};

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

#ifdef FILTER_PCSS
Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
#endif
Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

#ifdef FILTER_PCSS
SamplerState g_sCascadeShadowRaw : register(s4);
#endif
SamplerComparisonState g_sCascadeShadowCmp : register(s5);

static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.1415927;

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  recon = 1.0 - encLenSq * 0.25;
    float  z = 1.0 - encLenSq * 0.5;
    float  scale = sqrt(recon);
    return float3(enc * scale, -z);
}

#ifdef AMBIENT
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(cb2_ambient_gradient_row0, directionH);
    encoded.y = dot(cb2_ambient_gradient_row1, directionH);
    encoded.z = dot(cb2_ambient_gradient_row2, directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

float ComputeCascadeShadow(float3 posView, float slice)
{
    float4 posViewH = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_cascade0_row0, posViewH);
    shadowUV.y = dot(cb2_cascade0_row1, posViewH);
    float shadowZ = dot(cb2_cascade0_row2, posViewH);

#ifdef FILTER_POISSON
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif

#ifdef FO4_DS1_TIGHT_WORLD_SCALE
    uint cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
#endif
#ifdef FO4_DS1_AMBIENT_WORLD_SCALE
    uint cascade = (uint)slice;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
#endif

#if defined(FILTER_PCF1)

    return g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)

    float sum = 0.0;
    [loop]
    for (int i = 0; i < 3; ++i)
    {
        float offsetX = float(i) - 1.0;
        [loop]
        for (int j = 0; j < 3; ++j)
        {
            float offsetY = float(j) - 1.0;
            float2 tapUV = float2(offsetX, offsetY)
                * cb2_idx20_shadow_sample_param.zw + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * (1.0 / 9.0);

#elif defined(FILTER_PCSS)
    float2 searchStep = 1.0 / cascadeScale.xy;
    float2 blocker = 0.0;
    [loop]
    for (int bx = 0; bx < 5; ++bx)
    {
        float offsetX = float(bx - 2);
        [loop]
        for (int by = 0; by < 5; ++by)
        {
            float offsetY = float(by - 2);
            float2 tapUV = float2(offsetX, offsetY) * searchStep + shadowUV;
            float tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool isBlocker = tapDepth < shadowZ;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y == 0.0)
        return 1.0;

    float centerDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float sum = centerDepth >= shadowZ ? 1.0 : 0.0;

    float averageBlocker = blocker.x / blocker.y;
    float worldRange = cascadeScale.w - cascadeScale.z;
    float receiverWorld = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
    float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra = blockerWorld < cascadeScale.z + 0.001
        ? 1.9
        : separation * 1.8 + 0.1;

    [loop]
    for (int fx = 0; fx < 5; ++fx)
    {
        float offsetX = penumbra * (float(fx) - 2.0);
        [loop]
        for (int fy = 0; fy < 5; ++fy)
        {
            float offsetY = penumbra * (float(fy) - 2.0);
            float2 tapUV = searchStep * float2(offsetX, offsetY) * 0.5 + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * 0.04;

#else

    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - rcpWorldRange * 0.275;

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), zRef);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), zRef);
    }
    return sum * 0.0625;
#endif
}

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    float  linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 uvScreen = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float2 uvNDC = uvScreen * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float4 albedoSample = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

    float roughness01   = 1.0 - matSample.x;
    float posViewLenSq  = dot(-posView, -posView);
    float posViewLen    = rsqrt(posViewLenSq);
    float3 viewDirNeg   = -posView * posViewLen;

#ifdef AMBIENT
    float3 ambientDiffuse  = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float ambientFresLog = log2(1.0 - NdotV_view);
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif

    float slice = cb2_idx9_cascade_slice.y;

    float shadow = ComputeCascadeShadow(posView, slice);

    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    shadow = fadeFactor * (shadow - 1.0) + 1.0;

#if defined(DIRECTIONAL) && defined(SCREEN_SPACE_SHADOWS)
    shadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.position.xy);
#endif
#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
    shadow *= TerrainShadows::GetTerrainShadowMultFromViewPosition(
        posView,
        TerrainShadows::TerrainShadowsSampler,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif
#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    shadow *= WaterEffects::GetCausticsMultFromViewPosition(
        posView,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif

    float3 albedoPremult  = albedoSample.xyz * albedoSample.w;
    float  NdotL_raw      = dot(normalView, SunDirection.xyz);
    float  NdotL_pos      = max(NdotL_raw, 0.0);
    float  NdotL_clamped  = min(NdotL_pos, 1.0);
    float  oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;

#ifndef AMBIENT
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif

    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float skinNdotL  = dot(matSample.xyz, SunDirection.xyz);
        float skinNdotV  = dot(matSample.xyz, viewDirNeg);
        float sinScaleL  = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV  = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_hair_spec_shifts.y, sinA1, cosA1);
        float rot1     = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
#ifndef AMBIENT
        float vis1     = max(rot1 * skinNdotV + rot1Perp * sinScaleV, 0.0);
#else
        float vis1     = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
#endif
        float pow1     = exp2(log2(vis1) * cb12_idx28_hair_spec_params.w);
        float hairIntensity =
            saturate(cb12_idx28_hair_spec_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoSample.w, hairIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_hair_spec_shifts.x, sinA2, cosA2);
        float rot2     = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2     = max(rot2 * skinNdotV + rot2Perp * sinScaleV, 0.0);
        float pow2     = exp2(log2(vis2) * cb12_idx28_hair_spec_params.y)
            * cb12_idx28_hair_spec_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * SunColor_HDR.xyz);
        brdfModulator = 0.0;
    }
    else
    {
        float depthScale     = matSample.z * 100.0;
        float specExpBase    = exp2(matSample.x * 10.0 + 1.0);
#ifndef AMBIENT
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#endif

        float NdotV_raw = dot(viewDirNeg, normalView);
#ifdef AMBIENT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
        float  oneMinusNdotV = 1.0 - saturate(NdotV_raw);
        float  ambientSpecularFactor =
            exp2((3.0 - matSample.x) * log2(oneMinusNdotV)) * 0.25;
        ambientSpecular = ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir) * matSample.y;

        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#endif
        float3 tangentV  = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL  = SunDirection.xyz - normalView * NdotL_raw;
        float tangentVLDot = dot(tangentV, tangentL);

        float roughSq = roughness01 * roughness01;
        float visA    = roughSq / (roughSq + 0.57);
        float visB    = roughSq / (roughSq + 0.09);
        visA = 1.0 - 0.5 * visA;
        visB *= 0.45;

        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentRatio = tangentSin / tangentDenom;
        float tangentVL = max(tangentVLDot, 0.0);
        float visibilityGeom = visB * tangentVL;
        visibilityGeom = visibilityGeom * tangentRatio + visA;
        brdfShadowMix  = NdotL_pos * visibilityGeom;

        float3 halfVec = SunDirection.xyz - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH     = saturate(dot(viewDirNeg, halfVec));
        float NdotH     = saturate(dot(halfVec, normalView));

        float distributionNorm =
            (specExpBase * specExpScale + 2.0) * 0.15915494;
        float distribution     = exp2(specExp * log2(NdotH));
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, asfloat(0x34000000));
        float minN         = min(NdotL_clamped, NdotV_sat);
        float twoNdotH     = NdotH + NdotH;
        bool  usePeakRatio = (VdotH_nonneg >= minN * twoNdotH);
#ifndef AMBIENT
        bool  useUnityRatio = (minN == NdotV_sat);
#else
        bool  useUnityRatio = (NdotV_sat == minN);
#endif
        float ratioNLNV    = NdotL_clamped / NdotV_sat;
        float ratio        = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility   = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH  = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm    = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = fresnelTerm * visibility;
        specMag = specMag * distributionNorm;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_DIRECTIONAL_SPECULAR_SCALE;

        brdfSpecular = (specMag * SunColor_HDR.xyz) * NdotL_clamped;
        brdfModulator = depthScale;
    }

#ifdef AMBIENT
    float ambientFres = exp2(ambientFresLog * 0.01);
#else
    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = 1.0 - NdotV_view;
    ambientFres = exp2(log2(ambientFres) * 0.01);
#endif

    float fresEdge    = saturate(dot(viewDirNeg, -SunDirection.xyz));
    float ambientTerm = ambientFres * fresEdge * NdotL_clamped * roughness01;

    float3 finalDiffuse = SunColor_HDR.xyz * brdfShadowMix;
    finalDiffuse += SunColor_HDR.xyz * ambientTerm;

    float backfaceWrap = saturate(-NdotL_raw);
    finalDiffuse += SunColor_HDR.xyz * (backfaceWrap * albedoPremult);

    float forwardBlend =
        saturate((NdotL_raw + brdfModulator) / (brdfModulator + 1.0));
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * SunColor_HDR.xyz) * albedoSample.xyz;

    float specMix = mad(schlickFres, -0.5, 1.0);
#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::Evaluation skylighting =
        Skylighting::Evaluate(posView, normalView);
    Skylighting::ApplyAmbient(
        ambientDiffuse, ambientSpecular, skylighting);
#endif
#ifdef WETNESS_EFFECTS
    float wetness = WetnessEffects::GetWetness(
        normalView,
        float4(ViewToWorld_row2.xyz, 1.0));
    float3 wetViewDir = -posView * rsqrt(dot(posView, posView));
    float3 wetDiffuse = finalDiffuse * shadow;
    float3 wetSpecular = (brdfSpecular * specMix) * shadow;
    WetnessEffects::ApplyDirectCoat(
        normalView,
        wetViewDir,
        SunDirection.xyz,
        SunColor_HDR.xyz * shadow,
        wetness,
        wetDiffuse,
        wetSpecular);
#  ifdef AMBIENT
    output.specular = float4(wetSpecular + ambientSpecular, 1.0);
    output.diffuse = float4(wetDiffuse + ambientDiffuse, 0.0);
#  else
    output.specular = float4(wetSpecular, 1.0);
    output.diffuse = float4(wetDiffuse, 0.0);
#  endif
    output.diffuse /= 3.0;
#else
#ifdef AMBIENT
    float3 scaledSpecular = brdfSpecular * specMix;
    output.specular = float4(0.0, 0.0, 0.0, 1.0);
    output.specular += float4(
        mad(scaledSpecular, shadow, ambientSpecular), 0.0);
#else
    output.specular.xyz = (brdfSpecular * specMix) * shadow;
    output.specular.w = 1.0;
#endif

#ifdef AMBIENT
    output.diffuse = float4(ambientDiffuse, 0.0);
    output.diffuse += float4(finalDiffuse * shadow, 0.0);
    output.diffuse /= 3.0;
#else
    output.diffuse = float4(finalDiffuse, 0.0);
    output.diffuse *= shadow;
    output.diffuse /= 3.0;
#endif
#endif

#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::ApplyFullscreenDebug(
        output.diffuse, output.specular, skylighting);
#endif
    return output;
}
#endif

#ifdef BSDFLIGHT_PS_DIRSPLITS2

#if !defined(DIRECTIONAL)
#  error "this source is the native DIRECTIONAL family; define DIRECTIONAL"
#endif
#if defined(POINTOMNI) || defined(POINTSPOT) || defined(SPOT) || defined(HALFOMNI)
#  error "DIRECTIONAL excludes punctual light macros"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the DIRSPLITS=1 family in bsdf_light_deferred_shadow_only.hlsl"
#endif
#ifndef SHADOW
#  error "DIRSPLITS=2 requires SHADOW"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the split count is a native axis and is never assumed"
#endif
#if DIRSPLITS != 2
#  error "this block requires DIRSPLITS=2"
#endif
#if !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native DIRSPLITS=2 SHADOW blob carries both SPECULAR and RGBSPEC"
#endif
#ifdef FILTER_PCSSPOISSON
#  error "DIRSPLITS=2 does not support FILTER_PCSSPOISSON"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_POISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif

#include "Common/DeferredContracts.hlsli"

#ifdef FILTER_POISSON
#include "Common/ShadowPoissonKernel.hlsli"
#endif

#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_POISSON)
#  define FO4_DS2_SHADOW_RAW_TAP 1
#endif

#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON)
#  define FO4_DS2_SHADOW_CMP_TAP 1
#endif

#if !defined(AMBIENT) \
    && ((defined(BLENDSPLIT) \
         && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
             || defined(FILTER_PCSS) || defined(FILTER_POISSON))) \
        || (!defined(BLENDSPLIT) \
            && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
                || defined(FILTER_PCSS) || defined(FILTER_POISSON) \
                || (!defined(FILTER_PCF9) && !defined(FILTER_PCSS) \
                    && !defined(FILTER_POISSON)))))
#  define FO4_DS2_TIGHT_NONAMBIENT 1
#endif
#if defined(FO4_DS2_TIGHT_NONAMBIENT) && !defined(BLENDSPLIT)
#  define FO4_DS2_TIGHT_NONAMBIENT_NOBLEND 1
#endif
#if defined(AMBIENT) && defined(IGNOREROUGHNESS) \
    && (defined(FILTER_PCF1) || defined(FILTER_PCSS) \
        || defined(FILTER_POISSON) \
        || (!defined(BLENDSPLIT) && defined(FILTER_PCF9)) \
        || (!defined(BLENDSPLIT) && !defined(FILTER_PCF9) \
            && !defined(FILTER_PCSS)))
#  define FO4_DS2_TIGHT_AMBIENT_IGNORE 1
#endif
#if defined(FO4_DS2_TIGHT_AMBIENT_IGNORE) && !defined(BLENDSPLIT)
#  define FO4_DS2_TIGHT_AMBIENT_NOBLEND 1
#endif
#if defined(FO4_DS2_TIGHT_NONAMBIENT_NOBLEND) \
    || defined(FO4_DS2_TIGHT_AMBIENT_NOBLEND)
#  define FO4_DS2_TIGHT_NOBLEND 1
#endif
#if defined(FO4_DS2_TIGHT_NONAMBIENT) || defined(FO4_DS2_TIGHT_AMBIENT_IGNORE)
#  define FO4_DS2_TIGHT_ORDER 1
#endif

#if defined(AMBIENT) && !defined(IGNOREROUGHNESS) \
    && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
        || defined(FILTER_PCSS) || defined(FILTER_POISSON) \
        || (!defined(BLENDSPLIT) && !defined(FILTER_PCSS)))
#  define FO4_DS2_REUSE_AMBIENT_VIEW_LOG 1
#endif
#if defined(AMBIENT) && defined(IGNOREROUGHNESS) && defined(BLENDSPLIT) \
    && defined(FILTER_PCF9)
#  define FO4_DS2_EQUAL_IGNORE_BLEND 1
#endif
#if defined(FO4_DS2_REUSE_AMBIENT_VIEW_LOG) \
    || defined(FO4_DS2_EQUAL_IGNORE_BLEND)
#  define FO4_DS2_TARGET_ORDER 1
#endif
#if defined(FO4_DS2_REUSE_AMBIENT_VIEW_LOG) && !defined(BLENDSPLIT)
#  define FO4_DS2_EARLY_SPLIT_GATE 1
#endif

#if defined(FO4_DS2_TIGHT_ORDER) || defined(FO4_DS2_TARGET_ORDER)
#  define FO4_DS2_REASSOC_ORDER 1
#endif

#if defined(FILTER_PCSS) || defined(FILTER_POISSON)
#  define FO4_DS2_USES_WORLD_SCALE 1
#endif

#if defined(FILTER_PCF9) || defined(FILTER_POISSON)
#  define FO4_DS2_USES_SAMPLE_PARAM 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_hair_spec_params;

    float4 cb12_idx29_hair_spec_shifts;

    float4 cb12_idx30;

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 cb12_pad_31_34[4];
    float4 CameraPosAdjust;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 SunDirection;

    float4 SunColor_HDR;

#ifdef AMBIENT

    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#else

    float4 cb2_pad_3_8[6];
#endif

    float4 cb2_idx9_split_distances;

    float4 cb2_idx10_fade_distances;

    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;
    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;

    float4 cb2_pad_17_19[3];

    float4 cb2_idx20_shadow_sample_param;

    float4 cb2_idx21_cascade0_world_scale;
    float4 cb2_idx22_cascade1_world_scale;

    float4 cb2_pad_23;

    float4 cb2_idx24_distance_fade;
};

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

#ifdef FO4_DS2_SHADOW_RAW_TAP

Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
#endif

#ifdef FO4_DS2_SHADOW_CMP_TAP

Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
#endif

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

#ifdef FO4_DS2_SHADOW_RAW_TAP
SamplerState g_sCascadeShadowRaw : register(s4);
#endif

#ifdef FO4_DS2_SHADOW_CMP_TAP
SamplerComparisonState g_sCascadeShadowCmp : register(s5);
#endif

#if defined(AMBIENT) || defined(FO4_DS2_TIGHT_NONAMBIENT)
static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.1415927;
#else
static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.141593;
#endif

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  recon = 1.0 - encLenSq * 0.25;
    float  z = 1.0 - encLenSq * 0.5;
    float  scale = sqrt(recon);
    return float3(enc * scale, -z);
}

#ifdef AMBIENT
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(cb2_ambient_gradient_row0, directionH);
    encoded.y = dot(cb2_ambient_gradient_row1, directionH);
    encoded.z = dot(cb2_ambient_gradient_row2, directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

float ComputeCascadeShadow(float3 posView,
                           float4 row0, float4 row1, float4 row2,
                           float slice
#ifdef FO4_DS2_USES_WORLD_SCALE
                           , float4 cascadeScale
#endif
#ifdef FILTER_POISSON
                           , float poissonBiasScale
#endif
                           )
{
    float4 posViewH = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(row0, posViewH);
    shadowUV.y = dot(row1, posViewH);
    float shadowZ = dot(row2, posViewH);

#if defined(FILTER_PCF1)

    return g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)

    float sum = 0.0;
    [loop]
    for (int i = 0; i < 3; ++i)
    {
        float offsetX = float(i) - 1.0;
        [loop]
        for (int j = 0; j < 3; ++j)
        {
            float offsetY = float(j) - 1.0;
            float2 tapUV = float2(offsetX, offsetY)
                * cb2_idx20_shadow_sample_param.zw + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * (1.0 / 9.0);

#elif defined(FILTER_PCSS)

    float2 searchStep = 1.0 / cascadeScale.xy;

    float2 blocker = float2(0.0, 0.0);
    [unroll]
    for (int bi = 0; bi < 5; ++bi)
    {
        float offsetX = float(bi - 2);
        [unroll]
        for (int bj = 0; bj < 5; ++bj)
        {
            float offsetY = float(bj - 2);
            float2 tapUV = shadowUV + float2(offsetX, offsetY) * searchStep;
            float  tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool   isBlocker = tapDepth < shadowZ;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y == 0.0)
    {
        return 1.0;
    }

    float centerDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float centerLit = (centerDepth >= shadowZ) ? 1.0 : 0.0;

    float averageBlocker = blocker.x / blocker.y;
    float worldRange     = cascadeScale.w - cascadeScale.z;
    float receiverWorld  = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld   = worldRange * averageBlocker + cascadeScale.z;
    float separation     = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra       = (blockerWorld < cascadeScale.z + 0.001)
        ? 1.9
        : (separation * 1.8 + 0.1);

    float sum = centerLit;
    [loop]
    for (int fi = 0; fi < 5; ++fi)
    {
        float offsetX = penumbra * (float(fi) - 2.0);
        [loop]
        for (int fj = 0; fj < 5; ++fj)
        {
            float offsetY = penumbra * (float(fj) - 2.0);
            float2 tapUV = (searchStep * float2(offsetX, offsetY)) * 0.5 + shadowUV;
            sum = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * 0.04;

#elif defined(FILTER_POISSON)

#if defined(FO4_DS2_TIGHT_ORDER) || defined(FO4_DS2_REUSE_AMBIENT_VIEW_LOG)
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - rcpWorldRange * poissonBiasScale;
#if !defined(FO4_DS2_TIGHT_ORDER) && !defined(FO4_DS2_REUSE_AMBIENT_VIEW_LOG)
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), zRef);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), zRef);
    }
    return sum * 0.0625;

#else

    float tapDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    return (tapDepth >= shadowZ) ? 1.0 : 0.0;
#endif
}

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    float linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 uvScreen = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float2 uvNDC = uvScreen * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float4 albedoSample = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

#ifndef IGNOREROUGHNESS
    float roughness01   = 1.0 - matSample.x;
#endif
    float posViewLenSq  = dot(-posView, -posView);
    float posViewLen    = rsqrt(posViewLenSq);
    float3 viewDirNeg   = -posView * posViewLen;

#ifdef AMBIENT
    float3 ambientDiffuse  = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

#ifdef FO4_DS2_TIGHT_AMBIENT_IGNORE
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif
#ifdef FO4_DS2_REUSE_AMBIENT_VIEW_LOG
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float ambientFresLog = log2(1.0 - NdotV_view);
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif
#ifdef FO4_DS2_EQUAL_IGNORE_BLEND
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif

#ifdef FO4_DS2_TIGHT_NOBLEND
    float shadow;
    if (linearizedDepth < cb2_idx9_split_distances.w)
    {
#endif

#ifdef FO4_DS2_EARLY_SPLIT_GATE
    float shadow;
    [branch]
    if (linearizedDepth < cb2_idx9_split_distances.w)
    {
#endif
    bool cascade0Active = (linearizedDepth < cb2_idx10_fade_distances.y);
    bool beforeCascade1 = (linearizedDepth < cb2_idx10_fade_distances.x);
#ifndef FO4_DS2_REASSOC_ORDER
    bool cascade1Active = (cb2_idx10_fade_distances.x < linearizedDepth);
    bool beyondCascade0 = (cb2_idx10_fade_distances.y < linearizedDepth);
#endif

    float cascade0Shadow = 1.0;
    if (cascade0Active)
    {
        cascade0Shadow = ComputeCascadeShadow(
            posView, cb2_cascade0_row0, cb2_cascade0_row1, cb2_cascade0_row2,
            0.0
#ifdef FO4_DS2_USES_WORLD_SCALE
            , cb2_idx21_cascade0_world_scale
#endif
#ifdef FILTER_POISSON
            , 0.275
#endif
            );
    }

#ifdef FO4_DS2_REASSOC_ORDER
    bool cascade1Active = (cb2_idx10_fade_distances.x < linearizedDepth);
    bool beyondCascade0 = (cb2_idx10_fade_distances.y < linearizedDepth);
#endif

    float cascade1Shadow = 1.0;
    if (cascade1Active)
    {
        cascade1Shadow = ComputeCascadeShadow(
            posView, cb2_cascade1_row0, cb2_cascade1_row1, cb2_cascade1_row2,
            1.0
#ifdef FO4_DS2_USES_WORLD_SCALE
            , cb2_idx22_cascade1_world_scale
#endif
#ifdef FILTER_POISSON
            , 1.0
#endif
            );
    }

#ifdef BLENDSPLIT

    float blendRange = cb2_idx10_fade_distances.y - cb2_idx10_fade_distances.x;
#ifdef FO4_DS2_TIGHT_ORDER
    float blendDistance = linearizedDepth - cb2_idx10_fade_distances.x;
    float blendRangeInv = 1.0 / blendRange;
    float t = saturate(blendDistance * blendRangeInv);
#elif defined(FO4_DS2_TARGET_ORDER)
    float blendDelta = linearizedDepth - cb2_idx10_fade_distances.x;
    float inverseBlendRange = 1.0 / blendRange;
#  ifdef FO4_DS2_EQUAL_IGNORE_BLEND
    float t = saturate(blendDelta * inverseBlendRange);
#  else
    float t = saturate(inverseBlendRange * blendDelta);
#  endif
#else
    float t = saturate((linearizedDepth - cb2_idx10_fade_distances.x) / blendRange);
#endif
#ifdef FO4_DS2_REASSOC_ORDER
    float blendSeed = mad(t, -2.0, 3.0);
    t *= t;
    float blendW = min(blendSeed * t, 1.0);
#else
    float blendW = min(t * t * (3.0 - 2.0 * t), 1.0);
#endif
    float bandShadow = lerp(cascade0Shadow, cascade1Shadow, blendW);
#else

    float bandShadow = 1.0;
#endif

#if defined(FO4_DS2_TIGHT_NOBLEND) || defined(FO4_DS2_EARLY_SPLIT_GATE)
    shadow = beyondCascade0 ? cascade1Shadow : bandShadow;
#else
    float shadow = beyondCascade0 ? cascade1Shadow : bandShadow;
#endif
    shadow = beforeCascade1 ? cascade0Shadow : shadow;

    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    shadow = fadeFactor * (shadow - 1.0) + 1.0;

#if defined(FO4_DS2_TIGHT_NOBLEND) || defined(FO4_DS2_EARLY_SPLIT_GATE)
    }
    else
    {
        shadow = 1.0;
    }
#else
#ifndef BLENDSPLIT

    if (!(linearizedDepth < cb2_idx9_split_distances.w))
    {
        shadow = 1.0;
    }
#endif
#endif

#ifdef FO4_DS2_REASSOC_ORDER
    float3 albedoPremult  = albedoSample.xyz * albedoSample.w;
#else
    float3 albedoPremult  = albedoSample.w * albedoSample.xyz;
#endif

#if defined(DIRECTIONAL) && defined(SCREEN_SPACE_SHADOWS)
    shadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.position.xy);
#endif
#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
    shadow *= TerrainShadows::GetTerrainShadowMultFromViewPosition(
        posView,
        TerrainShadows::TerrainShadowsSampler,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif
#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    shadow *= WaterEffects::GetCausticsMultFromViewPosition(
        posView,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif

    float  NdotL_raw      = dot(normalView, SunDirection.xyz);
    float  NdotL_pos      = max(NdotL_raw, 0.0);
    float  NdotL_clamped  = min(NdotL_pos, 1.0);
    float  oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;

#if !defined(FO4_DS2_TIGHT_AMBIENT_IGNORE) && !defined(FO4_DS2_TARGET_ORDER)
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif

    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {

        float skinNdotL  = dot(matSample.xyz, SunDirection.xyz);
        float skinNdotV  = dot(matSample.xyz, viewDirNeg);
        float sinScaleL  = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV  = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_hair_spec_shifts.y, sinA1, cosA1);
        float rot1     = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
#ifdef FO4_DS2_TIGHT_NONAMBIENT
        float vis1     = max(rot1 * skinNdotV + rot1Perp * sinScaleV, 0.0);
#else
        float vis1     = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
#endif
        float pow1     = exp2(log2(vis1) * cb12_idx28_hair_spec_params.w);
        float hairIntensity =
            saturate(cb12_idx28_hair_spec_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoSample.w, hairIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_hair_spec_shifts.x, sinA2, cosA2);
        float rot2     = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
#ifdef FO4_DS2_REASSOC_ORDER
        float vis2     = max(rot2 * skinNdotV + rot2Perp * sinScaleV, 0.0);
#else
        float vis2     = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
#endif
        float pow2     = exp2(log2(vis2) * cb12_idx28_hair_spec_params.y)
            * cb12_idx28_hair_spec_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * SunColor_HDR.xyz);
        brdfModulator = 0.0;
    }
    else
    {
        float depthScale     = matSample.z * 100.0;
        float specExpBase    = exp2(matSample.x * 10.0 + 1.0);
#if defined(FO4_DS2_TIGHT_AMBIENT_IGNORE) || defined(FO4_DS2_TARGET_ORDER)
#elif defined(AMBIENT) || defined(FO4_DS2_TIGHT_NONAMBIENT)
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#else
        float specExpScale = 1.0 - schlickFres * 0.98;
        float specExp = specExpScale * specExpBase;
#endif

#if !defined(FO4_DS2_TIGHT_NONAMBIENT) || !defined(IGNOREROUGHNESS)
#if defined(FO4_DS2_TIGHT_AMBIENT_IGNORE) || defined(FO4_DS2_EQUAL_IGNORE_BLEND)
        float NdotV_raw = dot(normalView, viewDirNeg);
#else
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
#endif
#ifdef AMBIENT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
#ifndef FO4_DS2_REUSE_AMBIENT_VIEW_LOG
        float  oneMinusNdotV = 1.0 - saturate(NdotV_raw);
#endif
#ifdef IGNOREROUGHNESS
        float  ambientSpecularFactor =
            oneMinusNdotV * oneMinusNdotV * 0.25;
#elif defined(FO4_DS2_REUSE_AMBIENT_VIEW_LOG)
        float  ambientSpecularFactor =
            exp2((3.0 - matSample.x) * ambientFresLog) * 0.25;
#else
        float  ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
#endif
        ambientSpecular = ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir) * matSample.y;
#if defined(FO4_DS2_TIGHT_AMBIENT_IGNORE) || defined(FO4_DS2_TARGET_ORDER)
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#endif
#endif
#ifdef IGNOREROUGHNESS

        brdfShadowMix = NdotL_pos;
#else
        float3 tangentV  = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL  = SunDirection.xyz - normalView * NdotL_raw;
#ifdef FO4_DS2_REASSOC_ORDER
        float tangentVLDot = dot(tangentV, tangentL);

        float roughSq = roughness01 * roughness01;
        float visA    = roughSq / (roughSq + 0.57);
        float visB    = roughSq / (roughSq + 0.09);
        visA = 1.0 - 0.5 * visA;
        visB *= 0.45;

        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentRatio = tangentSin / tangentDenom;
        float tangentVL = max(tangentVLDot, 0.0);
        float visibilityGeom = visB * tangentVL;
        visibilityGeom = visibilityGeom * tangentRatio + visA;
#else
        float  tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA    = roughSq / (roughSq + 0.57);
        float visB    = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin   = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                           * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
#endif
        brdfShadowMix  = NdotL_pos * visibilityGeom;
#endif

        float3 halfVec = SunDirection.xyz - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

#if defined(FO4_DS2_TIGHT_NONAMBIENT) && defined(IGNOREROUGHNESS)
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
        float NdotV_sat = saturate(NdotV_raw);
        float VdotH     = saturate(dot(viewDirNeg, halfVec));
        float NdotH     = saturate(dot(halfVec, normalView));

#if defined(AMBIENT) || defined(FO4_DS2_TIGHT_NONAMBIENT)
        float distributionNorm =
            (specExpBase * specExpScale + 2.0) * 0.15915494;
#else
        float distributionNorm =
            (specExpBase * specExpScale + 2.0) * 0.159155;
#endif
#ifdef FO4_DS2_REASSOC_ORDER
        float distribution     = exp2(specExp * log2(NdotH));
#else
        float distribution     = exp2(log2(NdotH) * specExp);
#endif
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, asfloat(0x34000000));
#if (defined(FO4_DS2_TIGHT_ORDER) && defined(IGNOREROUGHNESS)) \
    || defined(FO4_DS2_EQUAL_IGNORE_BLEND)
        float minN         = min(NdotV_sat, NdotL_clamped);
#else
        float minN         = min(NdotL_clamped, NdotV_sat);
#endif
        float twoNdotH     = NdotH + NdotH;
#ifdef FO4_DS2_REASSOC_ORDER
        bool  usePeakRatio = (VdotH_nonneg >= minN * twoNdotH);
#else
        bool  usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
#endif
#if defined(FO4_DS2_TIGHT_ORDER) || defined(FO4_DS2_EQUAL_IGNORE_BLEND)
        bool  useUnityRatio = (minN == NdotV_sat);
#else
        bool  useUnityRatio = (NdotV_sat == minN);
#endif
        float ratioNLNV    = NdotL_clamped / NdotV_sat;
        float ratio        = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility   = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH  = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm    = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

#ifdef FO4_DS2_REASSOC_ORDER
        float specMag = fresnelTerm * visibility;
        specMag = specMag * distributionNorm;
#else
        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
#endif
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_DIRECTIONAL_SPECULAR_SCALE;

#ifdef FO4_DS2_REASSOC_ORDER
        brdfSpecular = (specMag * SunColor_HDR.xyz) * NdotL_clamped;
#else
        brdfSpecular = NdotL_clamped * (specMag * SunColor_HDR.xyz);
#endif
        brdfModulator = depthScale;
    }

#ifdef IGNOREROUGHNESS
    float3 finalDiffuse = SunColor_HDR.xyz * brdfShadowMix;
#else
#ifdef FO4_DS2_REUSE_AMBIENT_VIEW_LOG
    float ambientFres = exp2(ambientFresLog * 0.01);
#else
    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = 1.0 - NdotV_view;
    ambientFres = exp2(log2(ambientFres) * 0.01);
#endif

    float fresEdge    = saturate(dot(viewDirNeg, -SunDirection.xyz));
#ifdef FO4_DS2_REASSOC_ORDER
    float ambientTerm = ambientFres * fresEdge * NdotL_clamped * roughness01;
#else
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;
#endif

#ifdef FO4_DS2_REASSOC_ORDER
    float3 finalDiffuse = SunColor_HDR.xyz * brdfShadowMix;
    finalDiffuse += SunColor_HDR.xyz * ambientTerm;
#else
    float3 finalDiffuse = SunColor_HDR.xyz * ambientTerm;
    finalDiffuse += SunColor_HDR.xyz * brdfShadowMix;
#endif
#endif

    float backfaceWrap = saturate(-NdotL_raw);
#ifdef FO4_DS2_REASSOC_ORDER
    finalDiffuse += SunColor_HDR.xyz * (backfaceWrap * albedoPremult);
#else
    finalDiffuse += SunColor_HDR.xyz * (albedoPremult * backfaceWrap);
#endif

#if defined(FO4_DS2_TIGHT_ORDER) || defined(FO4_DS2_EQUAL_IGNORE_BLEND)
    float forwardBlend =
        saturate((NdotL_raw + brdfModulator) / (brdfModulator + 1.0));
#else
    float forwardBlend =
        saturate((brdfModulator + NdotL_raw) / (brdfModulator + 1.0));
#endif
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * SunColor_HDR.xyz) * albedoSample.xyz;

#ifdef FO4_DS2_REASSOC_ORDER
    float specMix = mad(schlickFres, -0.5, 1.0);
#else
    float specMix = 1.0 - schlickFres * 0.5;
#endif
#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::Evaluation skylighting =
        Skylighting::Evaluate(posView, normalView);
    Skylighting::ApplyAmbient(
        ambientDiffuse, ambientSpecular, skylighting);
#endif
#ifdef WETNESS_EFFECTS
    float wetness = WetnessEffects::GetWetness(
        normalView,
        float4(ViewToWorld_row2.xyz, 1.0));
    float3 wetViewDir = -posView * rsqrt(dot(posView, posView));
    float3 wetDiffuse = finalDiffuse * shadow;
    float3 wetSpecular = (brdfSpecular * specMix) * shadow;
    WetnessEffects::ApplyDirectCoat(
        normalView,
        wetViewDir,
        SunDirection.xyz,
        SunColor_HDR.xyz * shadow,
        wetness,
        wetDiffuse,
        wetSpecular);
#  ifdef AMBIENT
    output.specular = float4(wetSpecular + ambientSpecular, 1.0);
    output.diffuse = float4(wetDiffuse + ambientDiffuse, 0.0);
#  else
    output.specular = float4(wetSpecular, 1.0);
    output.diffuse = float4(wetDiffuse, 0.0);
#  endif
    output.diffuse /= 3.0;
#else
#ifdef AMBIENT
#if defined(FO4_DS2_TIGHT_AMBIENT_IGNORE) || defined(FO4_DS2_TARGET_ORDER)
    float3 scaledSpecular = brdfSpecular * specMix;
    output.specular = float4(0.0, 0.0, 0.0, 1.0);
    output.specular += float4(
        mad(scaledSpecular, shadow, ambientSpecular), 0.0);
#else
    float specularScale = shadow * specMix;
    output.specular = float4(0.0, 0.0, 0.0, 1.0);
    output.specular += float4(
        mad(brdfSpecular, specularScale, ambientSpecular), 0.0);
#endif
#else
#ifdef FO4_DS2_TIGHT_NONAMBIENT
    output.specular.xyz = (brdfSpecular * specMix) * shadow;
#else
    output.specular.xyz = shadow * specMix * brdfSpecular;
#endif
    output.specular.w = 1.0;
#endif

#ifdef AMBIENT
    output.diffuse = float4(ambientDiffuse, 0.0);
#if defined(FO4_DS2_TIGHT_AMBIENT_IGNORE) || defined(FO4_DS2_TARGET_ORDER)
    output.diffuse += float4(finalDiffuse * shadow, 0.0);
#else
    output.diffuse += float4(shadow * finalDiffuse, 0.0);
#endif
    output.diffuse /= 3.0;
#else
#ifdef FO4_DS2_TIGHT_NONAMBIENT
    output.diffuse = float4(finalDiffuse, 0.0);
    output.diffuse *= shadow;
    output.diffuse /= 3.0;
#else
    output.diffuse.xyz = shadow * finalDiffuse;
    output.diffuse.xyz /= 3.0;
    output.diffuse.w = 0.0;
#endif
#endif
#endif

#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::ApplyFullscreenDebug(
        output.diffuse, output.specular, skylighting);
#endif
    return output;
}

#endif

#ifdef BSDFLIGHT_PS_DIRSPLITS3

#if !defined(DIRECTIONAL)
#  error "this source is the native DIRECTIONAL family; define DIRECTIONAL"
#endif
#if defined(POINTOMNI) || defined(POINTSPOT) || defined(SPOT) || defined(HALFOMNI)
#  error "DIRECTIONAL excludes punctual light macros"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the DIRSPLITS=1 family in bsdf_light_deferred_shadow_only.hlsl"
#endif
#ifndef SHADOW
#  error "DIRSPLITS=3 requires SHADOW"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the split count is a native axis and is never assumed"
#endif
#if DIRSPLITS != 3
#  error "this block requires DIRSPLITS=3"
#endif
#if !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native DIRSPLITS=3 SHADOW blob carries both SPECULAR and RGBSPEC"
#endif
#ifdef IGNORERIM
#  error "DIRSPLITS=3 does not support IGNORERIM"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_PCSSPOISSON) + defined(FILTER_POISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif

#include "Common/DeferredContracts.hlsli"

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#include "Common/ShadowPoissonKernel.hlsli"
#endif

#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_POISSON)
#  define FO4_DS3_SHADOW_RAW_TAP 1
#endif

#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_PCSSPOISSON) || defined(FILTER_POISSON)
#  define FO4_DS3_SHADOW_CMP_TAP 1
#endif

#if defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON) || defined(FILTER_POISSON)
#  define FO4_DS3_USES_WORLD_SCALE 1
#endif

#if defined(FILTER_PCF9) || defined(FILTER_POISSON)
#  define FO4_DS3_USES_SAMPLE_PARAM 1
#endif

#if defined(AMBIENT) && defined(BLENDSPLIT) \
    && !defined(FILTER_PCF9) && !defined(FILTER_PCSS) \
    && !defined(FILTER_PCSSPOISSON) && !defined(FILTER_POISSON)
#  define FO4_DS3_TIGHT_AMBIENT_BLEND 1
#endif

#if defined(AMBIENT) && !defined(BLENDSPLIT) && !defined(IGNOREROUGHNESS)
#  define FO4_DS3_REUSE_AMBIENT_VIEW_LOG 1
#endif
#if defined(AMBIENT) && defined(BLENDSPLIT) && !defined(IGNOREROUGHNESS) \
    && (defined(FILTER_PCF9) || defined(FILTER_PCSS) \
        || defined(FILTER_PCSSPOISSON) || defined(FILTER_POISSON))
#  define FO4_DS3_EQUAL_AMBIENT_BLEND_PCF9 1
#endif
#if !defined(AMBIENT) && !defined(BLENDSPLIT) \
    && ((!defined(IGNOREROUGHNESS) \
         && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
             || defined(FILTER_PCSS) || defined(FILTER_POISSON))) \
        || (defined(IGNOREROUGHNESS) && defined(FILTER_PCSS)))
#  define FO4_DS3_EQUAL_NONAMBIENT_PCF1 1
#endif
#if !defined(AMBIENT) && defined(BLENDSPLIT) \
    && ((defined(IGNOREROUGHNESS) \
         && (defined(FILTER_PCF9) || defined(FILTER_POISSON))) \
        || (!defined(IGNOREROUGHNESS) \
            && (defined(FILTER_PCF1) || defined(FILTER_PCF9) \
                || defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON) \
                || defined(FILTER_POISSON) \
                || (!defined(FILTER_PCF9) && !defined(FILTER_PCSS) \
                    && !defined(FILTER_PCSSPOISSON) \
                    && !defined(FILTER_POISSON)))))
#  define FO4_DS3_TIGHT_NONAMBIENT_BLEND 1
#endif
#if defined(AMBIENT) && defined(BLENDSPLIT) && defined(IGNOREROUGHNESS) \
    && (defined(FILTER_PCF9) || defined(FILTER_POISSON))
#  define FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND 1
#endif
#if defined(AMBIENT) && !defined(BLENDSPLIT) && defined(IGNOREROUGHNESS) \
    && defined(FILTER_PCSS)
#  define FO4_DS3_TIGHT_AMBIENT_IGNORE_SPLIT 1
#endif
#if (defined(FO4_DS3_TIGHT_NONAMBIENT_BLEND) \
     || defined(FO4_DS3_EQUAL_NONAMBIENT_PCF1)) \
    && defined(IGNOREROUGHNESS)
#  define FO4_DS3_TIGHT_NONAMBIENT_IGNORE 1
#endif
#if defined(FO4_DS3_EQUAL_NONAMBIENT_PCF1) \
    || defined(FO4_DS3_TIGHT_NONAMBIENT_BLEND)
#  define FO4_DS3_TIGHT_NONAMBIENT 1
#endif

#if defined(FO4_DS3_TIGHT_AMBIENT_BLEND) \
    || defined(FO4_DS3_REUSE_AMBIENT_VIEW_LOG) \
    || defined(FO4_DS3_EQUAL_AMBIENT_BLEND_PCF9)
#  define FO4_DS3_HOISTED_VIEW_LOG 1
#endif
#if defined(FO4_DS3_HOISTED_VIEW_LOG) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_SPLIT)
#  define FO4_DS3_EARLY_MATERIAL 1
#endif
#if defined(FO4_DS3_HOISTED_VIEW_LOG) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_SPLIT)
#  define FO4_DS3_AMBIENT_ORDER 1
#endif

#if defined(FO4_DS3_TIGHT_AMBIENT_BLEND) \
    || defined(FO4_DS3_EQUAL_AMBIENT_BLEND_PCF9) \
    || defined(FO4_DS3_TIGHT_NONAMBIENT_BLEND) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND)
#  define FO4_DS3_PHASED_BLEND 1
#endif
#if defined(FO4_DS3_REUSE_AMBIENT_VIEW_LOG) \
    || defined(FO4_DS3_EQUAL_NONAMBIENT_PCF1) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_SPLIT)
#  define FO4_DS3_PHASED_SPLIT 1
#endif

#if defined(FO4_DS3_PHASED_BLEND) || defined(FO4_DS3_PHASED_SPLIT)
#  define FO4_DS3_REASSOC_ORDER 1
#endif

#if (defined(FO4_DS3_PHASED_BLEND) \
     || defined(FO4_DS3_EQUAL_NONAMBIENT_PCF1)) \
    && !defined(IGNOREROUGHNESS)
#  define FO4_DS3_LATE_DIFFUSE 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_hair_spec_params;

    float4 cb12_idx29_hair_spec_shifts;

    float4 cb12_idx30;

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 cb12_pad_31_34[4];
    float4 CameraPosAdjust;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 SunDirection;

    float4 SunColor_HDR;

#ifdef AMBIENT

    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#else

    float4 cb2_pad_3_8[6];
#endif

    float4 cb2_idx9_split_distances;

    float4 cb2_idx10_fade_distances;

    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;
    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;
    float4 cb2_cascade2_row0;
    float4 cb2_cascade2_row1;
    float4 cb2_cascade2_row2;

    float4 cb2_idx20_shadow_sample_param;

    float4 cb2_idx21_cascade0_world_scale;
    float4 cb2_idx22_cascade1_world_scale;
    float4 cb2_idx23_cascade2_world_scale;

    float4 cb2_idx24_distance_fade;
};

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

#ifdef FO4_DS3_SHADOW_RAW_TAP

Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
#endif

#ifdef FO4_DS3_SHADOW_CMP_TAP

Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
#endif

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

#ifdef FO4_DS3_SHADOW_RAW_TAP
SamplerState g_sCascadeShadowRaw : register(s4);
#endif

#ifdef FO4_DS3_SHADOW_CMP_TAP
SamplerComparisonState g_sCascadeShadowCmp : register(s5);
#endif

#if defined(AMBIENT) || defined(FO4_DS3_TIGHT_NONAMBIENT)
static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.1415927;
#else
static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.141593;
#endif

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  recon = 1.0 - encLenSq * 0.25;
    float  z = 1.0 - encLenSq * 0.5;
    float  scale = sqrt(recon);
    return float3(enc * scale, -z);
}

#ifdef AMBIENT
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(cb2_ambient_gradient_row0, directionH);
    encoded.y = dot(cb2_ambient_gradient_row1, directionH);
    encoded.z = dot(cb2_ambient_gradient_row2, directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

float ComputeCascadeShadow(float3 posView,
                           float4 row0, float4 row1, float4 row2,
                           float slice
#ifdef FO4_DS3_USES_WORLD_SCALE
                           , float4 cascadeScale
#endif
#ifdef FILTER_POISSON
                           , float poissonBiasScale
#endif
                           )
{
    float4 posViewH = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(row0, posViewH);
    shadowUV.y = dot(row1, posViewH);
    float shadowZ = dot(row2, posViewH);

#if defined(FILTER_PCF1)

    return g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)

    float sum = 0.0;
    [loop]
    for (int i = 0; i < 3; ++i)
    {
        float offsetX = float(i) - 1.0;
        [loop]
        for (int j = 0; j < 3; ++j)
        {
            float offsetY = float(j) - 1.0;
            float2 tapUV = float2(offsetX, offsetY)
                * cb2_idx20_shadow_sample_param.zw + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * (1.0 / 9.0);

#elif defined(FILTER_PCSS)

    float2 searchStep = 1.0 / cascadeScale.xy;

    float2 blocker = float2(0.0, 0.0);
    [unroll]
    for (int bi = 0; bi < 5; ++bi)
    {
        float offsetX = float(bi - 2);
        [unroll]
        for (int bj = 0; bj < 5; ++bj)
        {
            float offsetY = float(bj - 2);
            float2 tapUV = shadowUV + float2(offsetX, offsetY) * searchStep;
            float  tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool   isBlocker = tapDepth < shadowZ;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y == 0.0)
    {
        return 1.0;
    }

    float centerDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float centerLit = (centerDepth >= shadowZ) ? 1.0 : 0.0;

    float averageBlocker = blocker.x / blocker.y;
    float worldRange     = cascadeScale.w - cascadeScale.z;
    float receiverWorld  = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld   = worldRange * averageBlocker + cascadeScale.z;
    float separation     = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra       = (blockerWorld < cascadeScale.z + 0.001)
        ? 1.9
        : (separation * 1.8 + 0.1);

    float sum = centerLit;
    [loop]
    for (int fi = 0; fi < 5; ++fi)
    {
        float offsetX = penumbra * (float(fi) - 2.0);
        [loop]
        for (int fj = 0; fj < 5; ++fj)
        {
            float offsetY = penumbra * (float(fj) - 2.0);
            float2 tapUV = (searchStep * float2(offsetX, offsetY)) * 0.5 + shadowUV;
            sum = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * 0.04;

#elif defined(FILTER_PCSSPOISSON)

    float2 searchStep = 1.0 / cascadeScale.xy;

    float2 blocker = float2(0.0, 0.0);
    [unroll]
    for (int bi = 0; bi < 5; ++bi)
    {
        float offsetX = float(bi - 2);
        [unroll]
        for (int bj = 0; bj < 5; ++bj)
        {
            float offsetY = float(bj - 2);
            float2 tapUV = shadowUV + float2(offsetX, offsetY) * searchStep;
            float  tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool   isBlocker = tapDepth < shadowZ;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y == 0.0)
    {
        return 1.0;
    }

    float averageBlocker = blocker.x / blocker.y;
    float worldRange     = cascadeScale.w - cascadeScale.z;
    float receiverWorld  = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld   = worldRange * averageBlocker + cascadeScale.z;
    float separation     = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra       = (blockerWorld < cascadeScale.z + 0.001)
        ? 1.9
        : (separation * 1.8 + 0.1);

    float kernelScale = penumbra * searchStep.x;

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), shadowZ);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), shadowZ);
    }
    return sum * 0.0625;

#elif defined(FILTER_POISSON)

#if defined(FO4_DS3_REUSE_AMBIENT_VIEW_LOG) \
    || defined(FO4_DS3_EQUAL_AMBIENT_BLEND_PCF9) \
    || defined(FO4_DS3_EQUAL_NONAMBIENT_PCF1) \
    || defined(FO4_DS3_TIGHT_NONAMBIENT_BLEND) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND)
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - rcpWorldRange * poissonBiasScale;
#if !defined(FO4_DS3_REUSE_AMBIENT_VIEW_LOG) \
    && !defined(FO4_DS3_EQUAL_AMBIENT_BLEND_PCF9) \
    && !defined(FO4_DS3_EQUAL_NONAMBIENT_PCF1) \
    && !defined(FO4_DS3_TIGHT_NONAMBIENT_BLEND) \
    && !defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND)
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), zRef);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), zRef);
    }
    return sum * 0.0625;

#else

    float tapDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    return (tapDepth >= shadowZ) ? 1.0 : 0.0;
#endif
}

float ComputeDirectionalShadow(float3 posView, float linearizedDepth)
{
#ifndef BLENDSPLIT
    if (!(linearizedDepth < cb2_idx9_split_distances.w))
    {
        return 1.0;
    }
#endif

#ifdef FO4_DS3_PHASED_BLEND
    bool4 below = linearizedDepth < cb2_idx10_fade_distances.ywxz;
    bool cascade0Active = below.x;
#elif defined(FO4_DS3_PHASED_SPLIT)
    bool4 belowFade = linearizedDepth < cb2_idx10_fade_distances.ywxz;
    bool cascade0Active = belowFade.x;
#else
    bool cascade0Active = (linearizedDepth < cb2_idx10_fade_distances.y);
    bool cascade1Active = (cb2_idx10_fade_distances.x < linearizedDepth)
        && (linearizedDepth < cb2_idx10_fade_distances.w);
    bool cascade2Active = (cb2_idx10_fade_distances.z < linearizedDepth);
#endif

    float cascade0Shadow = 1.0;
    if (cascade0Active)
    {
        cascade0Shadow = ComputeCascadeShadow(
            posView, cb2_cascade0_row0, cb2_cascade0_row1, cb2_cascade0_row2,
            0.0
#ifdef FO4_DS3_USES_WORLD_SCALE
            , cb2_idx21_cascade0_world_scale
#endif
#ifdef FILTER_POISSON
            , 0.275
#endif
            );
    }

#ifdef FO4_DS3_PHASED_BLEND
    bool3 above = cb2_idx10_fade_distances.xyz < linearizedDepth;
    bool3 regions = below.ywy && above.xyz;
    bool cascade1Active = regions.x;
    bool cascade2Active = above.z;
#elif defined(FO4_DS3_PHASED_SPLIT)
    bool3 aboveFade = cb2_idx10_fade_distances.xzy < linearizedDepth;
    bool2 middleRegion = belowFade.yw && aboveFade.xz;
    bool cascade1Active = middleRegion.x;
    bool cascade2Active = aboveFade.y;
#endif

    float cascade1Shadow = 1.0;
    if (cascade1Active)
    {
        cascade1Shadow = ComputeCascadeShadow(
            posView, cb2_cascade1_row0, cb2_cascade1_row1, cb2_cascade1_row2,
            1.0
#ifdef FO4_DS3_USES_WORLD_SCALE
            , cb2_idx22_cascade1_world_scale
#endif
#ifdef FILTER_POISSON
            , 1.0
#endif
            );
    }

    float cascade2Shadow = 1.0;
    if (cascade2Active)
    {
        cascade2Shadow = ComputeCascadeShadow(
            posView, cb2_cascade2_row0, cb2_cascade2_row1, cb2_cascade2_row2,
            2.0
#ifdef FO4_DS3_USES_WORLD_SCALE
            , cb2_idx23_cascade2_world_scale
#endif
#ifdef FILTER_POISSON
            , 1.0
#endif
            );
    }

#ifdef BLENDSPLIT

#ifdef FO4_DS3_PHASED_BLEND
    float2 blendRange =
        cb2_idx10_fade_distances.yw - cb2_idx10_fade_distances.xz;
    float2 blendDistance =
        linearizedDepth - cb2_idx10_fade_distances.xz;
    float2 blendRangeInv = 1.0 / blendRange;
    float2 blendT = saturate(blendDistance * blendRangeInv);
    float2 blendSeed = mad(blendT, -2.0, 3.0);
    blendT *= blendT;
    float2 blendWeight = min(blendSeed * blendT, 1.0);
    float w01 = blendWeight.x;
    float w12 = blendWeight.y;
#else
    float range01 = cb2_idx10_fade_distances.y - cb2_idx10_fade_distances.x;
    float range12 = cb2_idx10_fade_distances.w - cb2_idx10_fade_distances.z;
    float t01 = saturate((linearizedDepth - cb2_idx10_fade_distances.x) / range01);
    float t12 = saturate((linearizedDepth - cb2_idx10_fade_distances.z) / range12);
    float w01 = min(t01 * t01 * (3.0 - 2.0 * t01), 1.0);
    float w12 = min(t12 * t12 * (3.0 - 2.0 * t12), 1.0);
#endif

    float lowerShadow = lerp(cascade0Shadow, cascade1Shadow, w01);
    float upperShadow = lerp(cascade1Shadow, cascade2Shadow, w12);
#ifdef FO4_DS3_PHASED_BLEND
    float shadow = cascade2Shadow;
    shadow = regions.z ? upperShadow : shadow;
    shadow = below.x ? lowerShadow : shadow;
    shadow = regions.y ? cascade1Shadow : shadow;
    shadow = below.z ? cascade0Shadow : shadow;
#else
    float shadow = cascade2Active ? upperShadow : lowerShadow;
#endif
#else

#ifdef FO4_DS3_PHASED_SPLIT
    float shadow = middleRegion.y ? cascade1Shadow : cascade2Shadow;
    shadow = belowFade.z ? cascade0Shadow : shadow;
#else
    float shadow = cascade2Active ? cascade2Shadow : cascade1Shadow;
    shadow = cascade0Active ? 1.0 : shadow;
    shadow = (cb2_idx10_fade_distances.x < linearizedDepth) ? shadow : cascade0Shadow;
#endif
#endif

    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    return fadeFactor * (shadow - 1.0) + 1.0;
}

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    float linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 uvScreen = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float2 uvNDC = uvScreen * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float4 albedoSample = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

#ifndef IGNOREROUGHNESS
    float roughness01 = 1.0 - matSample.x;
#endif
    float posViewLenSq  = dot(-posView, -posView);
    float posViewLen    = rsqrt(posViewLenSq);
    float3 viewDirNeg   = -posView * posViewLen;

#ifdef AMBIENT
    float3 ambientDiffuse  = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

#ifdef FO4_DS3_HOISTED_VIEW_LOG
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float ambientFresLog = log2(1.0 - NdotV_view);
#endif
#ifdef FO4_DS3_EARLY_MATERIAL
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif

    float shadow = ComputeDirectionalShadow(posView, linearizedDepth);

#if defined(DIRECTIONAL) && defined(SCREEN_SPACE_SHADOWS)
    shadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.position.xy);
#endif
#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
    shadow *= TerrainShadows::GetTerrainShadowMultFromViewPosition(
        posView,
        TerrainShadows::TerrainShadowsSampler,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif
#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    shadow *= WaterEffects::GetCausticsMultFromViewPosition(
        posView,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif

#ifdef FO4_DS3_REASSOC_ORDER
    float3 albedoPremult  = albedoSample.xyz * albedoSample.w;
#else
    float3 albedoPremult  = albedoSample.w * albedoSample.xyz;
#endif
    float  NdotL_raw      = dot(normalView, SunDirection.xyz);
    float  NdotL_pos      = max(NdotL_raw, 0.0);
    float  NdotL_clamped  = min(NdotL_pos, 1.0);
    float  oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;

#ifndef FO4_DS3_EARLY_MATERIAL
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif

    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {

        float skinNdotL  = dot(matSample.xyz, SunDirection.xyz);
        float skinNdotV  = dot(matSample.xyz, viewDirNeg);
        float sinScaleL  = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV  = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_hair_spec_shifts.y, sinA1, cosA1);
        float rot1     = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
#ifdef FO4_DS3_TIGHT_NONAMBIENT
        float vis1     = max(rot1 * skinNdotV + rot1Perp * sinScaleV, 0.0);
#else
        float vis1     = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
#endif
        float pow1     = exp2(log2(vis1) * cb12_idx28_hair_spec_params.w);
        float hairIntensity =
            saturate(cb12_idx28_hair_spec_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoSample.w, hairIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_hair_spec_shifts.x, sinA2, cosA2);
        float rot2     = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
#ifdef FO4_DS3_REASSOC_ORDER
        float vis2     = max(rot2 * skinNdotV + rot2Perp * sinScaleV, 0.0);
#else
        float vis2     = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
#endif
        float pow2     = exp2(log2(vis2) * cb12_idx28_hair_spec_params.y)
            * cb12_idx28_hair_spec_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * SunColor_HDR.xyz);
        brdfModulator = 0.0;
    }
    else
    {
        float depthScale     = matSample.z * 100.0;
        float specExpBase    = exp2(matSample.x * 10.0 + 1.0);
#ifdef FO4_DS3_TIGHT_NONAMBIENT
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#elif !defined(FO4_DS3_AMBIENT_ORDER)
        float specExpScale = 1.0 - schlickFres * 0.98;
        float specExp = specExpScale * specExpBase;
#endif

#ifndef FO4_DS3_TIGHT_NONAMBIENT_IGNORE
#if defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_SPLIT)
        float NdotV_raw = dot(normalView, viewDirNeg);
#else
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
#endif
#ifdef AMBIENT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
#ifndef FO4_DS3_REUSE_AMBIENT_VIEW_LOG
        float  oneMinusNdotV = 1.0 - saturate(NdotV_raw);
#endif
#ifdef IGNOREROUGHNESS
        float  ambientSpecularFactor =
            oneMinusNdotV * oneMinusNdotV * 0.25;
#elif defined(FO4_DS3_HOISTED_VIEW_LOG)
        float  ambientSpecularFactor =
            exp2((3.0 - matSample.x) * ambientFresLog) * 0.25;
#else
        float  ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
#endif
#ifdef FO4_DS3_AMBIENT_ORDER
        ambientSpecular = ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir) * matSample.y;
#else
        ambientSpecular = matSample.y * ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir);
#endif
#ifdef FO4_DS3_AMBIENT_ORDER
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#endif
#endif

#ifdef IGNOREROUGHNESS

        brdfShadowMix = NdotL_pos;
#else
        float3 tangentV  = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL  = SunDirection.xyz - normalView * NdotL_raw;
#ifdef FO4_DS3_REASSOC_ORDER
        float tangentVLDot = dot(tangentV, tangentL);
#else
        float tangentVL = max(dot(tangentV, tangentL), 0.0);
#endif

        float roughSq = roughness01 * roughness01;
        float visA    = roughSq / (roughSq + 0.57);
        float visB    = roughSq / (roughSq + 0.09);
#ifdef FO4_DS3_REASSOC_ORDER
        visA = 1.0 - 0.5 * visA;
        visB *= 0.45;
#else
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;
#endif

#ifdef FO4_DS3_REASSOC_ORDER
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentRatio = tangentSin / tangentDenom;
        float tangentVL = max(tangentVLDot, 0.0);
        float visibilityGeom = visB * tangentVL;
        visibilityGeom = visibilityGeom * tangentRatio + visA;
#else
        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin   = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                           * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
#endif
        brdfShadowMix  = NdotL_pos * visibilityGeom;
#endif

        float3 halfVec = SunDirection.xyz - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

#ifdef FO4_DS3_TIGHT_NONAMBIENT_IGNORE
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
        float NdotV_sat = saturate(NdotV_raw);
        float VdotH     = saturate(dot(viewDirNeg, halfVec));
        float NdotH     = saturate(dot(halfVec, normalView));

#if defined(AMBIENT) || defined(FO4_DS3_TIGHT_NONAMBIENT)
        float distributionNorm =
            (specExpBase * specExpScale + 2.0) * 0.15915494;
#else
        float distributionNorm =
            (specExpBase * specExpScale + 2.0) * 0.159155;
#endif
#ifdef FO4_DS3_REASSOC_ORDER
        float distribution     = exp2(specExp * log2(NdotH));
#else
        float distribution     = exp2(log2(NdotH) * specExp);
#endif
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, asfloat(0x34000000));
#if defined(FO4_DS3_TIGHT_NONAMBIENT_IGNORE) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_SPLIT)
        float minN         = min(NdotV_sat, NdotL_clamped);
#else
        float minN         = min(NdotL_clamped, NdotV_sat);
#endif
        float twoNdotH     = NdotH + NdotH;
#ifdef FO4_DS3_REASSOC_ORDER
        bool  usePeakRatio = (VdotH_nonneg >= minN * twoNdotH);
#else
        bool  usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
#endif
#if defined(FO4_DS3_TIGHT_AMBIENT_BLEND) \
    || defined(FO4_DS3_EQUAL_AMBIENT_BLEND_PCF9) \
    || defined(FO4_DS3_TIGHT_NONAMBIENT) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_BLEND) \
    || defined(FO4_DS3_TIGHT_AMBIENT_IGNORE_SPLIT)
        bool  useUnityRatio = (minN == NdotV_sat);
#else
        bool  useUnityRatio = (NdotV_sat == minN);
#endif
        float ratioNLNV    = NdotL_clamped / NdotV_sat;
        float ratio        = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility   = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH  = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm    = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

#ifdef FO4_DS3_REASSOC_ORDER
        float specMag = fresnelTerm * visibility;
        specMag = specMag * distributionNorm;
#else
        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
#endif
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_DIRECTIONAL_SPECULAR_SCALE;

#ifdef FO4_DS3_REASSOC_ORDER
        brdfSpecular = (specMag * SunColor_HDR.xyz) * NdotL_clamped;
#else
        brdfSpecular = NdotL_clamped * (specMag * SunColor_HDR.xyz);
#endif
        brdfModulator = depthScale;
    }

#ifndef FO4_DS3_LATE_DIFFUSE
    float3 finalDiffuse = SunColor_HDR.xyz * brdfShadowMix;
#endif

#ifndef IGNOREROUGHNESS

#ifdef FO4_DS3_HOISTED_VIEW_LOG
    float ambientFres = exp2(ambientFresLog * 0.01);
#else
    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = exp2(log2(1.0 - NdotV_view) * 0.01);
#endif
    float fresEdge    = saturate(dot(viewDirNeg, -SunDirection.xyz));
#ifdef FO4_DS3_REASSOC_ORDER
    float ambientTerm = ambientFres * fresEdge * NdotL_clamped * roughness01;
#else
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;
#endif

#ifdef FO4_DS3_LATE_DIFFUSE
    float3 finalDiffuse = SunColor_HDR.xyz * brdfShadowMix;
    finalDiffuse += SunColor_HDR.xyz * ambientTerm;
#else
    finalDiffuse += SunColor_HDR.xyz * ambientTerm;
#endif
#endif

    float backfaceWrap = saturate(-NdotL_raw);
#ifdef FO4_DS3_REASSOC_ORDER
    finalDiffuse += SunColor_HDR.xyz * (backfaceWrap * albedoPremult);
#else
    finalDiffuse += SunColor_HDR.xyz * (albedoPremult * backfaceWrap);
#endif

#if defined(FO4_DS3_PHASED_BLEND) \
    || defined(FO4_DS3_EQUAL_NONAMBIENT_PCF1)
    float forwardBlend =
        saturate((NdotL_raw + brdfModulator) / (brdfModulator + 1.0));
#else
    float forwardBlend =
        saturate((brdfModulator + NdotL_raw) / (brdfModulator + 1.0));
#endif
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * SunColor_HDR.xyz) * albedoSample.xyz;

#ifdef FO4_DS3_REASSOC_ORDER
    float specMix = mad(schlickFres, -0.5, 1.0);
#else
    float specMix = 1.0 - schlickFres * 0.5;
#endif

#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::Evaluation skylighting =
        Skylighting::Evaluate(posView, normalView);
    Skylighting::ApplyAmbient(
        ambientDiffuse, ambientSpecular, skylighting);
#endif
#ifdef WETNESS_EFFECTS
    float wetness = WetnessEffects::GetWetness(
        normalView,
        float4(ViewToWorld_row2.xyz, 1.0));
    float3 wetViewDir = -posView * rsqrt(dot(posView, posView));
    float3 wetDiffuse = finalDiffuse * shadow;
    float3 wetSpecular = (brdfSpecular * specMix) * shadow;
    WetnessEffects::ApplyDirectCoat(
        normalView,
        wetViewDir,
        SunDirection.xyz,
        SunColor_HDR.xyz * shadow,
        wetness,
        wetDiffuse,
        wetSpecular);
#  ifdef AMBIENT
    output.specular = float4(wetSpecular + ambientSpecular, 1.0);
    output.diffuse = float4(wetDiffuse + ambientDiffuse, 0.0);
#  else
    output.specular = float4(wetSpecular, 1.0);
    output.diffuse = float4(wetDiffuse, 0.0);
#  endif
    output.diffuse /= 3.0;
#else
#ifdef FO4_DS3_TIGHT_NONAMBIENT
    output.specular.xyz = (brdfSpecular * specMix) * shadow;
    output.specular.w = 1.0;
#elif defined(FO4_DS3_AMBIENT_ORDER)
    float3 scaledSpecular = brdfSpecular * specMix;
    output.specular = float4(0.0, 0.0, 0.0, 1.0);
    output.specular += float4(
        mad(scaledSpecular, shadow, ambientSpecular), 0.0);
#else
    output.specular.xyz = shadow * specMix * brdfSpecular;
#ifdef AMBIENT
    output.specular.xyz += ambientSpecular;
#endif
    output.specular.w = 1.0;
#endif

#ifdef FO4_DS3_TIGHT_NONAMBIENT
    output.diffuse = float4(finalDiffuse, 0.0);
    output.diffuse *= shadow;
    output.diffuse /= 3.0;
#elif defined(FO4_DS3_AMBIENT_ORDER)
    output.diffuse = float4(ambientDiffuse, 0.0);
    output.diffuse += float4(finalDiffuse * shadow, 0.0);
    output.diffuse /= 3.0;
#else
    output.diffuse.xyz = shadow * finalDiffuse;
#ifdef AMBIENT
    output.diffuse.xyz += ambientDiffuse;
#endif
    output.diffuse.xyz /= 3.0;
    output.diffuse.w = 0.0;
#endif
#endif

#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::ApplyFullscreenDebug(
        output.diffuse, output.specular, skylighting);
#endif
    return output;
}
#endif

#ifdef BSDFLIGHT_PS_GOBO

#if !defined(POINTOMNI)
#  error "this source is the native POINTOMNI gobo family; define POINTOMNI"
#endif
#if !defined(GOBOPROJECTION)
#  error "this source owns the t7/s7 GOBOPROJECTION contract; define GOBOPROJECTION"
#endif
#if !defined(RGBSPEC)
#  error "every native unshadowed POINTOMNI gobo blob carries RGBSPEC"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the decoder baseline is a native axis"
#endif
#if DIRSPLITS != 2
#  error "this source reconstructs DIRSPLITS=2 only"
#endif
#ifdef SHADOW
#  error "the shadowed POINTOMNI family has a distinct shadow-map ABI"
#endif
#if defined(DIRECTIONAL) || defined(POINTSPOT) || defined(SPOT)
#  error "POINTOMNI gobo is exclusive with the directional and projected light families"
#endif
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  error "FILTER_* selects a shadow tap and is invalid without SHADOW"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the directional DIRSPLITS=1 family"
#endif
#ifdef AMBIENT
#  error "no native unshadowed POINTOMNI gobo blob carries AMBIENT"
#endif
#ifdef ATTENUATION_ONLY
#  error "ATTENUATION_ONLY is not a native POINTOMNI gobo permutation"
#endif
#ifdef HALFOMNI
#  error "HALFOMNI only occurs on the shadowed POINTOMNI path"
#endif
#ifdef BLENDSPLIT
#  error "BLENDSPLIT is a directional cascade axis"
#endif
#if defined(IGNORERIM) && defined(IGNOREROUGHNESS)
#  error "the combined IGNORERIM and IGNOREROUGHNESS cells are not admitted in Wave 1"
#endif

#include "Common/DeferredContracts.hlsli"

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_sss_params;

    float4 cb12_idx29_sss_angles;
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 LightPos_and_Radius;

    float4 LightColor_HDR;

    float4 cb2_idx3_attenuation_curve;

    float4 cb2_pad_4_10[7];

    float4 cb2_lightspace_row0;
    float4 cb2_lightspace_row1;
    float4 cb2_lightspace_row2;
    float4 cb2_lightspace_row3;
};

Texture2D<float4> g_tGbufferAlbedo : register(t0);

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);

Texture2D<float4> g_tMainDepth : register(t3);

Texture2D<float4> g_tLightCookie : register(t7);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);
SamplerState g_sLightCookie     : register(s7);

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  recon = 1.0 - encLenSq * 0.25;
    float  z = 1.0 - encLenSq * 0.5;
    float  scale = sqrt(recon);
#ifdef SPECULAR
    float3 normal;
    normal.xy = enc * scale;
    normal.z = -z;
    return normal;
#else
    return float3(enc * scale, -z);
#endif
}

float2 ProjectCookieUV(float4 dirLightSpace)
{
    float zHalf = dirLightSpace.z * 0.5 + 0.5;
    float3 projected = dirLightSpace.xyz / dirLightSpace.www;
    float projectedInvLength = rsqrt(dot(projected, projected));
    bool negativeHemisphere = zHalf < 0.0;
    float3 pole = negativeHemisphere ? float3(0.0, 0.0, -1.0)
                                     : float3(0.0, 0.0, 1.0);
    float3 d = projected * projectedInvLength + pole;
    d = normalize(d);
    float2 uv = d.xy / d.zz;
    uv = uv * 0.5 + 0.5;
    uv.y = negativeHemisphere ? (1.0 - uv.y) * 0.5 + 0.5 : uv.y * 0.5;
    return uv;
}

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    float linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 uvScreen = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float2 uvNDC = uvScreen * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float3 toLight    = LightPos_and_Radius.xyz - posView;
    float toLightLenSq = dot(toLight, toLight);
    float d = sqrt(toLightLenSq);
    float dNorm = saturate(d / LightPos_and_Radius.w);
    float dPowZ = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float falloffLin = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                + cb2_idx3_attenuation_curve.x);
    float attenuation = exp2(log2(1.0 - falloffLin) * 2.2);
#ifdef INVERSE_SQUARE_LIGHTING
    attenuation = InverseSquareLighting::GetAttenuation(
        attenuation, d, LightPos_and_Radius.w, input.position.x);
#endif

    if (attenuation <= 0.001)
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(toLightLenSq);
    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;
    float3 normalView = DecodeOctahedralNormal(normalEnc);
#ifndef IGNOREROUGHNESS
    float roughness01 = 1.0 - matSample.x;
#endif
#if !defined(IGNOREROUGHNESS) || defined(SPECULAR)
    float posViewLenInv = rsqrt(dot(-posView, -posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;
#endif

#ifdef SPECULAR
    float NdotL_raw = dot(normalView, lightDir);
    float NdotL_sat = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);
#endif

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
    float3 brdfSpecular = float3(0, 0, 0);
    float brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float albedoW = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
#if defined(IGNOREROUGHNESS) && !defined(SPECULAR)
        float posViewLenInv = rsqrt(dot(-posView, -posView));
        float3 viewDirNeg = -posView * posViewLenInv.xxx;
#endif
        float skinNdotL = dot(matSample.xyz, lightDir);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));
#ifndef SPECULAR
        float NdotL_sat = max(dot(normalView, lightDir), 0.0);
#endif

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + rot1Perp * sinScaleV, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity = saturate(cb12_idx28_sss_params.z * pow1 + NdotL_sat);
        brdfShadowMix = min(albedoW, sssIntensity);

#ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + rot2Perp * sinScaleV, 0.0);
        float pow2 = exp2(log2(vis2) * cb12_idx28_sss_params.y) *
            cb12_idx28_sss_params.x;
        brdfSpecular = (pow2 * LightColor_HDR.xyz) * NdotL_clamped;
#endif
    }
    else
    {
#ifdef SPECULAR
        float specExp = exp2(matSample.x * 10.0 + 1.0);
#endif
#ifndef IGNOREROUGHNESS
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
#ifndef SPECULAR
        float NdotL_raw = dot(lightDir, normalView);
#ifdef IGNOREROUGHNESS
        float NdotL_sat = max(NdotL_raw, 0.0);
#endif
#endif
#ifdef IGNOREROUGHNESS

        brdfShadowMix = NdotL_sat;
#else
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = lightDir - normalView * NdotL_raw;
        float tangentVLRaw = dot(tangentV, tangentL);

        float roughSq = roughness01 * roughness01;
        float2 visAB = roughSq.xx / (roughSq.xx + float2(0.57, 0.09));
        float visA = visAB.x;
        float visB = visAB.y;
        visA = 1.0 - 0.5 * visA;
        visB *= 0.45;
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float tangentDenom = max(NdotV_raw, NdotL_raw);
        float tangentRatio = tangentSin / tangentDenom;
#if !defined(SPECULAR) && !defined(IGNOREROUGHNESS)
        float NdotL_sat = max(NdotL_raw, 0.0);
#endif
        float tangentVL = max(tangentVLRaw, 0.0);
        float visibilityGeom = visB * tangentVL;
        visibilityGeom = visibilityGeom * tangentRatio + visA;
        brdfShadowMix = NdotL_sat * visibilityGeom;
#endif
#ifdef SPECULAR
        float3 halfVec = lightDir - posView * posViewLenInv;
        halfVec *= rsqrt(dot(halfVec, halfVec));
#ifdef IGNOREROUGHNESS
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
        float NdotV_sat = saturate(NdotV_raw);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));
        float distributionNorm = (specExp + 2.0) * 0.15915494;
#ifdef IGNORERIM
        float distribution = exp2(specExp * log2(NdotH));
#else
        float distribution = exp2(log2(NdotH) * specExp);
#endif
        distributionNorm *= distribution;
        float VdotH_nonneg = max(VdotH, asfloat(0x34000000));
        float minN = min(NdotV_sat, NdotL_clamped);
        float twoNdotH = NdotH + NdotH;
        bool usePeakRatio = (VdotH_nonneg >= minN * twoNdotH);
#ifdef IGNORERIM
        bool useUnityRatio = (minN == NdotV_sat);
#else
        bool useUnityRatio = (NdotV_sat == minN);
#endif
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
#ifdef IGNOREROUGHNESS
        float visibility = (ratio * twoNdotH) / VdotH_nonneg;
#else
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
#endif
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;
        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);
        float specMag = fresnelTerm * visibility;
        specMag *= distributionNorm;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= 3.1415927;
        brdfSpecular = (specMag * LightColor_HDR.xyz) * NdotL_clamped;
#endif
    }

#ifndef SPECULAR
    float NdotL_raw = dot(normalView, lightDir);
#ifndef IGNOREROUGHNESS
    float NdotL_clamped = saturate(NdotL_raw);
#endif
#endif
#ifdef IGNOREROUGHNESS
    float ambientTerm = 0.0;
#elif !defined(IGNORERIM)
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
#ifdef SPECULAR
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;
#else
    float ambientTerm = edge * toLightDotView * NdotL_clamped * roughness01;
#endif
#else
    float ambientTerm = 0.0;
#endif
    float3 diffuseAccum = LightColor_HDR.xyz * brdfShadowMix;
    diffuseAccum += LightColor_HDR.xyz * ambientTerm;

    float4 posViewHomog = float4(posView, 1.0);
    float4 lsDir;
    lsDir.x = dot(cb2_lightspace_row0, posViewHomog);
    lsDir.y = dot(cb2_lightspace_row1, posViewHomog);
    lsDir.z = dot(cb2_lightspace_row2, posViewHomog);
    lsDir.w = dot(cb2_lightspace_row3, posViewHomog);
    float2 cookieUV = ProjectCookieUV(lsDir);
    float3 cookieRGB = g_tLightCookie.Sample(g_sLightCookie, cookieUV).xyz;

    diffuseAccum *= cookieRGB;
#ifdef WETNESS_EFFECTS
    float wetness = WetnessEffects::GetWetness(
        normalView,
        float4(ViewToWorld_row2.xyz, 1.0));
    float3 wetViewDir = -posView * rsqrt(dot(posView, posView));
    float3 wetLightColor = (LightColor_HDR.xyz * cookieRGB) * attenuation;
    float3 wetDiffuse = diffuseAccum * attenuation;
#  ifdef SPECULAR
    float3 wetSpecular = (brdfSpecular * cookieRGB) * attenuation;
#  else
    float3 wetSpecular = float3(0, 0, 0);
#  endif
    WetnessEffects::ApplyDirectCoat(
        normalView,
        wetViewDir,
        lightDir,
        wetLightColor,
        wetness,
        wetDiffuse,
        wetSpecular);
    output.specular = float4(wetSpecular, 1.0);
    output.diffuse = float4(wetDiffuse, 0.0);
    output.diffuse /= 3.0;
#else
#ifdef SPECULAR
    output.specular.xyz = (brdfSpecular * cookieRGB) * attenuation;
#else
    output.specular.xyz = float3(0, 0, 0);
#endif
    output.specular.w = 1.0;
    output.diffuse = float4(diffuseAccum, 0.0);
    output.diffuse *= attenuation;
    output.diffuse /= 3.0;
#endif
    return output;
}

#endif

#ifdef BSDFLIGHT_PS_SHADOW_ONLY

#if !defined(DIRECTIONAL) || !defined(SHADOW_ONLY)
#  error "this source is the native DIRECTIONAL + SHADOW_ONLY family; define both"
#endif
#ifndef SHADOW
#  error "every native SHADOW_ONLY blob carries SHADOW=1"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 1
#  error "the reconstructed SHADOW_ONLY family is DIRSPLITS=1 only"
#endif
#if defined(AMBIENT) || defined(BLENDSPLIT)
#  error "SHADOW_ONLY does not support AMBIENT or BLENDSPLIT"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_POISSON) + defined(FILTER_PCSSPOISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif

#include "Common/DeferredContracts.hlsli"

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#include "Common/ShadowPoissonKernel.hlsli"
#endif

#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_POISSON)
#  define FO4_SHADOW_RAW_TAP 1
#endif

#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  define FO4_SHADOW_CMP_TAP 1
#endif

#if defined(FILTER_PCSS) || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  define FO4_SHADOW_DYNAMIC_CB2 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 cb12_pad_28_34[7];
    float4 CameraPosAdjust;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 cb2_pad_1_8[8];

    float4 cb2_idx9_cascade_slice;

    float4 cb2_pad_10;

    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;

    float4 cb2_pad_14_19[6];

    float4 cb2_idx20_shadow_sample_param;

    float4 cb2_idx21_cascade_world_scale[3];

    float4 cb2_idx24_distance_fade;
};

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);

Texture2D<float4> g_tMainDepth : register(t3);

#ifdef FO4_SHADOW_RAW_TAP

Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
SamplerState g_sCascadeShadowRaw : register(s4);
#endif

#ifdef FO4_SHADOW_CMP_TAP

Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);
#endif

SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

float ComputeSlopeBias(float c)
{
    float sqrtTerm = sqrt(1.0 - c);
    float acosApprox = ((-0.0187293 * c + 0.0742610) * c - 0.2121144) * c + 1.5707288;
    acosApprox *= sqrtTerm;
    float sinA, cosA;
    sincos(acosApprox, sinA, cosA);
    return (sinA / cosA) * 0.08;
}

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv4.xy,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    float  linearDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 screenUV = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float4 pos4 = float4(screenUV * 2.0 - 1.0, linearDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float  materialCode = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv4.xy).w;
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv4.xy).xy * 4.0 - 2.0;

    float encodedZ = 1.0 - dot(normalEnc, normalEnc) * 0.5;
    bool  isMaterial1 = abs(materialCode * 255.0 - 1.0) < 0.25;
    float slopeBias = ComputeSlopeBias(max(encodedZ, 0.0));

#ifdef FILTER_POISSON

    float poissonBias = isMaterial1 ? slopeBias : 0.275;
#endif

    float4 posViewH1 = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_shadowproj_row0, posViewH1);
    shadowUV.y = dot(cb2_shadowproj_row1, posViewH1);
    float shadowZ = min(dot(cb2_shadowproj_row2, posViewH1), 0.999999);
    float slice = cb2_idx9_cascade_slice.y;

#ifdef FILTER_POISSON

    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif

#ifdef FO4_SHADOW_DYNAMIC_CB2
    uint   cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
#endif

    float shadow;

#if defined(FILTER_PCF1)

    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
    shadow = g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), zRef);

#elif defined(FILTER_PCF9)

    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
    float sum = 0.0;
    [loop]
    for (int i = 0; i < 3; ++i)
    {
        float offsetX = float(i) - 1.0;
        [loop]
        for (int j = 0; j < 3; ++j)
        {
            float offsetY = float(j) - 1.0;
            float2 tapUV = float2(offsetX, offsetY)
                * cb2_idx20_shadow_sample_param.zw + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), zRef);
        }
    }
    shadow = sum * (1.0 / 9.0);

#elif defined(FILTER_PCSS)

    float2 searchStep = 1.0 / cascadeScale.xy;
    float  zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);

    float2 blocker = float2(0.0, 0.0);
    [loop]
    for (int bi = 0; bi < 5; ++bi)
    {
        float offsetX = float(bi - 2);
        [loop]
        for (int bj = 0; bj < 5; ++bj)
        {
            float offsetY = float(bj - 2);
            float2 tapUV = float2(offsetX, offsetY) * searchStep + shadowUV;
            float  tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool   isBlocker = tapDepth < zRef;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y != 0.0)
    {

        float centerDepth = g_tCascadeShadowRaw.Sample(
            g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
        float centerLit = (centerDepth >= zRef) ? 1.0 : 0.0;

        float averageBlocker = blocker.x / blocker.y;
        float worldRange = cascadeScale.w - cascadeScale.z;
        float receiverWorld = worldRange * zRef + cascadeScale.z;
        float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
        float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
        float penumbra = (blockerWorld < cascadeScale.z + 0.001)
            ? 1.9
            : (separation * 1.8 + 0.1);

        float sum = centerLit;
        [loop]
        for (int fi = 0; fi < 5; ++fi)
        {
            float offsetX = penumbra * (float(fi) - 2.0);
            [loop]
            for (int fj = 0; fj < 5; ++fj)
            {
                float offsetY = penumbra * (float(fj) - 2.0);
                float2 tapUV = (searchStep * float2(offsetX, offsetY)) * 0.5
                    + shadowUV;
                sum = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
                    g_sCascadeShadowCmp, float3(tapUV, slice), zRef);
            }
        }
        shadow = sum * 0.04;
    }
    else
    {
        shadow = 1.0;
    }

#elif defined(FILTER_POISSON)

    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - poissonBias * rcpWorldRange;

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), zRef);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), zRef);
    }
    shadow = sum * 0.0625;

#elif defined(FILTER_PCSSPOISSON)

    float2 searchStep = 1.0 / cascadeScale.xy;
    float  bias = isMaterial1 ? -slopeBias : -0.275;
    float  blockerRef = shadowZ + bias;

    float2 blocker = float2(0.0, 0.0);
    [loop]
    for (int bi = 0; bi < 5; ++bi)
    {
        float offsetX = float(bi - 2);
        [loop]
        for (int bj = 0; bj < 5; ++bj)
        {
            float offsetY = float(bj - 2);
            float2 tapUV = float2(offsetX, offsetY) * searchStep + shadowUV;
            float  tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool   isBlocker = tapDepth < blockerRef;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y != 0.0)
    {
        float averageBlocker = blocker.x / blocker.y;
        float worldRange = cascadeScale.w - cascadeScale.z;
        float receiverWorld = worldRange * blockerRef + cascadeScale.z;
        float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
        float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
        float penumbra = (blockerWorld < cascadeScale.z + 0.001)
            ? 1.9
            : (separation * 1.8 + 0.1);
        float kernelScale = penumbra * searchStep.x;
        float zRef = blockerRef + bias;

        float sum = 0.0;
        [loop]
        for (int k = 0; k < 8; ++k)
        {
            float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
            float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
            float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
            float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV.xy, slice), zRef);
            sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV.zw, slice), zRef);
        }
        shadow = sum * 0.0625;
    }
    else
    {
        shadow = 1.0;
    }

#else

    float sampledDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
    shadow = (sampledDepth >= zRef) ? 1.0 : 0.0;
#endif

    float distNorm = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2 = distNorm * distNorm;
    float dist4 = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;

    float3 result = fadeFactor * (shadow - 1.0) + 1.0;

#if defined(DIRECTIONAL) && defined(SCREEN_SPACE_SHADOWS)
    result *= ScreenSpaceShadows::GetScreenSpaceShadow(input.position.xy);
#endif
#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
    result *= TerrainShadows::GetTerrainShadowMultFromViewPosition(
        posView,
        TerrainShadows::TerrainShadowsSampler,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif
#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    result *= WaterEffects::GetCausticsMultFromViewPosition(
        posView,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif

    output.diffuse = result.zzzz;
    output.specular = float4(result.xyz, 1.0);
    return output;
}

#endif

#ifdef BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT

#if !defined(DIRECTIONAL) || !defined(SHADOW_ONLY) || !defined(BLENDSPLIT)
#  error "define DIRECTIONAL, SHADOW_ONLY, and BLENDSPLIT"
#endif
#if !defined(SHADOW) || !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native SHADOW_ONLY + BLENDSPLIT blob carries SHADOW, SPECULAR and RGBSPEC"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 1
#  error "the reconstructed SHADOW_ONLY + BLENDSPLIT family is DIRSPLITS=1 only"
#endif
#if defined(POINTOMNI) || defined(HALFOMNI) || defined(SPOT) || defined(POINTSPOT)
#  error "mixed light kinds; this family is DIRECTIONAL"
#endif
#if defined(LIGHT_TYPE)
#  error "LIGHT_TYPE is not a native macro here"
#endif
#if defined(AMBIENT_IBL_IN_LIGHT)
#  error "AMBIENT_IBL_IN_LIGHT is not a native macro here"
#endif
#if defined(GOBOPROJECTION)
#  error "GOBOPROJECTION declares t7/s7 and is a different resource contract"
#endif
#if defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON)
#  error "BLENDSPLIT does not support blocker search"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_POISSON)) != 1
#  error "define exactly one of FILTER_PCF1, FILTER_PCF9, FILTER_POISSON"
#endif

#include "Common/DeferredContracts.hlsli"

#ifdef FILTER_POISSON
#include "Common/ShadowPoissonKernel.hlsli"
#endif

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 cb12_pad_28_34[7];
    float4 CameraPosAdjust;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 cb2_pad_1_5[5];

    float4 cb2_ambient_row0;
    float4 cb2_ambient_row1;
    float4 cb2_ambient_row2;

    float4 cb2_idx9_cascade_slice;

    float4 cb2_pad_10;

    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;

    float4 cb2_pad_14_19[6];

    float4 cb2_idx20_shadow_sample_param;

    float4 cb2_idx21_cascade_world_scale[3];

    float4 cb2_idx24_distance_fade;
};

#ifdef AMBIENT

Texture2D<float4> g_tGbufferNormal : register(t1);
SamplerState g_sGbufferNormal : register(s1);

Texture2D<float4> g_tGbufferMaterial : register(t2);
SamplerState g_sGbufferMaterial : register(s2);
#endif

Texture2D<float4> g_tMainDepth : register(t3);
SamplerState g_sMainDepth : register(s3);

Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv4.xy,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    float  linearDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 screenUV = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float4 pos4 = float4(screenUV * 2.0 - 1.0, linearDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

#ifdef AMBIENT

    float3 material = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv4.xy).xyw;
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv4.xy).xy * 4.0 - 2.0;

    float  encSq = dot(normalEnc, normalEnc);
    float3 normal;
    normal.xy = normalEnc * sqrt(1.0 - encSq * 0.25);
    normal.z = -(1.0 - encSq * 0.5);

    float3 ambientDiffuse;
    ambientDiffuse.x = dot(cb2_ambient_row0, float4(normal, 1.0));
    ambientDiffuse.y = dot(cb2_ambient_row1, float4(normal, 1.0));
    ambientDiffuse.z = dot(cb2_ambient_row2, float4(normal, 1.0));
    ambientDiffuse = pow(ambientDiffuse, 2.2);

    bool isMaterial1 = abs(material.z * 255.0 - 1.0) < 0.25;
#endif

    float4 posViewH1 = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_shadowproj_row0, posViewH1);
    shadowUV.y = dot(cb2_shadowproj_row1, posViewH1);
    float shadowZ = min(dot(cb2_shadowproj_row2, posViewH1), 0.999999);
    float slice = cb2_idx9_cascade_slice.y;

    float shadow;

#if defined(FILTER_PCF1)

    shadow = g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)

    float sum = 0.0;
    [loop]
    for (int i = 0; i < 3; ++i)
    {
        float offsetX = float(i) - 1.0;
        [loop]
        for (int j = 0; j < 3; ++j)
        {
            float offsetY = float(j) - 1.0;
            float2 tapUV = float2(offsetX, offsetY)
                * cb2_idx20_shadow_sample_param.zw + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    shadow = sum * (1.0 / 9.0);

#elif defined(FILTER_POISSON)

    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
    uint  cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - 0.275 * rcpWorldRange;

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), zRef);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), zRef);
    }
    shadow = sum * 0.0625;
#else
#  error "unreachable: the filter guard above admits exactly PCF1, PCF9 or POISSON"
#endif

    float distNorm = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2 = distNorm * distNorm;
    float dist4 = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;

#ifndef AMBIENT
    float3 result = fadeFactor * (shadow - 1.0) + 1.0;

#if defined(DIRECTIONAL) && defined(SCREEN_SPACE_SHADOWS)
    result *= ScreenSpaceShadows::GetScreenSpaceShadow(input.position.xy);
#endif
#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
    result *= TerrainShadows::GetTerrainShadowMultFromViewPosition(
        posView,
        TerrainShadows::TerrainShadowsSampler,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif
#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    result *= WaterEffects::GetCausticsMultFromViewPosition(
        posView,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif

    output.diffuse = result.zzzz;
    output.specular = float4(result.xyz, 1.0);
#else
    float shadowBlend = fadeFactor * (shadow - 1.0);
    float splitShadow = shadowBlend + 1.0;

#if defined(DIRECTIONAL) && defined(SCREEN_SPACE_SHADOWS)
    splitShadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.position.xy);
#endif
#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
    splitShadow *= TerrainShadows::GetTerrainShadowMultFromViewPosition(
        posView,
        TerrainShadows::TerrainShadowsSampler,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif
#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    splitShadow *= WaterEffects::GetCausticsMultFromViewPosition(
        posView,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
#endif

    float3 ambientSpecular;
    [branch]
    if (isMaterial1)
    {
        ambientSpecular = 0.0;
    }
    else
    {
        float3 view = normalize(-posView);
        float  NdotV = dot(normal, view);
        float3 reflected = (NdotV + NdotV) * normal - view;
        float  fresnel = pow(1.0 - saturate(NdotV), 3.0 - material.x) * 0.25;

        float3 ambientReflected;
        ambientReflected.x = dot(cb2_ambient_row0, float4(reflected, 1.0));
        ambientReflected.y = dot(cb2_ambient_row1, float4(reflected, 1.0));
        ambientReflected.z = dot(cb2_ambient_row2, float4(reflected, 1.0));
        ambientSpecular = (fresnel * pow(ambientReflected, 2.2)) * material.y;
    }

#ifdef SKYLIGHTING
    Skylighting::Evaluation skylighting =
        Skylighting::Evaluate(posView, normal);
    Skylighting::ApplyAmbient(
        ambientDiffuse, ambientSpecular, skylighting);
#endif
    output.specular = float4(splitShadow.xxx, 1.0) + float4(ambientSpecular, 0.0);
    output.diffuse = float4(ambientDiffuse, 1.0) + float4(splitShadow.xxx, shadowBlend);
#endif
#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::ApplyFullscreenDebug(
        output.diffuse, output.specular, skylighting);
#endif
    return output;
}
#endif

#ifdef BSDFLIGHT_PS_UNSHADOWED

#if defined(SHADOW)
#  error "this block requires no SHADOW"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the DIRSPLITS=1 family in bsdf_light_deferred_shadow_only.hlsl"
#endif
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  error "FILTER_* requires SHADOW"
#endif
#ifdef HALFOMNI
#  error "HALFOMNI occurs only with POINTOMNI and SHADOW; it is not part of this layer"
#endif
#ifdef GOBOPROJECTION
#  error "GOBOPROJECTION blobs declare t7/s7 and are a different resource contract"
#endif
#ifdef ATTENUATION_ONLY
#  error "ATTENUATION_ONLY belongs to its separate block"
#endif
#if defined(SPOT) || defined(POINTSPOT)
#  error "this block excludes SPOT"
#endif
#if (defined(DIRECTIONAL) + defined(POINTOMNI)) != 1
#  error "define exactly one of DIRECTIONAL or POINTOMNI"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS explicitly"
#endif
#if DIRSPLITS != 2
#  error "this block requires DIRSPLITS=2"
#endif
#if !defined(RGBSPEC)
#  error "every native no-SHADOW DIRSPLITS=2 blob carries RGBSPEC"
#endif

#ifdef DIRECTIONAL
#  ifdef IGNORERIM
#    error "DIRECTIONAL does not support IGNORERIM"
#  endif
#  if defined(AMBIENT) && !defined(SPECULAR)
#    error "DIRECTIONAL + AMBIENT requires SPECULAR"
#  endif
#  if defined(IGNOREROUGHNESS) && !defined(SPECULAR)
#    error "DIRECTIONAL + IGNOREROUGHNESS requires SPECULAR"
#  endif
#endif

#ifdef POINTOMNI
#  ifdef AMBIENT
#    error "no POINTOMNI blob carries AMBIENT; its CB2[4] has no DirectionalAmbient rows"
#  endif
#  if defined(IGNOREROUGHNESS) && defined(IGNORERIM)
#    error "no POINTOMNI blob carries both IGNOREROUGHNESS and IGNORERIM"
#  endif
#endif

#include "Common/DeferredContracts.hlsli"

#if defined(DIRECTIONAL) && defined(SPECULAR)
#  define FO4_UNSHADOWED_USES_GLOSS_FRESNEL 1
#endif
#if defined(AMBIENT) && defined(IGNOREROUGHNESS)
#  define FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS 1
#endif
#if defined(AMBIENT) && !defined(IGNOREROUGHNESS)
#  define FO4_UNSHADOWED_AMBIENT_ROUGHNESS 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_hair_spec_params;

    float4 cb12_idx29_hair_spec_shifts;

#if defined(DIRECTIONAL) && defined(SPECULAR)
    float4 cb12_idx30;
#elif defined(DIRECTIONAL) && (defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS))
    float4 cb12_idx30_terrain_pad;
#endif

#if defined(DIRECTIONAL) && (defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS))
    float4 cb12_pad_31_34[4];
    float4 CameraPosAdjust;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{

    float4 ScreenSize;

    float4 LightVector;

    float4 LightColor_HDR;

#if defined(POINTOMNI)

    float4 LightAttenuation;
#elif defined(AMBIENT)

    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#endif
};

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
static const float FO4_SPECULAR_SCALE = 3.1415927;
#else
static const float FO4_SPECULAR_SCALE = 3.1415927;
#endif

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    float  recon = 1.0 - encLenSq * 0.25;
    float2 xy = enc * sqrt(recon);
    return float3(xy, -(1.0 - encLenSq * 0.5));
#else
    float  recon = 1.0 - encLenSq * 0.25;
    float  z = 1.0 - encLenSq * 0.5;
    float  scale = sqrt(recon);
#ifdef SPECULAR
    float3 normal;
    normal.xy = enc * scale;
    normal.z = -z;
    return normal;
#else
    return float3(enc * scale, -z);
#endif
#endif
}

#ifdef AMBIENT
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(cb2_ambient_gradient_row0, directionH);
    encoded.y = dot(cb2_ambient_gradient_row1, directionH);
    encoded.z = dot(cb2_ambient_gradient_row2, directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    float  linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    if (depth <= 0.01)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 uvFlipped = float2(uv4.z, 1.0 - uv4.w);
    float2 uvNDC = uvFlipped * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

#ifdef POINTOMNI

    float3 toLight   = LightVector.xyz - posView;
    float  distSq    = dot(toLight, toLight);
    float  distNorm  = saturate(sqrt(distSq) / LightVector.w);
    float  falloff   = exp2(log2(distNorm) * LightAttenuation.z);
    float  attenBase = saturate(LightAttenuation.y * falloff + LightAttenuation.x);
    float  attenuation = exp2(log2(1.0 - attenBase) * 2.2);
#ifdef INVERSE_SQUARE_LIGHTING
    attenuation = InverseSquareLighting::GetAttenuation(
        attenuation, sqrt(distSq), LightVector.w, input.position.x);
#endif

    if (attenuation <= 0.001)
    {
        output.diffuse  = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(distSq);
#else
    float3 lightDir = LightVector.xyz;
#endif

    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
#ifdef DIRECTIONAL
    float4 albedoSample = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
#endif
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
    float roughness01 = 1.0 - matSample.x;
#endif

#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    float  posViewLen = rsqrt(dot(-posView, -posView));
    float3 viewDirNeg = -posView * posViewLen;
#endif

#ifdef AMBIENT
    float3 ambientDiffuse  = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
    float NdotV_raw = dot(normalView, viewDirNeg);
    float oneMinusNdotV = 1.0 - saturate(NdotV_raw);
    float oneMinusNdotVLog = log2(oneMinusNdotV);
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
    float3 albedoPremult = albedoSample.xyz * albedoSample.w;
#endif

#ifdef FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
    float3 albedoPremult = albedoSample.xyz * albedoSample.w;
#else
#if !defined(IGNOREROUGHNESS) && !defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    float roughness01 = 1.0 - matSample.x;
#endif
#endif

#if !defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    && !defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS) \
    && (!defined(POINTOMNI) || !defined(IGNOREROUGHNESS) || defined(SPECULAR))
    float  posViewLen = rsqrt(dot(-posView, -posView));
    float3 viewDirNeg = -posView * posViewLen;
#endif
#if defined(DIRECTIONAL) && !defined(AMBIENT)
    float3 albedoPremult = albedoSample.xyz * albedoSample.w;
#endif

#ifdef SPECULAR
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_pos     = max(NdotL_raw, 0.0);
    float NdotL_clamped = min(NdotL_pos, 1.0);
#endif

#ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
    float oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;
#endif

#if !defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    && !defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
#endif

    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfShadowMix = 0.0;
#ifdef DIRECTIONAL
    float  brdfModulator = 0.0;
#endif

    if (isMaterial1)
    {

#ifdef POINTOMNI
        float albedoAlpha = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
#if defined(IGNOREROUGHNESS) && !defined(SPECULAR)
        float  posViewLen = rsqrt(dot(-posView, -posView));
        float3 viewDirNeg = -posView * posViewLen;
#endif
#else
        float albedoAlpha = albedoSample.w;
#endif
        float skinNdotL  = dot(matSample.xyz, lightDir);
        float skinNdotV  = dot(matSample.xyz, viewDirNeg);
        float sinScaleL  = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV  = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));
#ifndef SPECULAR
        float NdotL_pos  = max(dot(normalView, lightDir), 0.0);
#endif

        float sinA1, cosA1;
        sincos(cb12_idx29_hair_spec_shifts.y, sinA1, cosA1);
        float rot1     = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
#ifdef AMBIENT
        float vis1     = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
#else
        float vis1     = max(rot1 * skinNdotV + rot1Perp * sinScaleV, 0.0);
#endif
        float pow1     = exp2(log2(vis1) * cb12_idx28_hair_spec_params.w);
        float hairIntensity =
            saturate(cb12_idx28_hair_spec_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoAlpha, hairIntensity);

#ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_hair_spec_shifts.x, sinA2, cosA2);
        float rot2     = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
        float vis2     = max(rot2 * skinNdotV + rot2Perp * sinScaleV, 0.0);
#else
        float vis2     = max(rot2 * skinNdotV + rot2Perp * sinScaleV, 0.0);
#endif
        float pow2     = exp2(log2(vis2) * cb12_idx28_hair_spec_params.y)
            * cb12_idx28_hair_spec_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
#endif
    }
    else
    {
#if defined(DIRECTIONAL) && !defined(AMBIENT)
        brdfModulator = matSample.z * 100.0;
#ifdef SPECULAR
        float specExpBase = exp2(matSample.x * 10.0 + 1.0);
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#endif
#endif
#if defined(POINTOMNI) && defined(SPECULAR) && !defined(AMBIENT)
        float specExpBase = exp2(matSample.x * 10.0 + 1.0);
#endif
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        brdfModulator = matSample.z * 100.0;
        float specExpBase = exp2(matSample.x * 10.0 + 1.0);
#endif
#ifdef FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS
        float specExpBase = exp2(matSample.x * 10.0 + 1.0);
        float NdotV_raw = dot(normalView, viewDirNeg);
#elif !defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS) \
    && !defined(IGNOREROUGHNESS)
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
#ifndef SPECULAR
        float NdotL_raw = dot(lightDir, normalView);
#ifdef IGNOREROUGHNESS
        float NdotL_pos = max(NdotL_raw, 0.0);
#endif
#endif

#ifdef AMBIENT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
#ifndef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        float  oneMinusNdotV = 1.0 - saturate(NdotV_raw);
#endif
#ifdef IGNOREROUGHNESS
        float  ambientSpecularFactor =
            oneMinusNdotV * oneMinusNdotV * 0.25;
#else
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        float  ambientSpecularFactor =
            exp2(oneMinusNdotVLog * (3.0 - matSample.x)) * 0.25;
#else
        float  ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
#endif
#endif
        ambientSpecular = ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir) * matSample.y;
#endif

#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp = specExpBase * specExpScale;
#endif

#ifdef IGNOREROUGHNESS

        brdfShadowMix = NdotL_pos;
#else
        float3 tangentV  = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL  = lightDir - normalView * NdotL_raw;
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        float tangentVLDot = dot(tangentV, tangentL);
#else
        float tangentVLDot = dot(tangentV, tangentL);
#endif

        float roughSq = roughness01 * roughness01;
        float visA    = roughSq / (roughSq + 0.57);
        float visB    = roughSq / (roughSq + 0.09);
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        visA = 1.0 - 0.5 * visA;
        visB *= 0.45;
#else
        visA = 1.0 - 0.5 * visA;
        visB *= 0.45;
#endif

#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        float tangentSin   = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                           * (1.0 - NdotL_raw * NdotL_raw)));
        float tangentDenom = max(NdotL_raw, NdotV_raw);
#else
        float tangentSin   = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                           * (1.0 - NdotL_raw * NdotL_raw)));
        float tangentDenom = max(NdotV_raw, NdotL_raw);
#endif
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        float tangentRatio = tangentSin / tangentDenom;
        float tangentVL = max(tangentVLDot, 0.0);
#else
        float tangentRatio = tangentSin / tangentDenom;
#if !defined(SPECULAR) && !defined(IGNOREROUGHNESS)
        float NdotL_pos = max(NdotL_raw, 0.0);
#endif
        float tangentVL = max(tangentVLDot, 0.0);
#endif
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        float visibilityGeom = visB * tangentVL;
#else
        float visibilityGeom = visB * tangentVL;
#endif
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
        visibilityGeom = visibilityGeom * tangentRatio + visA;
#else
        visibilityGeom = visibilityGeom * tangentRatio + visA;
#endif
        brdfShadowMix  = NdotL_pos * visibilityGeom;
#endif

#ifdef SPECULAR
#ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
#ifdef FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS
        float specExpScale = mad(schlickFres, -0.98, 1.0);
        float specExp      = specExpBase * specExpScale;
#endif
#else

        float specExp = specExpBase;
#endif

        float3 halfVec = lightDir - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

#if !defined(AMBIENT) && defined(IGNOREROUGHNESS)
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
        float NdotV_sat = saturate(NdotV_raw);
        float VdotH     = saturate(dot(viewDirNeg, halfVec));
        float NdotH     = saturate(dot(halfVec, normalView));

#ifndef AMBIENT
#ifdef DIRECTIONAL
        float distributionNorm =
            (specExpScale * specExpBase + 2.0) * 0.15915494;
#else
        float distributionNorm = (specExpBase + 2.0) * 0.15915494;
#endif
#endif
#ifdef FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS
        float distributionNorm = (specExpBase * specExpScale + 2.0) * 0.15915494;
        float distribution = exp2(specExp * log2(NdotH));
#elif defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
        float distributionNorm = (specExpBase * specExpScale + 2.0) * 0.15915494;
        float distribution = exp2(specExp * log2(NdotH));
#elif defined(POINTOMNI) && !defined(IGNOREROUGHNESS)
        float distribution = exp2(specExp * log2(NdotH));
#elif defined(DIRECTIONAL) && !defined(AMBIENT)
        float distribution = exp2(specExp * log2(NdotH));
#else
        float distribution = exp2(log2(NdotH) * specExp);
#endif
        distributionNorm *= distribution;

#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
        float VdotH_nonneg = max(VdotH, 1.1920929e-07);
#else
        float VdotH_nonneg = max(VdotH, 1.1920929e-07);
#endif
#if defined(IGNOREROUGHNESS) || !defined(AMBIENT)
        float minN         = min(NdotV_sat, NdotL_clamped);
#else
        float minN         = min(NdotL_clamped, NdotV_sat);
#endif
        float twoNdotH     = NdotH + NdotH;
        bool  usePeakRatio = (VdotH_nonneg >= minN * twoNdotH);
#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) || !defined(AMBIENT)
        bool  useUnityRatio = (minN == NdotV_sat);
#else
        bool  useUnityRatio = (NdotV_sat == minN);
#endif
        float ratioNLNV    = NdotL_clamped / NdotV_sat;
        float ratio        = useUnityRatio ? 1.0 : ratioNLNV;
#ifndef AMBIENT
        float visibility   = (ratio * twoNdotH) / VdotH_nonneg;
#else
        float visibility   = (twoNdotH * ratio) / VdotH_nonneg;
#endif
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH  = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm    = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = fresnelTerm * visibility;
        specMag *= distributionNorm;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_SPECULAR_SCALE;

        brdfSpecular = (specMag * LightColor_HDR.xyz) * NdotL_clamped;
#endif

#if defined(DIRECTIONAL) && defined(AMBIENT) \
    && !defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
        brdfModulator = matSample.z * 100.0;
#endif
    }

#ifndef SPECULAR
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_clamped = saturate(NdotL_raw);
#endif

    float3 finalDiffuse = LightColor_HDR.xyz * brdfShadowMix;

#if !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)

    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = exp2(log2(1.0 - NdotV_view) * 0.01);
    float fresEdge    = saturate(dot(viewDirNeg, -lightDir));
#ifdef FO4_UNSHADOWED_AMBIENT_ROUGHNESS
    float ambientTerm = ambientFres * fresEdge * NdotL_clamped * roughness01;
#elif !defined(SPECULAR)
    float ambientTerm = ambientFres * fresEdge * NdotL_clamped * roughness01;
#elif defined(DIRECTIONAL)
    float ambientTerm = ambientFres * fresEdge * NdotL_clamped * roughness01;
#else
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;
#endif

    finalDiffuse += LightColor_HDR.xyz * ambientTerm;
#endif

#ifdef DIRECTIONAL
    float backfaceWrap = saturate(-NdotL_raw);
#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    finalDiffuse += LightColor_HDR.xyz * (backfaceWrap * albedoPremult);
#else
    finalDiffuse += LightColor_HDR.xyz * (backfaceWrap * albedoPremult);
#endif

#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    float forwardBlend =
        saturate((NdotL_raw + brdfModulator) / (brdfModulator + 1.0));
#else
    float forwardBlend =
        saturate((NdotL_raw + brdfModulator) / (brdfModulator + 1.0));
#endif
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * LightColor_HDR.xyz) * albedoSample.xyz;
#endif

#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)
    float terrainShadowMult = TerrainShadows::GetTerrainShadowMultFromViewPosition(
        posView,
        TerrainShadows::TerrainShadowsSampler,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
    finalDiffuse *= terrainShadowMult;
    brdfSpecular *= terrainShadowMult;
#endif
#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    float causticsMult = WaterEffects::GetCausticsMultFromViewPosition(
        posView,
        ViewToWorld_row0,
        ViewToWorld_row1,
        ViewToWorld_row2,
        CameraPosAdjust);
    finalDiffuse *= causticsMult;
    brdfSpecular *= causticsMult;
#endif

#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::Evaluation skylighting =
        Skylighting::Evaluate(posView, normalView);
    Skylighting::ApplyAmbient(
        ambientDiffuse, ambientSpecular, skylighting);
#endif
#ifdef WETNESS_EFFECTS
    float wetness = WetnessEffects::GetWetness(
        normalView,
        float4(ViewToWorld_row2.xyz, 1.0));
    float3 wetViewDir = -posView * rsqrt(dot(posView, posView));
#  ifdef POINTOMNI
    float3 wetLightColor = LightColor_HDR.xyz * attenuation;
    float3 wetDiffuse = finalDiffuse * attenuation;
#  else
    float3 wetLightColor = LightColor_HDR.xyz;
#    if defined(DIRECTIONAL) && defined(WATER_EFFECTS)
    // finalDiffuse and brdfSpecular already carry caustics; the coat's own sun
    // lobe is built from the raw light color, so modulate it too.
    wetLightColor *= causticsMult;
#    endif
    float3 wetDiffuse = finalDiffuse;
#  endif
#  ifdef SPECULAR
#    ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
    float3 wetSpecular = brdfSpecular * mad(schlickFres, -0.5, 1.0);
#    else
    float3 wetSpecular = attenuation * brdfSpecular;
#    endif
#  else
    float3 wetSpecular = float3(0, 0, 0);
#  endif
    WetnessEffects::ApplyDirectCoat(
        normalView,
        wetViewDir,
        lightDir,
        wetLightColor,
        wetness,
        wetDiffuse,
        wetSpecular);
#  ifdef AMBIENT
    output.specular = float4(wetSpecular + ambientSpecular, 1.0);
    output.diffuse = float4(wetDiffuse + ambientDiffuse, 0.0);
#  else
    output.specular = float4(wetSpecular, 1.0);
    output.diffuse = float4(wetDiffuse, 0.0);
#  endif
    output.diffuse /= 3.0;
#else
#ifdef SPECULAR
#  ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
#    if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
        || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    float specularFresnelScale = mad(schlickFres, -0.5, 1.0);
        output.specular = float4(0.0, 0.0, 0.0, 1.0);
        output.specular += float4(
            mad(brdfSpecular, specularFresnelScale, ambientSpecular), 0.0);
#    else
    float specularFresnelScale = mad(schlickFres, -0.5, 1.0);
    output.specular.xyz = brdfSpecular * specularFresnelScale;
#    endif
#  else
    output.specular.xyz = attenuation * brdfSpecular;
#  endif
#else
    output.specular.xyz = float3(0, 0, 0);
#endif
#if defined(AMBIENT) \
    && !defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    && !defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    output.specular.xyz += ambientSpecular;
#endif
#if !defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    && !defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    output.specular.w = 1.0;
#endif

#if defined(FO4_UNSHADOWED_AMBIENT_IGNORE_ROUGHNESS) \
    || defined(FO4_UNSHADOWED_AMBIENT_ROUGHNESS)
    output.diffuse = float4(ambientDiffuse, 0.0);
    output.diffuse += float4(finalDiffuse, 0.0);
    output.diffuse /= 3.0;
#else
    output.diffuse = float4(finalDiffuse, 0.0);
#  ifdef POINTOMNI
    output.diffuse *= attenuation;
#  endif
    output.diffuse /= 3.0;
#endif
#endif

#if defined(AMBIENT) && defined(SKYLIGHTING)
    Skylighting::ApplyFullscreenDebug(
        output.diffuse, output.specular, skylighting);
#endif
    return output;
}

#endif

#ifdef BSDFLIGHT_PS_AMBIENT

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_19[20];
    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;
    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 cb2_pad_1_5[5];
    float4 DirectionalAmbient_row0;
    float4 DirectionalAmbient_row1;
    float4 DirectionalAmbient_row2;
};

Texture2D<float4> g_tGbufferNormal : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tGbufferShadingData : register(t3);
SamplerState g_sGbufferNormal : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sGbufferShadingData : register(s3);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 unusedPosition : POSITION14;
};

struct PS_OUTPUT
{
    float4 specular : SV_Target1;
    float4 diffuse : SV_Target0;
};

float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded = float3(
        dot(DirectionalAmbient_row0, directionH),
        dot(DirectionalAmbient_row1, directionH),
        dot(DirectionalAmbient_row2, directionH));
    return exp2(log2(encoded) * 2.2);
}

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    float4 screen = input.position.xyxy * ScreenSize;
    float2 uv = screen.xy;
    float gradientX = ddx(uv.x);
    float gradientY = ddy(uv.y);
    float depth = g_tGbufferShadingData.SampleGrad(
        g_sGbufferShadingData, uv, gradientX.xx, gradientY.xx).x;

    float linearDepth;
    float4 reprojRow0;
    float4 reprojRow1;
    float4 reprojRow2;
    float4 reprojRow3;
    if (depth <= 0.01)
    {
        linearDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 ndc = float2(screen.z, 1.0 - screen.w) * 2.0 - 1.0;
    float4 positionInput = float4(ndc, linearDepth, 1.0);
    float4 positionViewH = float4(
        dot(reprojRow0, positionInput),
        dot(reprojRow1, positionInput),
        dot(reprojRow2, positionInput),
        dot(reprojRow3, positionInput));
    float3 positionView = positionViewH.xyz / positionViewH.w;

    float3 material =
        g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv).xyw;
    float2 encodedNormal =
        g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy * 4.0 - 2.0;
    float encodedLengthSquared = dot(encodedNormal, encodedNormal);
    float3 normalView = float3(
        encodedNormal * sqrt(1.0 - encodedLengthSquared * 0.25),
        -(1.0 - encodedLengthSquared * 0.5));
    float3 viewDirection = normalize(-positionView);

    float3 ambientDiffuse = EvaluateAmbientGradient(normalView);
    float ndotv = dot(normalView, viewDirection);
    float3 reflectionDirection = 2.0 * ndotv * normalView - viewDirection;
    float oneMinusNdotV = 1.0 - saturate(ndotv);
    float ambientExponent = 3.0 - material.x;
    float ambientSpecularFactor =
        exp2(ambientExponent * log2(oneMinusNdotV)) * 0.25;
    float3 ambientSpecular =
        EvaluateAmbientGradient(reflectionDirection) * ambientSpecularFactor;
    bool isMaterial1 = abs(material.z * 255.0 - 1.0) < 0.25;
    ambientSpecular *= isMaterial1 ? 0.0 : material.y;

#ifdef SKYLIGHTING
    Skylighting::Evaluation skylighting =
        Skylighting::Evaluate(positionView, normalView);
    Skylighting::ApplyAmbient(
        ambientDiffuse, ambientSpecular, skylighting);
#endif
    PS_OUTPUT output;
    output.specular = float4(0.0, 0.0, 0.0, 1.0);
    output.specular += float4(ambientSpecular, 0.0);
    output.diffuse = float4(0.0, 0.0, 0.0, 0.0);
    output.diffuse += float4(ambientDiffuse / 3.0, 0.0);
#ifdef SKYLIGHTING
    Skylighting::ApplyFullscreenDebug(
        output.diffuse, output.specular, skylighting);
#endif
    return output;
}
#endif

#ifdef BSDFLIGHT_PS_CHARACTER_LIGHT

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_19[20];
    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;
    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 cb2_pad_1_5[5];
    float4 DirectionalAmbient_row0;
    float4 DirectionalAmbient_row1;
    float4 DirectionalAmbient_row2;
    float4 cb2_pad_9_20[12];
    float4 CharacterLightParams;
};

Texture2D<float4> g_tGbufferNormal : register(t1);
Texture2D<float4> g_tGbufferShadingData : register(t3);
Texture2D<float4> g_tCharacterLightScale : register(t8);
SamplerState g_sGbufferNormal : register(s1);
SamplerState g_sGbufferShadingData : register(s3);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 unusedPosition : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    float4 screen = input.position.xyxy * ScreenSize;
    float2 uv = screen.xy;
    float gradientX = ddx(uv.x);
    float gradientY = ddy(uv.y);
    float depth = g_tGbufferShadingData.SampleGrad(
        g_sGbufferShadingData, uv, gradientX.xx, gradientY.xx).x;

    float linearDepth;
    float4 reprojRow0;
    float4 reprojRow1;
    float4 reprojRow2;
    float4 reprojRow3;
    if (depth <= 0.01)
    {
        linearDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 ndc = float2(screen.z, 1.0 - screen.w) * 2.0 - 1.0;
    float4 positionInput = float4(ndc, linearDepth, 1.0);
    float4 positionViewH = float4(
        dot(reprojRow0, positionInput),
        dot(reprojRow1, positionInput),
        dot(reprojRow2, positionInput),
        dot(reprojRow3, positionInput));
    float3 positionView = positionViewH.xyz / positionViewH.w;

    float2 encodedNormal =
        g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy * 4.0 - 2.0;
    float encodedLengthSquared = dot(encodedNormal, encodedNormal);
    float3 normalView = float3(
        encodedNormal * sqrt(1.0 - encodedLengthSquared * 0.25),
        -(1.0 - encodedLengthSquared * 0.5));

    float3 eyeDirection = normalize(-positionView);
    float4 normalH = float4(normalView, 1.0);
    float3 ambientEncoded = float3(
        dot(DirectionalAmbient_row0, normalH),
        dot(DirectionalAmbient_row1, normalH),
        dot(DirectionalAmbient_row2, normalH));
    float3 ambientLinear = exp2(log2(ambientEncoded) * 2.2);

    float lobeA = saturate(dot(float3(-1.5, 1.0, 0.0) - eyeDirection, normalView));
    float lobeB = saturate(dot(float3(1.0, 1.0, 0.0) - eyeDirection, normalView));
    float lobeC = saturate(dot(float2(0.125, -0.75), normalView.yz));
    float lightAmount = mad(
        lobeB, CharacterLightParams.x,
        lobeA * CharacterLightParams.x);
    lightAmount = mad(lobeC, CharacterLightParams.y, lightAmount);
    float3 color = lightAmount * normalize(ambientLinear);
    float scale = g_tCharacterLightScale.Load(int3(0, 0, 0)).x * 96.0 + 1.5;

    PS_OUTPUT output;
    output.diffuse = float4(color * scale, 1.0);
    output.specular = 0.0;
    return output;
}
#endif

#ifdef BSDFLIGHT_PS_CHARACTER_LIGHT_C26

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_19[20];
    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;
    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 cb2_pad_1_5[5];
    float4 DirectionalAmbient_row0;
    float4 DirectionalAmbient_row1;
    float4 DirectionalAmbient_row2;
    float4 cb2_pad_9_25[17];
    float4 CharacterLightParams;
};

Texture2D<float4> g_tGbufferNormal : register(t1);
Texture2D<float4> g_tGbufferShadingData : register(t3);
Texture2D<float4> g_tCharacterLightScale : register(t8);
SamplerState g_sGbufferNormal : register(s1);
SamplerState g_sGbufferShadingData : register(s3);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 unusedPosition : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    float4 screen = input.position.xyxy * ScreenSize;
    float2 uv = screen.xy;
    float gradientX = ddx(uv.x);
    float gradientY = ddy(uv.y);
    float depth = g_tGbufferShadingData.SampleGrad(
        g_sGbufferShadingData, uv, gradientX.xx, gradientY.xx).x;

    float linearDepth;
    float4 reprojRow0;
    float4 reprojRow1;
    float4 reprojRow2;
    float4 reprojRow3;
    if (depth <= 0.01)
    {
        linearDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 ndc = float2(screen.z, 1.0 - screen.w) * 2.0 - 1.0;
    float4 positionInput = float4(ndc, linearDepth, 1.0);
    float4 positionViewH = float4(
        dot(reprojRow0, positionInput),
        dot(reprojRow1, positionInput),
        dot(reprojRow2, positionInput),
        dot(reprojRow3, positionInput));
    float3 positionView = positionViewH.xyz / positionViewH.w;

    float2 encodedNormal =
        g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy * 4.0 - 2.0;
    float encodedLengthSquared = dot(encodedNormal, encodedNormal);
    float3 normalView = float3(
        encodedNormal * sqrt(1.0 - encodedLengthSquared * 0.25),
        -(1.0 - encodedLengthSquared * 0.5));

    float3 eyeDirection = normalize(-positionView);
    float4 normalH = float4(normalView, 1.0);
    float3 ambientEncoded = float3(
        dot(DirectionalAmbient_row0, normalH),
        dot(DirectionalAmbient_row1, normalH),
        dot(DirectionalAmbient_row2, normalH));
    float3 ambientLinear = exp2(log2(ambientEncoded) * 2.2);

    float lobeA = saturate(dot(float3(-1.5, 1.0, 0.0) - eyeDirection, normalView));
    float lobeB = saturate(dot(float3(1.0, 1.0, 0.0) - eyeDirection, normalView));
    float lobeC = saturate(dot(float2(0.125, -0.75), normalView.yz));
    float lightAmount = mad(
        lobeB, CharacterLightParams.x,
        lobeA * CharacterLightParams.x);
    lightAmount = mad(lobeC, CharacterLightParams.y, lightAmount);
    float3 color = lightAmount * normalize(ambientLinear);
    float scale = g_tCharacterLightScale.Load(int3(0, 0, 0)).x * 96.0 + 1.5;

    PS_OUTPUT output;
    output.diffuse = float4(color * scale, 1.0);
    output.specular = 0.0;
    return output;
}
#endif

#ifdef BSDFLIGHT_PS_OVERDRAW

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 unusedPosition : POSITION14;
};

struct PS_OUTPUT
{
    float4 overdraw : SV_Target0;
    float4 auxiliary : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
#ifdef SKYLIGHTING
    Skylighting::DiscardNonConsumerDebug();
#endif
    PS_OUTPUT output;
    output.overdraw = float4(0.0625, 0.0625, 0.0625, 0.0);
    output.auxiliary = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
#endif

#ifdef BSDFLIGHT_VS
// FO4RE_BLOCK_GUARD_BEGIN BSDFLIGHT_VS
#if defined(BSDFLIGHT_PS_DEFERRED) \
 || defined(BSDFLIGHT_PS_ATTENUATION_ONLY) \
 || defined(BSDFLIGHT_PS_DIRSPLITS1) \
 || defined(BSDFLIGHT_PS_DIRSPLITS2) \
 || defined(BSDFLIGHT_PS_DIRSPLITS3) \
 || defined(BSDFLIGHT_PS_GOBO) \
 || defined(BSDFLIGHT_PS_SHADOW_ONLY) \
 || defined(BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT) \
 || defined(BSDFLIGHT_PS_UNSHADOWED) \
 || defined(BSDFLIGHT_PS_AMBIENT) \
 || defined(BSDFLIGHT_PS_CHARACTER_LIGHT) \
 || defined(BSDFLIGHT_PS_CHARACTER_LIGHT_C26) \
 || defined(BSDFLIGHT_PS_OVERDRAW)
#  error "BSDFLIGHT_VS is exclusive with every other selector block"
#endif
// FO4RE_BLOCK_GUARD_END BSDFLIGHT_VS
struct VSInput
{
    float4 position : POSITION0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 position14 : POSITION14;
};

cbuffer PerGeometry : register(b2)
{
    row_major float4x4 transform;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 position = float4(input.position.xyz, 1.0);
    output.position = mul(transform, position);
    output.position.z = min(output.position.z, output.position.w);
    output.position14 = 1.0;
    return output;
}
#endif
