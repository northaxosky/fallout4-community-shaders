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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

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

#ifdef SCREEN_SPACE_SHADOWS

#include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#endif

#ifdef WETNESS_EFFECTS
Texture2D<float> g_tWetnessMask : register(t25);
#endif

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
    PS_OUTPUT output;

#ifdef WETNESS_EFFECTS
    float wetness = saturate(
        g_tWetnessMask.Load(int3(int2(input.position.xy), 0)).x);
#endif

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

#ifdef SCREEN_SPACE_SHADOWS

    shadowPcf *= ScreenSpaceShadows::GetScreenSpaceShadow(input.position.xy);
#endif

    float3 albedoPremult = albedoSample.w * albedoSample.xyz;
    float  NdotL_raw     = dot(normalView, SunDirection_and_padding.xyz);
    float  NdotL_pos     = max(NdotL_raw, 0.0);
    float  NdotL_clamped = min(NdotL_pos, 1.0);
    float  oneMinusGloss = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres   = 1.0 - oneMinusGloss * oneMinusGloss4;

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

#ifdef WETNESS_EFFECTS
    float wetFilmRoughness = max(saturate(1.0 - wetness), 0.05);
    float wetFilmStrength = saturate(1.0 - wetFilmRoughness);
    float3 wetFilmNormal = isMaterial1 ? matSample.xyz : normalView;
    wetFilmNormal *=
        rsqrt(max(dot(wetFilmNormal, wetFilmNormal), 1.0e-8));
    float3 wetFilmHalf = viewDirNeg + SunDirection_and_padding.xyz;
    wetFilmHalf *= rsqrt(max(dot(wetFilmHalf, wetFilmHalf), 1.0e-8));
#endif

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

#ifdef WETNESS_EFFECTS
    float wetNdotL = clamp(
        dot(wetFilmNormal, SunDirection_and_padding.xyz), 1.0e-5, 1.0);
    float wetNdotV =
        saturate(abs(dot(wetFilmNormal, viewDirNeg)) + 1.0e-5);
    float wetNdotH = saturate(dot(wetFilmNormal, wetFilmHalf));
    float wetVdotH = saturate(dot(viewDirNeg, wetFilmHalf));
    float wetOneMinusVdotH = 1.0 - wetVdotH;
    float wetOneMinusVdotH2 = wetOneMinusVdotH * wetOneMinusVdotH;
    float wetFresnel =
        0.02 +
        0.98 * wetOneMinusVdotH2 * wetOneMinusVdotH2 *
            wetOneMinusVdotH;
    float wetnessF = wetFilmStrength * wetFresnel;

    float wetA = wetFilmRoughness * wetFilmRoughness;
    float wetA2 = wetA * wetA;
    float wetDdenom = wetNdotH * wetNdotH * (wetA2 - 1.0) + 1.0;
    float wetD = wetA2 / (3.141593 * wetDdenom * wetDdenom);
    float wetVisV =
        wetNdotL * (wetNdotV * (1.0 + wetA) + wetA);
    float wetVisL =
        wetNdotV * (wetNdotL * (1.0 + wetA) + wetA);
    float wetG = 0.5 / max(wetVisV + wetVisL, 1.0e-6);
    float wetSpecMag =
        min(wetD * wetG * wetFresnel * wetNdotL, 15.0) *
        wetFilmStrength;
    float3 wetFilmSpecular =
        wetNdotL * wetSpecMag * SunColor_HDR.xyz *
        FO4_DIRECTIONAL_SPECULAR_SCALE;
#ifdef AMBIENT_IBL_IN_LIGHT
    float wetAmbientNdotVRaw = dot(wetFilmNormal, viewDirNeg);
    float wetAmbientOneMinusNdotV =
        1.0 - saturate(wetAmbientNdotVRaw);
    float wetAmbientOneMinusNdotV2 =
        wetAmbientOneMinusNdotV * wetAmbientOneMinusNdotV;
    float wetAmbientFresnel =
        0.02 +
        0.98 * wetAmbientOneMinusNdotV2 * wetAmbientOneMinusNdotV2 *
            wetAmbientOneMinusNdotV;
    float ambientWetnessF = wetFilmStrength * wetAmbientFresnel;
    float3 wetAmbientReflection =
        2.0 * wetAmbientNdotVRaw * wetFilmNormal - viewDirNeg;
    float3 wetFilmAmbient =
        EvaluateAmbientGradient(wetAmbientReflection);
#endif
#endif

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

    float specMix = (1.0 - schlickFres * 0.5);
    output.specular.xyz = shadowPcf * specMix * brdfSpecular;
#ifdef WETNESS_EFFECTS
    output.specular.xyz *= 1.0 - wetnessF;
    output.specular.xyz += shadowPcf * wetFilmSpecular;
#ifdef AMBIENT_IBL_IN_LIGHT
    ambientSpecular =
        ambientSpecular * (1.0 - ambientWetnessF) +
        wetFilmAmbient * ambientWetnessF;
#endif
#endif
#ifdef AMBIENT_IBL_IN_LIGHT
    output.specular.xyz += ambientSpecular;
#endif
    output.specular.w   = 1.0;

    output.diffuse.xyz = shadowPcf * finalDiffuse;
#ifdef WETNESS_EFFECTS
    output.diffuse.xyz *= 1.0 - wetnessF;
#ifdef AMBIENT_IBL_IN_LIGHT
    ambientDiffuse *= 1.0 - ambientWetnessF;
#endif
#endif
#ifdef AMBIENT_IBL_IN_LIGHT
    output.diffuse.xyz += ambientDiffuse;
#endif
    output.diffuse.xyz /= 3.0;
    output.diffuse.w   = 0.0;

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
// FO4RE_INLINE_INCLUDE_BEGIN shadow_poisson_kernel.hlsli

#ifndef SHADOW_POISSON_KERNEL_HLSLI_INCLUDED
#define SHADOW_POISSON_KERNEL_HLSLI_INCLUDED

#define SHADOW_POISSON_KERNEL_SIZE 1000

static const float2 SHADOW_POISSON_KERNEL[SHADOW_POISSON_KERNEL_SIZE] =
{
    float2(0.4933930039405823, 0.3942689895629883), float2(0.7985470294952393, 0.8859220147132874),
    float2(0.2473219931125641, 0.9264500141143799), float2(0.051454201340675354, 0.14078199863433838),
    float2(0.8318430185317993, 0.009552289731800556), float2(0.428631991147995, 0.017151400446891785),
    float2(0.01565600000321865, 0.7497789859771729), float2(0.7583850026130676, 0.4961700141429901),
    float2(0.2234870046377182, 0.5621510148048401), float2(0.011627599596977234, 0.4069949984550476),
    float2(0.24146200716495514, 0.30463600158691406), float2(0.430310994386673, 0.7272260189056396),
    float2(0.981810986995697, 0.27835899591445923), float2(0.4070560038089752, 0.5005339980125427),
    float2(0.123478002846241, 0.4635460078716278), float2(0.8095340132713318, 0.6822720170021057),
    float2(0.6758019924163818, 0.6539199948310852), float2(0.23801399767398834, 0.06933809816837311),
    float2(0.0006714069750159979, 0.6111029982566833), float2(0.6218760013580322, 0.4990389943122864),
    float2(0.7128819823265076, 0.11529900133609772), float2(0.9136630296707153, 0.8193910121917725),
    float2(0.29545000195503235, 0.8096870183944702), float2(0.9850149750709534, 0.1178010031580925),
    float2(0.6307569742202759, 0.31321099400520325), float2(0.362621009349823, 0.1857050061225891),
    float2(0.16446399688720703, 0.7875909805297852), float2(0.0038453300949186087, 0.9388409852981567),
    float2(0.5227519869804382, 0.14627499878406525), float2(0.9875180125236511, 0.9389939904212952),
    float2(0.7701039910316467, 0.3155309855937958), float2(0.04483170062303543, 0.26883798837661743),
    float2(0.972320020198822, 0.43855100870132446), float2(0.6903589963912964, 0.9747310280799866),
    float2(0.5827199816703796, 0.8335520029067993), float2(0.49678000807762146, 0.9989929795265198),
    float2(0.498214989900589, 0.6034730076789856), float2(0.9164400100708008, 0.5907769799232483),
    float2(0.851131021976471, 0.21952000260353088), float2(0.4134649932384491, 0.893123984336853),
    float2(0.004425180144608021, 0.015686500817537308), float2(0.5808889865875244, 0.027405599132180214),
    float2(0.09085360169410706, 0.36497101187705994), float2(0.9074980020523071, 0.3878290057182312),
    float2(0.1073639988899231, 0.7465130090713501), float2(0.9870910048484802, 0.18369099497795105),
    float2(0.30414700508117676, 0.5427410006523132), float2(0.7691270112991333, 0.022675300016999245),
    float2(0.8954439759254456, 0.05838190019130707), float2(0.70967698097229, 0.20316199958324432),
    float2(0.4203920066356659, 0.4147160053253174), float2(0.8330940008163452, 0.15762799978256226),
    float2(0.29096299409866333, 0.19553199410438538), float2(0.4844200015068054, 0.9089329838752747),
    float2(0.7604910135269165, 0.9561449885368347), float2(0.03717150166630745, 0.5517749786376953),
    float2(0.14200299978256226, 0.19510500133037567), float2(0.9505599737167358, 0.7496259808540344),
    float2(0.36411601305007935, 0.7906429767608643), float2(0.22901099920272827, 0.8579360246658325),
    float2(0.7427290081977844, 0.7322310209274292), float2(0.7128509879112244, 0.3787960112094879),
    float2(0.34632399678230286, 0.4421829879283905), float2(0.4817650020122528, 0.22287699580192566),
    float2(0.6732990145683289, 0.5668200254440308), float2(0.0006408889894373715, 0.3230080008506775),
    float2(0.8751789927482605, 0.32813501358032227), float2(0.390514999628067, 0.3244419991970062),
    float2(0.9904170036315918, 0.6501359939575195), float2(0.35621199011802673, 0.9518420100212097),
    float2(0.43202000856399536, 0.796563982963562), float2(0.6701859831809998, 0.44901901483535767),
    float2(0.6072880029678345, 0.7214270234107971), float2(0.13770000636577606, 0.5319679975509644),
    float2(0.7076939940452576, 0.8413950204849243), float2(0.8066959977149963, 0.8207039833068848),
    float2(0.6437270045280457, 0.10141299664974213), float2(0.2517470121383667, 0.00012207399413455278),
    float2(0.5584890246391296, 0.4125489890575409), float2(0.5076450109481812, 0.00634784996509552),
    float2(0.8238170146942139, 0.40803200006484985), float2(0.3017059862613678, 0.35984399914741516),
    float2(0.30072900652885437, 0.6263920068740845), float2(0.9513229727745056, 0.5352029800415039),
    float2(0.11658100038766861, 0.8782010078430176), float2(0.3677479922771454, 0.0460829995572567),
    float2(0.2561720013618469, 0.7403180003166199), float2(0.16229699552059174, 0.9830009937286377),
    float2(0.5321210026741028, 0.4979709982872009), float2(0.20673200488090515, 0.23987500369548798),
    float2(0.10248100012540817, 0.6265760064125061), float2(0.34681200981140137, 0.6882839798927307),
    float2(0.903531014919281, 0.6722310185432434), float2(0.17407800257205963, 0.12005999684333801),
    float2(0.31723999977111816, 0.285317987203598), float2(0.09793390333652496, 0.9796140193939209),
    float2(0.19544099271297455, 0.38596799969673157), float2(0.11398699879646301, 0.300942987203598),
    float2(0.8307440280914307, 0.5856199860572815), float2(0.5628529787063599, 0.6620380282402039),
    float2(0.3735159933567047, 0.11416999995708466), float2(0.887935996055603, 0.9788510203361511),
    float2(0.9789119958877563, 0.8495740294456482), float2(0.5026400089263916, 0.06869719922542572),
    float2(0.1686760038137436, 0.05017239972949028), float2(0.8659319877624512, 0.5303509831428528),
    float2(0.9236429929733276, 0.1647389978170395), float2(0.04965360090136528, 0.2052370011806488),
    float2(0.8231760263442993, 0.0799890011548996), float2(0.024811500683426857, 0.6864219903945923),
    float2(0.8720660209655762, 0.871882975101471), float2(0.6138799786567688, 0.991362988948822),
    float2(0.09607230126857758, 0.0949430987238884), float2(0.8258919715881348, 0.2834559977054596),
    float2(0.18890999257564545, 0.4492020010948181), float2(0.6251720190048218, 0.9024930000305176),
    float2(0.5876340270042419, 0.5638599991798401), float2(0.012054800055921078, 0.4879299998283386),
    float2(0.3265480101108551, 0.8919640183448792), float2(0.9322789907455444, 0.8912010192871094),
    float2(0.49372801184654236, 0.6959750056266785), float2(0.6568499803543091, 0.780144989490509),
    float2(0.47056499123573303, 0.46147000789642334), float2(0.3726620078086853, 0.25141099095344543),
    float2(0.8736839890480042, 0.4529249966144562), float2(0.1747490018606186, 0.6543470025062561),
    float2(0.6952120065689087, 0.3021329939365387), float2(0.09561450034379959, 0.8131960034370422),
    float2(0.034150201827287674, 0.07660149782896042), float2(0.06680499762296677, 0.9160130023956299),
    float2(0.23401600122451782, 0.6280710101127625), float2(0.616595983505249, 0.376446008682251),
    float2(0.5639820098876953, 0.22901099920272827), float2(0.010223699733614922, 0.865444004535675),
    float2(0.41499099135398865, 0.6073489785194397), float2(0.631397008895874, 0.2421029955148697),
    float2(0.07110810279846191, 0.002563549904152751), float2(0.8778039813041687, 0.7384870052337646),
    float2(0.9971920251846313, 0.03616439923644066), float2(0.4361099898815155, 0.11423099786043167),
    float2(0.9591969847679138, 0.33796200156211853), float2(0.7051299810409546, 0.04675440117716789),
    float2(0.18008999526500702, 0.3244119882583618), float2(0.6102179884910583, 0.1621749997138977),
    float2(0.5471659898757935, 0.300942987203598), float2(0.18503400683403015, 0.9186990261077881),
    float2(0.4466080069541931, 0.9603869915008545), float2(0.954069972038269, 0.9948729872703552),
    float2(0.33497101068496704, 0.166935995221138), float2(0.3333840072154999, 0.21280600130558014),
    float2(0.4661700129508972, 0.5096290111541748), float2(0.36365899443626404, 0.34330299496650696),
    float2(0.17191100120544434, 0.17108699679374695), float2(0.759880006313324, 0.4608910083770752),
    float2(0.2912079989910126, 0.9834280014038086), float2(0.7586290240287781, 0.10745599865913391),
    float2(0.04507580026984215, 0.596759021282196), float2(0.9029819965362549, 0.5074009895324707),
    float2(0.5965149998664856, 0.7915890216827393), float2(0.17499299347400665, 0.24253100156784058),
    float2(0.4685809910297394, 0.5535449981689453), float2(0.8664510250091553, 0.6336860060691833),
    float2(0.6723840236663818, 0.06936860084533691), float2(0.23993700742721558, 0.18555299937725067),
    float2(0.6918849945068359, 0.7355570197105408), float2(0.6443979740142822, 0.7349770069122314),
    float2(0.41938498616218567, 0.558152973651886), float2(0.4960170090198517, 0.43458399176597595),
    float2(0.6652119755744934, 0.913815975189209), float2(0.2779630124568939, 0.765434980392456),
    float2(0.08548229932785034, 0.17151400446891785), float2(0.30582600831985474, 0.39497101306915283),
    float2(0.7080289721488953, 0.5744190216064453), float2(0.7287819981575012, 0.16055800020694733),
    float2(0.1861020028591156, 0.28904101252555847), float2(0.6779379844665527, 0.14361999928951263),
    float2(0.14032399654388428, 0.7077850103378296), float2(0.05908380076289177, 0.760308027267456),
    float2(0.6109809875488281, 0.4517959952354431), float2(0.03625600039958954, 0.007629630155861378),
    float2(0.9473249912261963, 0.4029659926891327), float2(0.19592900574207306, 0.6829130053520203),
    float2(0.057710498571395874, 0.46809300780296326), float2(0.5917540192604065, 0.2777490019798279),
    float2(0.7339699864387512, 0.6210520267486572), float2(0.9282199740409851, 0.7740709781646729),
    float2(0.8901939988136292, 0.18771900236606598), float2(0.740805983543396, 0.0805383026599884),
    float2(0.7708979845046997, 0.5667589902877808), float2(0.480087012052536, 0.13376300036907196),
    float2(0.33899998664855957, 0.016968300566077232), float2(0.8547930121421814, 0.8099920153617859),
    float2(0.5144199728965759, 0.2596510052680969), float2(0.1364479959011078, 0.6233410239219666),
    float2(0.3690600097179413, 0.5036159753799438), float2(0.33542901277542114, 0.6557819843292236),
    float2(0.5450000166893005, 0.6102790236473083), float2(0.9918820261955261, 0.7283239960670471),
    float2(0.27637600898742676, 0.8976100087165833), float2(0.6288340091705322, 0.9483630061149597),
    float2(0.5381019711494446, 0.7844169735908508), float2(0.06775110214948654, 0.057588398456573486),
    float2(0.09720149636268616, 0.035279400646686554), float2(0.4394359886646271, 0.6425979733467102),
    float2(0.2509230077266693, 0.3704639971256256), float2(0.45289498567581177, 0.17441299557685852),
    float2(0.20642699301242828, 0.7395550012588501), float2(0.4592120051383972, 0.3491320013999939),
    float2(0.8646810054779053, 0.020020099356770515), float2(0.6320689916610718, 0.18671199679374695),
    float2(0.792352020740509, 0.21115800738334656), float2(0.09134189784526825, 0.40156298875808716),
    float2(0.3082979917526245, 0.2425609976053238), float2(0.5730460286140442, 0.3318580090999603),
    float2(0.5030670166015625, 0.6413159966468811), float2(0.665058970451355, 0.8760030269622803),
    float2(0.2581869959831238, 0.5482649803161621), float2(0.873134970664978, 0.6994839906692505),
    float2(0.3422960042953491, 0.11078199744224548), float2(0.2691729962825775, 0.21857400238513947),
    float2(0.07446520030498505, 0.5483570098876953), float2(0.35157299041748047, 0.40360701084136963),
    float2(0.6602979898452759, 0.3413800001144409), float2(0.86285001039505, 0.09915459901094437),
    float2(0.20813600718975067, 0.952817976474762), float2(0.19370099902153015, 0.029023099690675735),
    float2(0.40882599353790283, 0.1530199944972992), float2(0.7104399800300598, 0.7962579727172852),
    float2(0.7826780080795288, 0.7365339994430542), float2(0.8307139873504639, 0.7723320126533508),
    float2(0.4948880076408386, 0.4970549941062927), float2(0.7148659825325012, 0.6980500221252441),
    float2(0.9317910075187683, 0.1991640031337738), float2(0.6378369927406311, 0.8089849948883057),
    float2(0.6657000184059143, 0.5995969772338867), float2(0.7531049847602844, 0.6726580262184143),
    float2(0.5937070250511169, 0.41236600279808044), float2(0.22888900339603424, 0.48976099491119385),
    float2(0.559099018573761, 0.10855399817228317), float2(0.6749169826507568, 0.3947260081768036),
    float2(0.7104399800300598, 0.46766600012779236), float2(0.10541100054979324, 0.22852300107479095),
    float2(0.5172579884529114, 0.10803599655628204), float2(0.9805899858474731, 0.546953022480011),
    float2(0.394665002822876, 0.8042240142822266), float2(0.8669999837875366, 0.16284699738025665),
    float2(0.8225650191307068, 0.6406139731407166), float2(0.8028500080108643, 0.25720998644828796),
    float2(0.44144999980926514, 0.2212589979171753), float2(0.959318995475769, 0.7075719833374023),
    float2(0.6282539963722229, 0.6852020025253296), float2(0.1322370022535324, 0.24506400525569916),
    float2(0.041200000792741776, 0.8709679841995239), float2(0.8260440230369568, 0.4974820017814636),
    float2(0.24625399708747864, 0.6735129952430725), float2(0.025544000789523125, 0.4378489851951599),
    float2(0.23813599348068237, 0.22241899371147156), float2(0.16843199729919434, 0.887050986289978),
    float2(0.2742390036582947, 0.33637499809265137), float2(0.35056599974632263, 0.8275700211524963),
    float2(0.20206299424171448, 0.5102999806404114), float2(0.3134250044822693, 0.9222080111503601),
    float2(0.05026400089263916, 0.7097989916801453), float2(0.36939600110054016, 0.5843070149421692),
    float2(0.07763910293579102, 0.4414199888706207), float2(0.4021419882774353, 0.7092499732971191),
    float2(0.2239139974117279, 0.7875909805297852), float2(0.9979860186576843, 0.9055449962615967),
    float2(0.4920800030231476, 0.7682120203971863), float2(0.4643999934196472, 0.04620499908924103),
    float2(0.5120700001716614, 0.9446390271186829), float2(0.6356390118598938, 0.6482740044593811),
    float2(0.8786889910697937, 0.25064900517463684), float2(0.27533799409866333, 0.1322370022535324),
    float2(0.8996549844741821, 0.7868279814720154), float2(0.30582600831985474, 0.7288129925727844),
    float2(0.9813230037689209, 0.8091070055961609), float2(0.32331299781799316, 0.7908869981765747),
    float2(0.5439310073852539, 0.3821530044078827), float2(0.2187259942293167, 0.6564840078353882),
    float2(0.5931580066680908, 0.6300240159034729), float2(0.14828899502754211, 0.2917569875717163),
    float2(0.9242839813232422, 0.038453299552202225), float2(0.5154579877853394, 0.3307900130748749),
    float2(0.16144299507141113, 0.0877406969666481), float2(0.9222999811172485, 0.5511339902877808),
    float2(0.07669299840927124, 0.32676199078559875), float2(0.1838739961385727, 0.5891600251197815),
    float2(0.4387040138244629, 0.387706995010376), float2(0.8076720237731934, 0.607990026473999),
    float2(0.9334390163421631, 0.9492779970169067), float2(0.7456279993057251, 0.28369998931884766),
    float2(0.8460950255393982, 0.9068880081176758), float2(0.8607749938964844, 0.06769009679555893),
    float2(0.5854060053825378, 0.1382180005311966), float2(0.8695639967918396, 0.9325540065765381),
    float2(0.7618640065193176, 0.4056209921836853), float2(0.3135170042514801, 0.6791890263557434),
    float2(0.5335549712181091, 0.05142369866371155), float2(0.20685400068759918, 0.11807599663734436),
    float2(0.38502201437950134, 0.7539600133895874), float2(0.4065069854259491, 0.9872130155563354),
    float2(0.7939079999923706, 0.09723199903964996), float2(0.5392929911613464, 0.993133008480072),
    float2(0.35935500264167786, 0.7271339893341064), float2(0.9175080060958862, 0.33622199296951294),
    float2(0.0383617989718914, 0.9344459772109985), float2(0.8032469749450684, 0.9429299831390381),
    float2(0.09891050308942795, 0.5130770206451416), float2(0.6351510286331177, 0.026551099494099617),
    float2(0.02212589979171753, 0.1808219999074936), float2(0.4687950015068054, 0.09451580047607422),
    float2(0.614031970500946, 0.5858640074729919), float2(0.47022899985313416, 0.31464600563049316),
    float2(0.707053005695343, 0.430525004863739), float2(0.03366189822554588, 0.7913450002670288),
    float2(0.44935500621795654, 0.8981900215148926), float2(0.8767359852790833, 0.2822049856185913),
    float2(0.17279599606990814, 0.5384079813957214), float2(0.25800299644470215, 0.4765770137310028),
    float2(0.5892509818077087, 0.8811910152435303), float2(0.34333300590515137, 0.5415509939193726),
    float2(0.25071001052856445, 0.4148379862308502), float2(0.27228599786758423, 0.8397780060768127),
    float2(0.5558339953422546, 0.9547110199928284), float2(0.013550199568271637, 0.6540729999542236),
    float2(0.3352150022983551, 0.6053959727287292), float2(0.10904300212860107, 0.5640429854393005),
    float2(0.12421000003814697, 0.16727200150489807), float2(0.3903929889202118, 0.9488199949264526),
    float2(0.8118230104446411, 0.03530989959836006), float2(0.665913999080658, 0.1921750009059906),
    float2(0.7866150140762329, 0.5288860201835632), float2(0.8380079865455627, 0.9941099882125854),
    float2(0.9034090042114258, 0.4408090114593506), float2(0.8189949989318848, 0.7336339950561523),
    float2(0.5269020199775696, 0.8884850144386292), float2(0.9153419733047485, 0.6300240159034729),
    float2(0.8071539998054504, 0.4390699863433838), float2(0.9585559964179993, 0.1530809998512268),
    float2(0.05508590117096901, 0.8280280232429504), float2(0.5516219735145569, 0.004120000172406435),
    float2(0.6189460158348083, 0.8395339846611023), float2(0.4622940123081207, 0.7839900255203247),
    float2(0.8262280225753784, 0.12442400306463242), float2(0.927702009677887, 0.09329509735107422),
    float2(0.5537580251693726, 0.26767799258232117), float2(0.03744620084762573, 0.352183997631073),
    float2(0.38016900420188904, 0.544327974319458), float2(0.8460339903831482, 0.6719570159912109),
    float2(0.7600020170211792, 0.2283090054988861), float2(0.4577470123767853, 0.9305700063705444),
    float2(0.839838981628418, 0.436598002910614), float2(0.2591629922389984, 0.8022710084915161),
    float2(0.9543439745903015, 0.9241920113563538), float2(0.7391279935836792, 0.03790400177240372),
    float2(0.195592999458313, 0.8325750231742859), float2(0.33365899324417114, 0.8562269806861877),
    float2(0.5727710127830505, 0.4569540023803711), float2(0.2591019868850708, 0.036316998302936554),
    float2(0.9794920086860657, 0.6032590270042419), float2(0.26157400012016296, 0.9602950215339661),
    float2(0.6637780070304871, 0.5244609713554382), float2(0.70100998878479, 0.882777988910675),
    float2(0.1817069947719574, 0.7600330114364624), float2(0.1148110032081604, 0.9175999760627747),
    float2(0.9108859896659851, 0.25141099095344543), float2(0.43717798590660095, 0.7649160027503967),
    float2(0.14874699711799622, 0.5772579908370972), float2(0.04138309881091118, 0.30802300572395325),
    float2(0.06421089917421341, 0.659017026424408), float2(0.06592000275850296, 0.9849849939346313),
    float2(0.9661549925804138, 0.022553199902176857), float2(0.2212589979171753, 0.4282050132751465),
    float2(0.9422590136528015, 0.29847100377082825), float2(0.5663629770278931, 0.526108980178833),
    float2(0.41761499643325806, 0.05960259959101677), float2(0.029572399333119392, 0.9823600053787231),
    float2(0.766319990158081, 0.9114660024642944), float2(0.00012207399413455278, 0.9023410081863403),
    float2(0.6978060007095337, 0.5262309908866882), float2(0.5988039970397949, 0.5335860252380371),
    float2(0.27808499336242676, 0.5724660158157349), float2(0.5938599705696106, 0.6913049817085266),
    float2(0.39493998885154724, 0.18863500654697418), float2(0.22229699790477753, 0.7057099938392639),
    float2(0.7985780239105225, 0.9938960075378418), float2(0.48066699504852295, 0.8187509775161743),
    float2(0.12372200191020966, 0.3670769929885864), float2(0.7100440263748169, 0.6486709713935852),
    float2(0.9430519938468933, 0.26413801312446594), float2(0.7608259916305542, 0.6033509969711304),
    float2(0.6530050039291382, 0.27854201197624207), float2(0.5258030295372009, 0.4099859893321991),
    float2(0.1334269940853119, 0.677174985408783), float2(0.7902460098266602, 0.2891629934310913),
    float2(0.256630003452301, 0.7044280171394348), float2(0.5891289710998535, 0.07733389735221863),
    float2(0.9811090230941772, 0.23142200708389282), float2(0.20831899344921112, 0.8974270224571228),
    float2(0.5684379935264587, 0.7473070025444031), float2(0.13040600717067719, 0.04840239882469177),
    float2(0.5238500237464905, 0.8460649847984314), float2(0.549485981464386, 0.6958829760551453),
    float2(0.3769649863243103, 0.42201000452041626), float2(0.4108709990978241, 0.6622520089149475),
    float2(0.2935880124568939, 0.055848900228738785), float2(0.4882049858570099, 0.965453028678894),
    float2(0.7264630198478699, 0.9537339806556702), float2(0.9837639927864075, 0.0706809014081955),
    float2(0.7990660071372986, 0.14807599782943726), float2(0.6926789879798889, 0.24274399876594543),
    float2(0.8107550144195557, 0.37058600783348083), float2(0.7613450288772583, 0.7735530138015747),
    float2(0.5843989849090576, 0.19711899757385254), float2(0.3822439908981323, 0.649586021900177),
    float2(0.5361189842224121, 0.5626090168952942), float2(0.1069369986653328, 0.7808769941329956),
    float2(0.04721210151910782, 0.5020599961280823), float2(0.7670519948005676, 0.25974300503730774),
    float2(0.21903100609779358, 0.269569993019104), float2(0.9999079704284668, 0.7610099911689758),
    float2(0.7401350140571594, 0.0054933298379182816), float2(0.9114959836006165, 0.0007019260083325207),
    float2(0.0054933298379182816, 0.11359000205993652), float2(0.746940016746521, 0.5364239811897278),
    float2(0.8309879899024963, 0.858119010925293), float2(0.9400010108947754, 0.46684199571609497),
    float2(0.46415600180625916, 0.7427589893341064), float2(0.3052160143852234, 0.11606200039386749),
    float2(0.2652060091495514, 0.6256899833679199), float2(0.3504140079021454, 0.7576829791069031),
    float2(0.5263530015945435, 0.22064900398254395), float2(0.43202000856399536, 0.835536003112793),
    float2(0.45939499139785767, 0.6797080039978027), float2(0.7512440085411072, 0.8253120183944702),
    float2(0.9889219999313354, 0.36979299783706665), float2(0.554764986038208, 0.9222080111503601),
    float2(0.13818800449371338, 0.33500200510025024), float2(0.06369210034608841, 0.6258739829063416),
    float2(0.7296370267868042, 0.9963380098342896), float2(0.8769490122795105, 0.5734729766845703),
    float2(0.30063799023628235, 0.15240900218486786), float2(0.41410601139068604, 0.27835899591445923),
    float2(0.4047060012817383, 0.4435249865055084), float2(0.5926390290260315, 0.9656059741973877),
    float2(0.16592900454998016, 0.39811399579048157), float2(0.40757501125335693, 0.3829460144042969),
    float2(0.14102600514888763, 0.9392679929733276), float2(0.31070899963378906, 0.44724899530410767),
    float2(0.9505910277366638, 0.8207039833068848), float2(0.185247004032135, 0.6207159757614136),
    float2(0.4216740131378174, 0.4699850082397461), float2(0.995727002620697, 0.4603720009326935),
    float2(0.6359750032424927, 0.06024349853396416), float2(0.6675620079040527, 0.7038180232048035),
    float2(0.9926760196685791, 0.50968998670578), float2(0.2269970029592514, 0.8265330195426941),
    float2(0.08923610299825668, 0.27405598759651184), float2(0.12225700169801712, 0.4274730086326599),
    float2(0.008758810348808765, 0.21646800637245178), float2(0.9691460132598877, 0.9648119807243347),
    float2(0.5273900032043457, 0.7440410256385803), float2(0.376446008682251, 0.46232500672340393),
    float2(0.2663959860801697, 0.27372100949287415), float2(0.7247229814529419, 0.5046539902687073),
    float2(0.33017998933792114, 0.0569474995136261), float2(0.10275600105524063, 0.7081519961357117),
    float2(0.2038940042257309, 0.07797479629516602), float2(0.27274399995803833, 0.09881889820098877),
    float2(0.8383129835128784, 0.5476850271224976), float2(0.13641799986362457, 0.0021057799458503723),
    float2(0.1447490006685257, 0.8574180006980896), float2(0.36631399393081665, 0.30069899559020996),
    float2(0.8690450191497803, 0.36930400133132935), float2(0.33710700273513794, 0.36228498816490173),
    float2(0.6480000019073486, 0.992247998714447), float2(0.15732300281524658, 0.4629659950733185),
    float2(0.2579120099544525, 0.5161899924278259), float2(0.756401002407074, 0.36332300305366516),
    float2(0.07925660163164139, 0.863277018070221), float2(0.49708500504493713, 0.872829020023346),
    float2(0.7334210276603699, 0.2482379972934723), float2(0.8385570049285889, 0.33674100041389465),
    float2(0.701865017414093, 0.6125680208206177), float2(0.8915070295333862, 0.1351660043001175),
    float2(0.9291669726371765, 0.8591570258140564), float2(0.16385400295257568, 0.8232979774475098),
    float2(0.29578500986099243, 0.8687090277671814), float2(0.14365099370479584, 0.1382489949464798),
    float2(0.6873679757118225, 0.00015259299834724516), float2(0.19272400438785553, 0.19836999475955963),
    float2(0.4761190116405487, 0.005981630180031061), float2(0.0029603000730276108, 0.05365150049328804),
    float2(0.37595799565315247, 0.8765529990196228), float2(0.48942500352859497, 0.17853300273418427),
    float2(0.44901901483535767, 0.2751240134239197), float2(0.05920590087771416, 0.10687600076198578),
    float2(0.21671199798583984, 0.33851099014282227), float2(0.4308910071849823, 0.3313089907169342),
    float2(0.3969849944114685, 0.018188999965786934), float2(0.7869200110435486, 0.34943699836730957),
    float2(0.4629659950733185, 0.5937380194664001), float2(0.32071900367736816, 0.48463401198387146),
    float2(0.5358740091323853, 0.18234799802303314), float2(0.5571150183677673, 0.07528919726610184),
    float2(0.3522450029850006, 0.9188820123672485), float2(0.5332189798355103, 0.5303199887275696),
    float2(0.004028439987450838, 0.5356910228729248), float2(0.10589899867773056, 0.14139199256896973),
    float2(0.3878290057182312, 0.83992999792099), float2(0.9102150201797485, 0.7370830178260803),
    float2(0.9266639947891235, 0.9795830249786377), float2(0.6080809831619263, 0.8122199773788452),
    float2(0.9649649858474731, 0.27631500363349915), float2(0.14288799464702606, 0.44889700412750244),
    float2(0.7782829999923706, 0.9297770261764526), float2(0.8665729761123657, 0.9796749949455261),
    float2(0.6211429834365845, 0.03671380132436752), float2(0.14835000038146973, 0.32108500599861145),
    float2(0.39762601256370544, 0.52531498670578), float2(0.09006009995937347, 0.532243013381958),
    float2(0.5182650089263916, 0.5578479766845703), float2(0.9172340035438538, 0.6475110054016113),
    float2(0.4321730136871338, 0.5689259767532349), float2(0.20355799794197083, 0.2852570116519928),
    float2(0.7240210175514221, 0.09759820252656937), float2(0.7398300170898438, 0.652974009513855),
    float2(0.026337500661611557, 0.46317899227142334), float2(0.3448899984359741, 0.5820180177688599),
    float2(0.6165649890899658, 0.2599869966506958), float2(0.8989840149879456, 0.29755499958992004),
    float2(0.6288949847221375, 0.8738669753074646), float2(0.2642289996147156, 0.7735530138015747),
    float2(0.27021101117134094, 0.05664239823818207), float2(0.25159499049186707, 0.26065900921821594),
    float2(0.4109010100364685, 0.3521530032157898), float2(0.2665790021419525, 0.8811910152435303),
    float2(0.8006229996681213, 0.5765249729156494), float2(0.04422130063176155, 0.9715870022773743),
    float2(0.7536550164222717, 0.7961670160293579), float2(0.9815670251846313, 0.6637780070304871),
    float2(0.6142150163650513, 0.19577600061893463), float2(0.9745169878005981, 0.46595698595046997),
    float2(0.7259439826011658, 0.7727289795875549), float2(0.10940899699926376, 0.18436199426651),
    float2(0.3935059905052185, 0.9672539830207825), float2(0.6308789849281311, 0.1587270051240921),
    float2(0.07803580164909363, 0.71144700050354), float2(0.2816550135612488, 0.35581499338150024),
    float2(0.6762599945068359, 0.12686499953269958), float2(0.5257419943809509, 0.029328299686312675),
    float2(0.6664940118789673, 0.3791919946670532), float2(0.09625539928674698, 0.3855710029602051),
    float2(0.9055150151252747, 0.9863280057907104), float2(0.794031023979187, 0.7696769833564758),
    float2(0.5939819812774658, 0.6646019816398621), float2(0.24237799644470215, 0.7307350039482117),
    float2(0.34241798520088196, 0.7878350019454956), float2(0.27280500531196594, 0.9724720120429993),
    float2(0.7841730117797852, 0.975737988948822), float2(0.08322399854660034, 0.7509689927101135),
    float2(0.1539350003004074, 0.9025539755821228), float2(0.5869929790496826, 0.2986850142478943),
    float2(0.6508380174636841, 0.46540701389312744), float2(0.9307839870452881, 0.41453298926353455),
    float2(0.04864649847149849, 0.1587270051240921), float2(0.2348400056362152, 0.9785450100898743),
    float2(0.9290140271186829, 0.01223789993673563), float2(0.8074589967727661, 0.3382669985294342),
    float2(0.279092013835907, 0.4622940123081207), float2(0.8978850245475769, 0.3205359876155853),
    float2(0.16229699552059174, 0.5076749920845032), float2(0.024140100926160812, 0.8540909886360168),
    float2(0.32322201132774353, 0.6404920220375061), float2(0.774528980255127, 0.44495999813079834),
    float2(0.583361029624939, 0.5878170132637024), float2(0.23020100593566895, 0.5282449722290039),
    float2(0.7177950143814087, 0.5284280180931091), float2(0.895779013633728, 0.9174169898033142),
    float2(0.05331579968333244, 0.08786279708147049), float2(0.6327099800109863, 0.2616350054740906),
    float2(0.7405930161476135, 0.10132099688053131), float2(0.7268900275230408, 0.81563800573349),
    float2(0.4935759902000427, 0.7398599982261658), float2(0.19110700488090515, 0.25623300671577454),
    float2(0.7097690105438232, 0.33460500836372375), float2(0.30463600158691406, 0.4738300144672394),
    float2(0.1699880063533783, 0.8445690274238586), float2(0.1622059941291809, 0.13837100565433502),
    float2(0.6375619769096375, 0.5273900032043457), float2(0.9686880111694336, 0.5222629904747009),
    float2(0.6437569856643677, 0.2951439917087555), float2(0.8762779831886292, 0.06378369778394699),
    float2(0.8490859866142273, 0.6306959986686707), float2(0.3152559995651245, 0.31974199414253235),
    float2(0.6399120092391968, 0.0016479999758303165), float2(0.7389140129089355, 0.8538159728050232),
    float2(0.834650993347168, 0.6953639984130859), float2(0.21842099726200104, 0.5859249830245972),
    float2(0.20313100516796112, 0.3199560046195984), float2(0.2605060040950775, 0.9350569844245911),
    float2(0.5032200217247009, 0.22409699857234955), float2(0.9806510210037231, 0.6869109869003296),
    float2(0.04492320120334625, 0.29142099618911743), float2(0.6318550109863281, 0.45756399631500244),
    float2(0.6268810033798218, 0.783715009689331), float2(0.4195989966392517, 0.6993619799613953),
    float2(0.0400707982480526, 0.055970899760723114), float2(0.03051850013434887, 0.9512010216712952),
    float2(0.9286779761314392, 0.929502010345459), float2(0.8272039890289307, 0.7477650046348572),
    float2(0.11105699837207794, 0.01834160089492798), float2(0.5337380170822144, 0.9626150131225586),
    float2(0.9068269729614258, 0.6102790236473083), float2(0.5822929739952087, 0.2507399916648865),
    float2(0.05746639892458916, 0.8055970072746277), float2(0.711355984210968, 0.3095490038394928),
    float2(0.3408310115337372, 0.9883419871330261), float2(0.8656880259513855, 0.1908629983663559),
    float2(0.28107500076293945, 0.8549759984016418), float2(0.673695981502533, 0.6842250227928162),
    float2(0.4851219952106476, 0.8502150177955627), float2(0.12683500349521637, 0.11697699874639511),
    float2(0.38239699602127075, 0.7724850177764893), float2(0.14078199863433838, 0.4656510055065155),
    float2(0.3996399939060211, 0.7349159717559814), float2(0.925961971282959, 0.6934720277786255),
    float2(0.003295999951660633, 0.4524979889392853), float2(0.018677299842238426, 0.05279700085520744),
    float2(0.31504300236701965, 0.6172059774398804), float2(0.9683520197868347, 0.2957240045070648),
    float2(0.1404460072517395, 0.21460600197315216), float2(0.8244580030441284, 0.9115880131721497),
    float2(0.6701560020446777, 0.2635580003261566), float2(0.9408860206604004, 0.07382430136203766),
    float2(0.5609909892082214, 0.855434000492096), float2(0.9088109731674194, 0.40531599521636963),
    float2(0.04318369925022125, 0.0306100994348526), float2(0.8865630030632019, 0.5467389822006226),
    float2(0.5602589845657349, 0.3935360014438629), float2(0.7865840196609497, 0.6956999897956848),
    float2(0.9813230037689209, 0.3108919858932495), float2(0.741690993309021, 0.807000994682312),
    float2(0.7927489876747131, 0.9555649757385254), float2(0.03796499967575073, 0.1091340035200119),
    float2(0.5427719950675964, 0.3266089856624603), float2(0.2568129897117615, 0.2299260050058365),
    float2(0.5290690064430237, 0.12536999583244324), float2(0.15723100304603577, 0.21924500167369843),
    float2(0.5113679766654968, 0.6803799867630005), float2(0.8819540143013, 0.08236949890851974),
    float2(0.7788019776344299, 0.3316139876842499), float2(0.8931549787521362, 0.23706799745559692),
    float2(0.9986569881439209, 0.2539139986038208), float2(0.5540030002593994, 0.3552660048007965),
    float2(0.3447679877281189, 0.32279399037361145), float2(0.06250189989805222, 0.5236979722976685),
    float2(0.48634299635887146, 0.7996150255203247), float2(0.09692680090665817, 0.8869900107383728),
    float2(0.10077200084924698, 0.3481859862804413), float2(0.30857300758361816, 0.3744010031223297),
    float2(0.11847300082445145, 0.6935030221939087), float2(0.27414798736572266, 0.17810599505901337),
    float2(0.10101599991321564, 0.6044800281524658), float2(0.44770699739456177, 0.8116400241851807),
    float2(0.6609389781951904, 0.8439589738845825), float2(0.8148750066757202, 0.7059850096702576),
    float2(0.19531799852848053, 0.06024349853396416), float2(0.17032399773597717, 0.3525800108909607),
    float2(0.14594000577926636, 0.2640460133552551), float2(0.6514790058135986, 0.8976100087165833),
    float2(0.5121009945869446, 0.04525899887084961), float2(0.9422889947891235, 0.6454660296440125),
    float2(0.8371229767799377, 0.566057026386261), float2(0.6933190226554871, 0.7099519968032837),
    float2(0.22318199276924133, 0.2987760007381439), float2(0.776665985584259, 0.991424024105072),
    float2(0.6462900042533875, 0.8787189722061157), float2(0.013794399797916412, 0.06796470284461975),
    float2(0.5755490064620972, 0.8907129764556885), float2(0.6829130053520203, 0.5961490273475647),
    float2(0.6779080033302307, 0.17645800113677979), float2(0.3838619887828827, 0.20413799583911896),
    float2(0.039155200123786926, 0.65385901927948), float2(0.7220979928970337, 0.9296240210533142),
    float2(0.4055599868297577, 0.6443679928779602), float2(0.646992027759552, 0.7561569809913635),
    float2(0.9165930151939392, 0.11093500256538391), float2(0.7798699736595154, 0.5862910151481628),
    float2(0.5788750052452087, 0.7039700150489807), float2(0.1622059941291809, 0.6864219903945923),
    float2(0.5453659892082214, 0.5935239791870117), float2(0.6008179783821106, 0.9816280007362366),
    float2(0.4918060004711151, 0.32804301381111145), float2(0.3344219923019409, 0.036591701209545135),
    float2(0.437483012676239, 0.4643389880657196), float2(0.11142300069332123, 0.2599259912967682),
    float2(0.8161569833755493, 0.5559859871864319), float2(0.5656300187110901, 0.5763729810714722),
    float2(0.4595780074596405, 0.6119269728660583), float2(0.7170630097389221, 0.06869719922542572),
    float2(0.8241519927978516, 0.5289160013198853), float2(0.09192179888486862, 0.2417680025100708),
    float2(0.07708980143070221, 0.3779410123825073), float2(0.8162180185317993, 0.9664300084114075),
    float2(0.07837150245904922, 0.5922729969024658), float2(0.10809700191020966, 0.06491290032863617),
    float2(0.664205014705658, 0.9528489708900452), float2(0.6889550089836121, 0.5806760191917419),
    float2(0.06735440343618393, 0.2725299894809723), float2(0.02386550046503544, 0.7331770062446594),
    float2(0.8414869904518127, 0.9492779970169067), float2(0.9583420157432556, 0.4991300106048584),
    float2(0.2952969968318939, 0.9075899720191956), float2(0.4260689914226532, 0.6551409959793091),
    float2(0.3744930028915405, 0.8037049770355225), float2(0.45243701338768005, 0.7263100147247314),
    float2(0.770684003829956, 0.6922510266304016), float2(0.2409130036830902, 0.5623040199279785),
    float2(0.8374890089035034, 0.1417890042066574), float2(0.9472950100898743, 0.6683549880981445),
    float2(0.6531879901885986, 0.5027620196342468), float2(0.7993410229682922, 0.4907679855823517),
    float2(0.044373899698257446, 0.9018830060958862), float2(0.23279500007629395, 0.08410900086164474),
    float2(0.039277300238609314, 0.24481900036334991), float2(0.6735739707946777, 0.050630200654268265),
    float2(0.42893800139427185, 0.6259040236473083), float2(0.8502150177955627, 0.02804649993777275),
    float2(0.13934800028800964, 0.9736009836196899), float2(0.70973801612854, 0.1386760026216507),
    float2(0.7105010151863098, 0.24466699361801147), float2(0.806207001209259, 0.4577470123767853),
    float2(0.3526720106601715, 0.48011699318885803), float2(0.019013000652194023, 0.5774409770965576),
    float2(0.30634498596191406, 0.7099519968032837), float2(0.6133000254631042, 0.8608049750328064),
    float2(0.870693027973175, 0.6712549924850464), float2(0.1938840001821518, 0.1082490012049675),
    float2(0.8637959957122803, 0.11880899965763092), float2(0.6128730177879333, 0.6519359946250916),
    float2(0.740776002407074, 0.4344309866428375), float2(0.7747730016708374, 0.8911100029945374),
    float2(0.9734179973602295, 0.8720660209655762), float2(0.29666998982429504, 0.07504499703645706),
    float2(0.6775410175323486, 0.7664719820022583), float2(0.7274090051651001, 0.737326979637146),
    float2(0.231330007314682, 0.1134679988026619), float2(0.8655660152435303, 0.5137490034103394),
    float2(0.02783289924263954, 0.39231500029563904), float2(0.676351010799408, 0.0865200012922287),
    float2(0.08197270333766937, 0.12448500096797943), float2(0.7660449743270874, 0.9399700164794922),
    float2(0.4135870039463043, 0.81392902135849), float2(0.38462498784065247, 0.9191870093345642),
    float2(0.9143959879875183, 0.8536940217018127), float2(0.8333079814910889, 0.8886989951133728),
    float2(0.5034639835357666, 0.5336470007896423), float2(0.9488199949264526, 0.04211549833416939),
    float2(0.8899199962615967, 0.51670902967453), float2(0.029145199805498123, 0.1991640031337738),
    float2(0.6961269974708557, 0.9537950158119202), float2(0.6809290051460266, 0.8413649797439575),
    float2(0.4759669899940491, 0.42362698912620544), float2(0.8825039863586426, 0.4031189978122711),
    float2(0.48719701170921326, 0.6223949790000916), float2(0.29154300689697266, 0.28327301144599915),
    float2(0.41770699620246887, 0.8735920190811157), float2(0.5697810053825378, 0.5599539875984192),
    float2(0.7090370059013367, 0.819940984249115), float2(0.9451889991760254, 0.7791069746017456),
    float2(0.20664100348949432, 0.559831976890564), float2(0.15451499819755554, 0.8005009889602661),
    float2(0.761559009552002, 0.583666980266571), float2(0.8436229825019836, 0.5150610208511353),
    float2(0.9064610004425049, 0.36847999691963196), float2(0.7637559771537781, 0.1731320023536682),
    float2(0.3397020101547241, 0.2404550015926361), float2(0.9735710024833679, 0.1291240006685257),
    float2(0.5914790034294128, 0.32502201199531555), float2(0.7144380211830139, 0.22290700674057007),
    float2(0.28156399726867676, 0.014984600245952606), float2(0.37586599588394165, 0.157383993268013),
    float2(0.8169500231742859, 0.06500440090894699), float2(0.5927299857139587, 0.043763499706983566),
    float2(0.33323198556900024, 0.9622179865837097), float2(0.8095030188560486, 0.11258299648761749),
    float2(0.8926969766616821, 0.8699300289154053), float2(0.27539899945259094, 0.6830959916114807),
    float2(0.6876429915428162, 0.7784050107002258), float2(0.04742579907178879, 0.6855679750442505),
    float2(0.8386790156364441, 0.7967159748077393), float2(0.003295999951660633, 0.960204005241394),
    float2(0.4747759997844696, 0.7012540102005005), float2(0.6989960074424744, 0.40763598680496216),
    float2(0.46705499291419983, 0.9523910284042358), float2(0.009064000099897385, 0.7844780087471008),
    float2(0.7901549935340881, 0.8426160216331482), float2(0.7426679730415344, 0.9260839819908142),
    float2(0.9539480209350586, 0.08786279708147049), float2(0.7841119766235352, 0.3865779936313629),
    float2(0.4679099917411804, 0.6360059976577759), float2(0.2775050103664398, 0.7463300228118896),
    float2(0.9962769746780396, 0.017731299623847008), float2(0.6936249732971191, 0.015411799773573875),
    float2(0.16180899739265442, 0.27713900804519653), float2(0.7480700016021729, 0.5717949867248535),
    float2(0.38032200932502747, 0.30930501222610474), float2(0.26319199800491333, 0.0722372978925705),
    float2(0.12341699749231339, 0.736503005027771), float2(0.29456499218940735, 0.09595020115375519),
    float2(0.40058600902557373, 0.26285600662231445), float2(0.7446519732475281, 0.020508399233222008),
    float2(0.2360610067844391, 0.4338510036468506), float2(0.5066990256309509, 0.3561820089817047),
    float2(0.7633900046348572, 0.7488020062446594), float2(0.18716999888420105, 0.5315709710121155),
    float2(0.5283359885215759, 0.6516619920730591), float2(0.875361979007721, 0.8960540294647217),
    float2(0.3034459948539734, 0.9632560014724731), float2(0.28626400232315063, 0.6138489842414856),
    float2(0.41810399293899536, 0.3053379952907562), float2(0.6449170112609863, 0.386821985244751),
    float2(0.47199898958206177, 0.2539750039577484), float2(0.9628890156745911, 0.5816829800605774),
    float2(0.4387950003147125, 0.18417899310588837), float2(0.6507459878921509, 0.6331980228424072),
    float2(0.8930330276489258, 0.038544900715351105), float2(0.8532059788703918, 0.003906370140612125),
    float2(0.39274299144744873, 0.16058799624443054), float2(0.20053699612617493, 0.4065679907798767),
    float2(0.38416698575019836, 0.6161990165710449), float2(0.676351010799408, 0.10593000054359436),
    float2(0.061189599335193634, 0.7376319766044617), float2(0.5238810181617737, 0.6342049837112427),
    float2(0.07486189901828766, 0.8802449703216553), float2(0.2795189917087555, 0.6369820237159729),
    float2(0.9775689840316772, 0.5641040205955505), float2(0.6579790115356445, 0.7241430282592773),
    float2(0.587602972984314, 0.43284401297569275), float2(0.1535390019416809, 0.42844900488853455),
    float2(0.5446940064430237, 0.4000059962272644), float2(0.7756890058517456, 0.48335200548171997),
    float2(0.22074000537395477, 0.04196299985051155), float2(0.08465830236673355, 0.2912989854812622),
    float2(0.23642699420452118, 0.7124850153923035), float2(0.00299080996774137, 0.349590003490448),
    float2(0.9074069857597351, 0.7080289721488953), float2(0.8657799959182739, 0.7618029713630676),
    float2(0.7648239731788635, 0.8086180090904236), float2(0.3283179998397827, 0.34629398584365845),
    float2(0.3868829905986786, 0.0961942970752716), float2(0.6354870200157166, 0.5113070011138916),
    float2(0.2108519971370697, 0.5334029793739319), float2(0.8951690196990967, 0.9441210031509399),
    float2(0.905239999294281, 0.47059500217437744), float2(0.2443619966506958, 0.9996340274810791),
    float2(0.6532179713249207, 0.06213570013642311), float2(0.18250100314617157, 0.14465799927711487),
    float2(0.9402449727058411, 0.113926000893116), float2(0.8352310061454773, 0.8336129784584045),
    float2(0.7318639755249023, 0.8900110125541687), float2(0.4682759940624237, 0.8510090112686157),
    float2(0.7259439826011658, 0.050416599959135056), float2(0.02999969944357872, 0.5138710141181946),
    float2(0.6426889896392822, 0.8466749787330627), float2(0.115665003657341, 0.6367689967155457),
    float2(0.8600119948387146, 0.31769800186157227), float2(0.651295006275177, 0.15012100338935852),
    float2(0.14667199552059174, 0.48329100012779236), float2(0.002594070043414831, 0.080690898001194),
    float2(0.2742699980735779, 0.9473559856414795), float2(0.8565629720687866, 0.884456992149353),
    float2(0.6153450012207031, 0.5258949995040894), float2(0.6161689758300781, 0.0650349035859108),
    float2(0.04464859887957573, 0.7540209889411926), float2(0.6215699911117554, 0.610614001750946),
    float2(0.9099090099334717, 0.12802499532699585), float2(0.38581499457359314, 0.7100440263748169),
    float2(0.9608139991760254, 0.8434709906578064), float2(0.2586140036582947, 0.17401699721813202),
    float2(0.3295390009880066, 0.0877406969666481), float2(0.3620719909667969, 0.010986699722707272),
    float2(0.5771049857139587, 0.7944579720497131), float2(0.9248939752578735, 0.6160770058631897),
    float2(0.35148200392723083, 0.871487021446228), float2(0.1452070027589798, 0.08713030070066452),
    float2(0.26370999217033386, 0.9180880188941956), float2(0.9304180145263672, 0.21790200471878052),
    float2(0.03839230164885521, 0.8409990072250366), float2(0.3638420104980469, 0.9659109711647034),
    float2(0.7118750214576721, 0.9094820022583008), float2(0.5462200045585632, 0.9036229848861694),
    float2(0.7722709774971008, 0.5484790205955505), float2(0.06863609701395035, 0.23969200253486633),
    float2(0.5107269883155823, 0.6626480221748352), float2(0.8228399753570557, 0.200873002409935),
    float2(0.8471019864082336, 0.7287819981575012), float2(0.8039489984512329, 0.3090910017490387),
    float2(0.16592900454998016, 0.9992070198059082), float2(0.017029300332069397, 0.258338987827301),
    float2(0.14404700696468353, 0.6911529898643494), float2(0.30634498596191406, 0.8825950026512146),
    float2(0.2857449948787689, 0.8237559795379639), float2(0.9004179835319519, 0.21585699915885925),
    float2(0.7908260226249695, 0.6521810293197632), float2(0.542618989944458, 0.09350869804620743),
    float2(0.9871519804000854, 0.40418699383735657), float2(0.586778998374939, 0.546159029006958),
    float2(0.0012207400286570191, 0.14838099479675293), float2(0.3886840045452118, 0.04126099869608879),
    float2(0.30332300066947937, 0.6541950106620789), float2(0.2299869954586029, 0.4130989909172058),
    float2(0.08926659822463989, 0.05551319941878319), float2(0.05078279972076416, 0.44187700748443604),
    float2(0.16595999896526337, 0.9257789850234985), float2(0.3691520094871521, 0.43934398889541626),
    float2(0.9942010045051575, 0.8421279788017273), float2(0.6016420125961304, 0.2378309965133667),
    float2(0.2598649859428406, 0.31934601068496704), float2(0.31193000078201294, 0.09430219978094101),
    float2(0.8503680229187012, 0.5842159986495972), float2(0.4444110095500946, 0.9432049989700317),
    float2(0.8410900235176086, 0.6138190031051636), float2(0.7526469826698303, 0.8688920140266418),
    float2(0.8423110246658325, 0.4768820106983185), float2(0.9987789988517761, 0.5566580295562744),
    float2(0.7288429737091064, 0.2999970018863678), float2(0.41907998919487, 0.51139897108078),
    float2(0.006530960090458393, 0.19009999930858612), float2(0.777184009552002, 0.8656880259513855),
    float2(0.5524160265922546, 0.8083130121231079), float2(0.9157080054283142, 0.2674950063228607),
    float2(0.2386849969625473, 0.9572740197181702), float2(0.17767900228500366, 0.6733300089836121),
    float2(0.4201180040836334, 0.7552419900894165), float2(0.6901149749755859, 0.3751029968261719),
    float2(0.6461989879608154, 0.48582398891448975), float2(0.14795400202274323, 0.8742030262947083),
    float2(0.45377999544143677, 0.5587630271911621), float2(0.8760949969291687, 0.7765129804611206),
    float2(0.09781180322170258, 0.4535660147666931), float2(0.288796991109848, 0.7829520106315613),
    float2(0.5181130170822144, 0.9272440075874329), float2(0.5347149968147278, 0.8046209812164307),
    float2(0.6116520166397095, 0.1325719952583313), float2(0.39588600397109985, 0.3677789866924286),
    float2(0.07458720356225967, 0.7817320227622986), float2(0.09790340065956116, 0.7313460111618042),
    float2(0.47846901416778564, 0.6591389775276184), float2(0.7460860013961792, 0.17899100482463837),
    float2(0.8828089833259583, 0.659932017326355), float2(0.5023350119590759, 0.5522930026054382),
    float2(0.6184269785881042, 0.745140016078949), float2(0.03329569846391678, 0.2152779996395111),
    float2(0.8204290270805359, 0.9392380118370056), float2(0.2187259942293167, 0.9186379909515381),
    float2(0.9653000235557556, 0.9029819965362549), float2(0.41251900792121887, 0.32721900939941406),
    float2(0.5757930278778076, 0.11654999852180481), float2(0.4983980059623718, 0.8133180141448975),
    float2(0.8005620241165161, 0.8046510219573975), float2(0.014648900367319584, 0.7161779999732971),
    float2(0.28116700053215027, 0.2526319921016693), float2(0.46018901467323303, 0.9919739961624146),
    float2(0.8584250211715698, 0.48796001076698303), float2(0.4055910110473633, 0.2269359976053238),
    float2(0.6199529767036438, 0.34458398818969727), float2(0.22595900297164917, 0.7304610013961792),
    float2(0.8765529990196228, 0.38453298807144165), float2(0.7404710054397583, 0.5567190051078796),
    float2(0.33588701486587524, 0.9378029704093933), float2(0.6446120142936707, 0.5851920247077942),
    float2(0.09021270275115967, 0.8339790105819702), float2(0.7720270156860352, 0.8380690217018127),
    float2(0.6318249702453613, 0.5486310124397278), float2(0.5929440259933472, 0.9261149764060974),
    float2(0.6869109869003296, 0.8998990058898926), float2(0.655538022518158, 0.8112429976463318),
    float2(0.5086519718170166, 0.12631599605083466), float2(0.732109010219574, 0.20297899842262268),
    float2(0.5872679948806763, 0.01049839984625578), float2(0.12387499958276749, 0.8373969793319702),
    float2(0.5549790263175964, 0.20761699974536896), float2(0.0631427988409996, 0.17551200091838837),
    float2(0.5942869782447815, 0.7750179767608643), float2(0.20181900262832642, 0.8092589974403381),
    float2(0.23673200607299805, 0.13409799337387085), float2(0.9876400232315063, 0.08883939683437347),
    float2(0.8188419938087463, 0.4270760118961334), float2(0.11001899838447571, 0.47312799096107483),
    float2(0.7257300019264221, 0.8386179804801941), float2(0.7529529929161072, 0.20108599960803986),
    float2(0.5498830080032349, 0.49897798895835876), float2(0.399152010679245, 0.7832580208778381),
    float2(0.44984298944473267, 0.3073819875717163), float2(0.3033849895000458, 0.014557300135493279),
    float2(0.4429759979248047, 0.3716540038585663), float2(0.4494459927082062, 0.8466749787330627),
};

#endif
// FO4RE_INLINE_INCLUDE_END shadow_poisson_kernel.hlsli
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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

#ifdef FILTER_POISSON
// FO4RE_INLINE_INCLUDE_BEGIN shadow_poisson_kernel.hlsli

#ifndef SHADOW_POISSON_KERNEL_HLSLI_INCLUDED
#define SHADOW_POISSON_KERNEL_HLSLI_INCLUDED

#define SHADOW_POISSON_KERNEL_SIZE 1000

static const float2 SHADOW_POISSON_KERNEL[SHADOW_POISSON_KERNEL_SIZE] =
{
    float2(0.4933930039405823, 0.3942689895629883), float2(0.7985470294952393, 0.8859220147132874),
    float2(0.2473219931125641, 0.9264500141143799), float2(0.051454201340675354, 0.14078199863433838),
    float2(0.8318430185317993, 0.009552289731800556), float2(0.428631991147995, 0.017151400446891785),
    float2(0.01565600000321865, 0.7497789859771729), float2(0.7583850026130676, 0.4961700141429901),
    float2(0.2234870046377182, 0.5621510148048401), float2(0.011627599596977234, 0.4069949984550476),
    float2(0.24146200716495514, 0.30463600158691406), float2(0.430310994386673, 0.7272260189056396),
    float2(0.981810986995697, 0.27835899591445923), float2(0.4070560038089752, 0.5005339980125427),
    float2(0.123478002846241, 0.4635460078716278), float2(0.8095340132713318, 0.6822720170021057),
    float2(0.6758019924163818, 0.6539199948310852), float2(0.23801399767398834, 0.06933809816837311),
    float2(0.0006714069750159979, 0.6111029982566833), float2(0.6218760013580322, 0.4990389943122864),
    float2(0.7128819823265076, 0.11529900133609772), float2(0.9136630296707153, 0.8193910121917725),
    float2(0.29545000195503235, 0.8096870183944702), float2(0.9850149750709534, 0.1178010031580925),
    float2(0.6307569742202759, 0.31321099400520325), float2(0.362621009349823, 0.1857050061225891),
    float2(0.16446399688720703, 0.7875909805297852), float2(0.0038453300949186087, 0.9388409852981567),
    float2(0.5227519869804382, 0.14627499878406525), float2(0.9875180125236511, 0.9389939904212952),
    float2(0.7701039910316467, 0.3155309855937958), float2(0.04483170062303543, 0.26883798837661743),
    float2(0.972320020198822, 0.43855100870132446), float2(0.6903589963912964, 0.9747310280799866),
    float2(0.5827199816703796, 0.8335520029067993), float2(0.49678000807762146, 0.9989929795265198),
    float2(0.498214989900589, 0.6034730076789856), float2(0.9164400100708008, 0.5907769799232483),
    float2(0.851131021976471, 0.21952000260353088), float2(0.4134649932384491, 0.893123984336853),
    float2(0.004425180144608021, 0.015686500817537308), float2(0.5808889865875244, 0.027405599132180214),
    float2(0.09085360169410706, 0.36497101187705994), float2(0.9074980020523071, 0.3878290057182312),
    float2(0.1073639988899231, 0.7465130090713501), float2(0.9870910048484802, 0.18369099497795105),
    float2(0.30414700508117676, 0.5427410006523132), float2(0.7691270112991333, 0.022675300016999245),
    float2(0.8954439759254456, 0.05838190019130707), float2(0.70967698097229, 0.20316199958324432),
    float2(0.4203920066356659, 0.4147160053253174), float2(0.8330940008163452, 0.15762799978256226),
    float2(0.29096299409866333, 0.19553199410438538), float2(0.4844200015068054, 0.9089329838752747),
    float2(0.7604910135269165, 0.9561449885368347), float2(0.03717150166630745, 0.5517749786376953),
    float2(0.14200299978256226, 0.19510500133037567), float2(0.9505599737167358, 0.7496259808540344),
    float2(0.36411601305007935, 0.7906429767608643), float2(0.22901099920272827, 0.8579360246658325),
    float2(0.7427290081977844, 0.7322310209274292), float2(0.7128509879112244, 0.3787960112094879),
    float2(0.34632399678230286, 0.4421829879283905), float2(0.4817650020122528, 0.22287699580192566),
    float2(0.6732990145683289, 0.5668200254440308), float2(0.0006408889894373715, 0.3230080008506775),
    float2(0.8751789927482605, 0.32813501358032227), float2(0.390514999628067, 0.3244419991970062),
    float2(0.9904170036315918, 0.6501359939575195), float2(0.35621199011802673, 0.9518420100212097),
    float2(0.43202000856399536, 0.796563982963562), float2(0.6701859831809998, 0.44901901483535767),
    float2(0.6072880029678345, 0.7214270234107971), float2(0.13770000636577606, 0.5319679975509644),
    float2(0.7076939940452576, 0.8413950204849243), float2(0.8066959977149963, 0.8207039833068848),
    float2(0.6437270045280457, 0.10141299664974213), float2(0.2517470121383667, 0.00012207399413455278),
    float2(0.5584890246391296, 0.4125489890575409), float2(0.5076450109481812, 0.00634784996509552),
    float2(0.8238170146942139, 0.40803200006484985), float2(0.3017059862613678, 0.35984399914741516),
    float2(0.30072900652885437, 0.6263920068740845), float2(0.9513229727745056, 0.5352029800415039),
    float2(0.11658100038766861, 0.8782010078430176), float2(0.3677479922771454, 0.0460829995572567),
    float2(0.2561720013618469, 0.7403180003166199), float2(0.16229699552059174, 0.9830009937286377),
    float2(0.5321210026741028, 0.4979709982872009), float2(0.20673200488090515, 0.23987500369548798),
    float2(0.10248100012540817, 0.6265760064125061), float2(0.34681200981140137, 0.6882839798927307),
    float2(0.903531014919281, 0.6722310185432434), float2(0.17407800257205963, 0.12005999684333801),
    float2(0.31723999977111816, 0.285317987203598), float2(0.09793390333652496, 0.9796140193939209),
    float2(0.19544099271297455, 0.38596799969673157), float2(0.11398699879646301, 0.300942987203598),
    float2(0.8307440280914307, 0.5856199860572815), float2(0.5628529787063599, 0.6620380282402039),
    float2(0.3735159933567047, 0.11416999995708466), float2(0.887935996055603, 0.9788510203361511),
    float2(0.9789119958877563, 0.8495740294456482), float2(0.5026400089263916, 0.06869719922542572),
    float2(0.1686760038137436, 0.05017239972949028), float2(0.8659319877624512, 0.5303509831428528),
    float2(0.9236429929733276, 0.1647389978170395), float2(0.04965360090136528, 0.2052370011806488),
    float2(0.8231760263442993, 0.0799890011548996), float2(0.024811500683426857, 0.6864219903945923),
    float2(0.8720660209655762, 0.871882975101471), float2(0.6138799786567688, 0.991362988948822),
    float2(0.09607230126857758, 0.0949430987238884), float2(0.8258919715881348, 0.2834559977054596),
    float2(0.18890999257564545, 0.4492020010948181), float2(0.6251720190048218, 0.9024930000305176),
    float2(0.5876340270042419, 0.5638599991798401), float2(0.012054800055921078, 0.4879299998283386),
    float2(0.3265480101108551, 0.8919640183448792), float2(0.9322789907455444, 0.8912010192871094),
    float2(0.49372801184654236, 0.6959750056266785), float2(0.6568499803543091, 0.780144989490509),
    float2(0.47056499123573303, 0.46147000789642334), float2(0.3726620078086853, 0.25141099095344543),
    float2(0.8736839890480042, 0.4529249966144562), float2(0.1747490018606186, 0.6543470025062561),
    float2(0.6952120065689087, 0.3021329939365387), float2(0.09561450034379959, 0.8131960034370422),
    float2(0.034150201827287674, 0.07660149782896042), float2(0.06680499762296677, 0.9160130023956299),
    float2(0.23401600122451782, 0.6280710101127625), float2(0.616595983505249, 0.376446008682251),
    float2(0.5639820098876953, 0.22901099920272827), float2(0.010223699733614922, 0.865444004535675),
    float2(0.41499099135398865, 0.6073489785194397), float2(0.631397008895874, 0.2421029955148697),
    float2(0.07110810279846191, 0.002563549904152751), float2(0.8778039813041687, 0.7384870052337646),
    float2(0.9971920251846313, 0.03616439923644066), float2(0.4361099898815155, 0.11423099786043167),
    float2(0.9591969847679138, 0.33796200156211853), float2(0.7051299810409546, 0.04675440117716789),
    float2(0.18008999526500702, 0.3244119882583618), float2(0.6102179884910583, 0.1621749997138977),
    float2(0.5471659898757935, 0.300942987203598), float2(0.18503400683403015, 0.9186990261077881),
    float2(0.4466080069541931, 0.9603869915008545), float2(0.954069972038269, 0.9948729872703552),
    float2(0.33497101068496704, 0.166935995221138), float2(0.3333840072154999, 0.21280600130558014),
    float2(0.4661700129508972, 0.5096290111541748), float2(0.36365899443626404, 0.34330299496650696),
    float2(0.17191100120544434, 0.17108699679374695), float2(0.759880006313324, 0.4608910083770752),
    float2(0.2912079989910126, 0.9834280014038086), float2(0.7586290240287781, 0.10745599865913391),
    float2(0.04507580026984215, 0.596759021282196), float2(0.9029819965362549, 0.5074009895324707),
    float2(0.5965149998664856, 0.7915890216827393), float2(0.17499299347400665, 0.24253100156784058),
    float2(0.4685809910297394, 0.5535449981689453), float2(0.8664510250091553, 0.6336860060691833),
    float2(0.6723840236663818, 0.06936860084533691), float2(0.23993700742721558, 0.18555299937725067),
    float2(0.6918849945068359, 0.7355570197105408), float2(0.6443979740142822, 0.7349770069122314),
    float2(0.41938498616218567, 0.558152973651886), float2(0.4960170090198517, 0.43458399176597595),
    float2(0.6652119755744934, 0.913815975189209), float2(0.2779630124568939, 0.765434980392456),
    float2(0.08548229932785034, 0.17151400446891785), float2(0.30582600831985474, 0.39497101306915283),
    float2(0.7080289721488953, 0.5744190216064453), float2(0.7287819981575012, 0.16055800020694733),
    float2(0.1861020028591156, 0.28904101252555847), float2(0.6779379844665527, 0.14361999928951263),
    float2(0.14032399654388428, 0.7077850103378296), float2(0.05908380076289177, 0.760308027267456),
    float2(0.6109809875488281, 0.4517959952354431), float2(0.03625600039958954, 0.007629630155861378),
    float2(0.9473249912261963, 0.4029659926891327), float2(0.19592900574207306, 0.6829130053520203),
    float2(0.057710498571395874, 0.46809300780296326), float2(0.5917540192604065, 0.2777490019798279),
    float2(0.7339699864387512, 0.6210520267486572), float2(0.9282199740409851, 0.7740709781646729),
    float2(0.8901939988136292, 0.18771900236606598), float2(0.740805983543396, 0.0805383026599884),
    float2(0.7708979845046997, 0.5667589902877808), float2(0.480087012052536, 0.13376300036907196),
    float2(0.33899998664855957, 0.016968300566077232), float2(0.8547930121421814, 0.8099920153617859),
    float2(0.5144199728965759, 0.2596510052680969), float2(0.1364479959011078, 0.6233410239219666),
    float2(0.3690600097179413, 0.5036159753799438), float2(0.33542901277542114, 0.6557819843292236),
    float2(0.5450000166893005, 0.6102790236473083), float2(0.9918820261955261, 0.7283239960670471),
    float2(0.27637600898742676, 0.8976100087165833), float2(0.6288340091705322, 0.9483630061149597),
    float2(0.5381019711494446, 0.7844169735908508), float2(0.06775110214948654, 0.057588398456573486),
    float2(0.09720149636268616, 0.035279400646686554), float2(0.4394359886646271, 0.6425979733467102),
    float2(0.2509230077266693, 0.3704639971256256), float2(0.45289498567581177, 0.17441299557685852),
    float2(0.20642699301242828, 0.7395550012588501), float2(0.4592120051383972, 0.3491320013999939),
    float2(0.8646810054779053, 0.020020099356770515), float2(0.6320689916610718, 0.18671199679374695),
    float2(0.792352020740509, 0.21115800738334656), float2(0.09134189784526825, 0.40156298875808716),
    float2(0.3082979917526245, 0.2425609976053238), float2(0.5730460286140442, 0.3318580090999603),
    float2(0.5030670166015625, 0.6413159966468811), float2(0.665058970451355, 0.8760030269622803),
    float2(0.2581869959831238, 0.5482649803161621), float2(0.873134970664978, 0.6994839906692505),
    float2(0.3422960042953491, 0.11078199744224548), float2(0.2691729962825775, 0.21857400238513947),
    float2(0.07446520030498505, 0.5483570098876953), float2(0.35157299041748047, 0.40360701084136963),
    float2(0.6602979898452759, 0.3413800001144409), float2(0.86285001039505, 0.09915459901094437),
    float2(0.20813600718975067, 0.952817976474762), float2(0.19370099902153015, 0.029023099690675735),
    float2(0.40882599353790283, 0.1530199944972992), float2(0.7104399800300598, 0.7962579727172852),
    float2(0.7826780080795288, 0.7365339994430542), float2(0.8307139873504639, 0.7723320126533508),
    float2(0.4948880076408386, 0.4970549941062927), float2(0.7148659825325012, 0.6980500221252441),
    float2(0.9317910075187683, 0.1991640031337738), float2(0.6378369927406311, 0.8089849948883057),
    float2(0.6657000184059143, 0.5995969772338867), float2(0.7531049847602844, 0.6726580262184143),
    float2(0.5937070250511169, 0.41236600279808044), float2(0.22888900339603424, 0.48976099491119385),
    float2(0.559099018573761, 0.10855399817228317), float2(0.6749169826507568, 0.3947260081768036),
    float2(0.7104399800300598, 0.46766600012779236), float2(0.10541100054979324, 0.22852300107479095),
    float2(0.5172579884529114, 0.10803599655628204), float2(0.9805899858474731, 0.546953022480011),
    float2(0.394665002822876, 0.8042240142822266), float2(0.8669999837875366, 0.16284699738025665),
    float2(0.8225650191307068, 0.6406139731407166), float2(0.8028500080108643, 0.25720998644828796),
    float2(0.44144999980926514, 0.2212589979171753), float2(0.959318995475769, 0.7075719833374023),
    float2(0.6282539963722229, 0.6852020025253296), float2(0.1322370022535324, 0.24506400525569916),
    float2(0.041200000792741776, 0.8709679841995239), float2(0.8260440230369568, 0.4974820017814636),
    float2(0.24625399708747864, 0.6735129952430725), float2(0.025544000789523125, 0.4378489851951599),
    float2(0.23813599348068237, 0.22241899371147156), float2(0.16843199729919434, 0.887050986289978),
    float2(0.2742390036582947, 0.33637499809265137), float2(0.35056599974632263, 0.8275700211524963),
    float2(0.20206299424171448, 0.5102999806404114), float2(0.3134250044822693, 0.9222080111503601),
    float2(0.05026400089263916, 0.7097989916801453), float2(0.36939600110054016, 0.5843070149421692),
    float2(0.07763910293579102, 0.4414199888706207), float2(0.4021419882774353, 0.7092499732971191),
    float2(0.2239139974117279, 0.7875909805297852), float2(0.9979860186576843, 0.9055449962615967),
    float2(0.4920800030231476, 0.7682120203971863), float2(0.4643999934196472, 0.04620499908924103),
    float2(0.5120700001716614, 0.9446390271186829), float2(0.6356390118598938, 0.6482740044593811),
    float2(0.8786889910697937, 0.25064900517463684), float2(0.27533799409866333, 0.1322370022535324),
    float2(0.8996549844741821, 0.7868279814720154), float2(0.30582600831985474, 0.7288129925727844),
    float2(0.9813230037689209, 0.8091070055961609), float2(0.32331299781799316, 0.7908869981765747),
    float2(0.5439310073852539, 0.3821530044078827), float2(0.2187259942293167, 0.6564840078353882),
    float2(0.5931580066680908, 0.6300240159034729), float2(0.14828899502754211, 0.2917569875717163),
    float2(0.9242839813232422, 0.038453299552202225), float2(0.5154579877853394, 0.3307900130748749),
    float2(0.16144299507141113, 0.0877406969666481), float2(0.9222999811172485, 0.5511339902877808),
    float2(0.07669299840927124, 0.32676199078559875), float2(0.1838739961385727, 0.5891600251197815),
    float2(0.4387040138244629, 0.387706995010376), float2(0.8076720237731934, 0.607990026473999),
    float2(0.9334390163421631, 0.9492779970169067), float2(0.7456279993057251, 0.28369998931884766),
    float2(0.8460950255393982, 0.9068880081176758), float2(0.8607749938964844, 0.06769009679555893),
    float2(0.5854060053825378, 0.1382180005311966), float2(0.8695639967918396, 0.9325540065765381),
    float2(0.7618640065193176, 0.4056209921836853), float2(0.3135170042514801, 0.6791890263557434),
    float2(0.5335549712181091, 0.05142369866371155), float2(0.20685400068759918, 0.11807599663734436),
    float2(0.38502201437950134, 0.7539600133895874), float2(0.4065069854259491, 0.9872130155563354),
    float2(0.7939079999923706, 0.09723199903964996), float2(0.5392929911613464, 0.993133008480072),
    float2(0.35935500264167786, 0.7271339893341064), float2(0.9175080060958862, 0.33622199296951294),
    float2(0.0383617989718914, 0.9344459772109985), float2(0.8032469749450684, 0.9429299831390381),
    float2(0.09891050308942795, 0.5130770206451416), float2(0.6351510286331177, 0.026551099494099617),
    float2(0.02212589979171753, 0.1808219999074936), float2(0.4687950015068054, 0.09451580047607422),
    float2(0.614031970500946, 0.5858640074729919), float2(0.47022899985313416, 0.31464600563049316),
    float2(0.707053005695343, 0.430525004863739), float2(0.03366189822554588, 0.7913450002670288),
    float2(0.44935500621795654, 0.8981900215148926), float2(0.8767359852790833, 0.2822049856185913),
    float2(0.17279599606990814, 0.5384079813957214), float2(0.25800299644470215, 0.4765770137310028),
    float2(0.5892509818077087, 0.8811910152435303), float2(0.34333300590515137, 0.5415509939193726),
    float2(0.25071001052856445, 0.4148379862308502), float2(0.27228599786758423, 0.8397780060768127),
    float2(0.5558339953422546, 0.9547110199928284), float2(0.013550199568271637, 0.6540729999542236),
    float2(0.3352150022983551, 0.6053959727287292), float2(0.10904300212860107, 0.5640429854393005),
    float2(0.12421000003814697, 0.16727200150489807), float2(0.3903929889202118, 0.9488199949264526),
    float2(0.8118230104446411, 0.03530989959836006), float2(0.665913999080658, 0.1921750009059906),
    float2(0.7866150140762329, 0.5288860201835632), float2(0.8380079865455627, 0.9941099882125854),
    float2(0.9034090042114258, 0.4408090114593506), float2(0.8189949989318848, 0.7336339950561523),
    float2(0.5269020199775696, 0.8884850144386292), float2(0.9153419733047485, 0.6300240159034729),
    float2(0.8071539998054504, 0.4390699863433838), float2(0.9585559964179993, 0.1530809998512268),
    float2(0.05508590117096901, 0.8280280232429504), float2(0.5516219735145569, 0.004120000172406435),
    float2(0.6189460158348083, 0.8395339846611023), float2(0.4622940123081207, 0.7839900255203247),
    float2(0.8262280225753784, 0.12442400306463242), float2(0.927702009677887, 0.09329509735107422),
    float2(0.5537580251693726, 0.26767799258232117), float2(0.03744620084762573, 0.352183997631073),
    float2(0.38016900420188904, 0.544327974319458), float2(0.8460339903831482, 0.6719570159912109),
    float2(0.7600020170211792, 0.2283090054988861), float2(0.4577470123767853, 0.9305700063705444),
    float2(0.839838981628418, 0.436598002910614), float2(0.2591629922389984, 0.8022710084915161),
    float2(0.9543439745903015, 0.9241920113563538), float2(0.7391279935836792, 0.03790400177240372),
    float2(0.195592999458313, 0.8325750231742859), float2(0.33365899324417114, 0.8562269806861877),
    float2(0.5727710127830505, 0.4569540023803711), float2(0.2591019868850708, 0.036316998302936554),
    float2(0.9794920086860657, 0.6032590270042419), float2(0.26157400012016296, 0.9602950215339661),
    float2(0.6637780070304871, 0.5244609713554382), float2(0.70100998878479, 0.882777988910675),
    float2(0.1817069947719574, 0.7600330114364624), float2(0.1148110032081604, 0.9175999760627747),
    float2(0.9108859896659851, 0.25141099095344543), float2(0.43717798590660095, 0.7649160027503967),
    float2(0.14874699711799622, 0.5772579908370972), float2(0.04138309881091118, 0.30802300572395325),
    float2(0.06421089917421341, 0.659017026424408), float2(0.06592000275850296, 0.9849849939346313),
    float2(0.9661549925804138, 0.022553199902176857), float2(0.2212589979171753, 0.4282050132751465),
    float2(0.9422590136528015, 0.29847100377082825), float2(0.5663629770278931, 0.526108980178833),
    float2(0.41761499643325806, 0.05960259959101677), float2(0.029572399333119392, 0.9823600053787231),
    float2(0.766319990158081, 0.9114660024642944), float2(0.00012207399413455278, 0.9023410081863403),
    float2(0.6978060007095337, 0.5262309908866882), float2(0.5988039970397949, 0.5335860252380371),
    float2(0.27808499336242676, 0.5724660158157349), float2(0.5938599705696106, 0.6913049817085266),
    float2(0.39493998885154724, 0.18863500654697418), float2(0.22229699790477753, 0.7057099938392639),
    float2(0.7985780239105225, 0.9938960075378418), float2(0.48066699504852295, 0.8187509775161743),
    float2(0.12372200191020966, 0.3670769929885864), float2(0.7100440263748169, 0.6486709713935852),
    float2(0.9430519938468933, 0.26413801312446594), float2(0.7608259916305542, 0.6033509969711304),
    float2(0.6530050039291382, 0.27854201197624207), float2(0.5258030295372009, 0.4099859893321991),
    float2(0.1334269940853119, 0.677174985408783), float2(0.7902460098266602, 0.2891629934310913),
    float2(0.256630003452301, 0.7044280171394348), float2(0.5891289710998535, 0.07733389735221863),
    float2(0.9811090230941772, 0.23142200708389282), float2(0.20831899344921112, 0.8974270224571228),
    float2(0.5684379935264587, 0.7473070025444031), float2(0.13040600717067719, 0.04840239882469177),
    float2(0.5238500237464905, 0.8460649847984314), float2(0.549485981464386, 0.6958829760551453),
    float2(0.3769649863243103, 0.42201000452041626), float2(0.4108709990978241, 0.6622520089149475),
    float2(0.2935880124568939, 0.055848900228738785), float2(0.4882049858570099, 0.965453028678894),
    float2(0.7264630198478699, 0.9537339806556702), float2(0.9837639927864075, 0.0706809014081955),
    float2(0.7990660071372986, 0.14807599782943726), float2(0.6926789879798889, 0.24274399876594543),
    float2(0.8107550144195557, 0.37058600783348083), float2(0.7613450288772583, 0.7735530138015747),
    float2(0.5843989849090576, 0.19711899757385254), float2(0.3822439908981323, 0.649586021900177),
    float2(0.5361189842224121, 0.5626090168952942), float2(0.1069369986653328, 0.7808769941329956),
    float2(0.04721210151910782, 0.5020599961280823), float2(0.7670519948005676, 0.25974300503730774),
    float2(0.21903100609779358, 0.269569993019104), float2(0.9999079704284668, 0.7610099911689758),
    float2(0.7401350140571594, 0.0054933298379182816), float2(0.9114959836006165, 0.0007019260083325207),
    float2(0.0054933298379182816, 0.11359000205993652), float2(0.746940016746521, 0.5364239811897278),
    float2(0.8309879899024963, 0.858119010925293), float2(0.9400010108947754, 0.46684199571609497),
    float2(0.46415600180625916, 0.7427589893341064), float2(0.3052160143852234, 0.11606200039386749),
    float2(0.2652060091495514, 0.6256899833679199), float2(0.3504140079021454, 0.7576829791069031),
    float2(0.5263530015945435, 0.22064900398254395), float2(0.43202000856399536, 0.835536003112793),
    float2(0.45939499139785767, 0.6797080039978027), float2(0.7512440085411072, 0.8253120183944702),
    float2(0.9889219999313354, 0.36979299783706665), float2(0.554764986038208, 0.9222080111503601),
    float2(0.13818800449371338, 0.33500200510025024), float2(0.06369210034608841, 0.6258739829063416),
    float2(0.7296370267868042, 0.9963380098342896), float2(0.8769490122795105, 0.5734729766845703),
    float2(0.30063799023628235, 0.15240900218486786), float2(0.41410601139068604, 0.27835899591445923),
    float2(0.4047060012817383, 0.4435249865055084), float2(0.5926390290260315, 0.9656059741973877),
    float2(0.16592900454998016, 0.39811399579048157), float2(0.40757501125335693, 0.3829460144042969),
    float2(0.14102600514888763, 0.9392679929733276), float2(0.31070899963378906, 0.44724899530410767),
    float2(0.9505910277366638, 0.8207039833068848), float2(0.185247004032135, 0.6207159757614136),
    float2(0.4216740131378174, 0.4699850082397461), float2(0.995727002620697, 0.4603720009326935),
    float2(0.6359750032424927, 0.06024349853396416), float2(0.6675620079040527, 0.7038180232048035),
    float2(0.9926760196685791, 0.50968998670578), float2(0.2269970029592514, 0.8265330195426941),
    float2(0.08923610299825668, 0.27405598759651184), float2(0.12225700169801712, 0.4274730086326599),
    float2(0.008758810348808765, 0.21646800637245178), float2(0.9691460132598877, 0.9648119807243347),
    float2(0.5273900032043457, 0.7440410256385803), float2(0.376446008682251, 0.46232500672340393),
    float2(0.2663959860801697, 0.27372100949287415), float2(0.7247229814529419, 0.5046539902687073),
    float2(0.33017998933792114, 0.0569474995136261), float2(0.10275600105524063, 0.7081519961357117),
    float2(0.2038940042257309, 0.07797479629516602), float2(0.27274399995803833, 0.09881889820098877),
    float2(0.8383129835128784, 0.5476850271224976), float2(0.13641799986362457, 0.0021057799458503723),
    float2(0.1447490006685257, 0.8574180006980896), float2(0.36631399393081665, 0.30069899559020996),
    float2(0.8690450191497803, 0.36930400133132935), float2(0.33710700273513794, 0.36228498816490173),
    float2(0.6480000019073486, 0.992247998714447), float2(0.15732300281524658, 0.4629659950733185),
    float2(0.2579120099544525, 0.5161899924278259), float2(0.756401002407074, 0.36332300305366516),
    float2(0.07925660163164139, 0.863277018070221), float2(0.49708500504493713, 0.872829020023346),
    float2(0.7334210276603699, 0.2482379972934723), float2(0.8385570049285889, 0.33674100041389465),
    float2(0.701865017414093, 0.6125680208206177), float2(0.8915070295333862, 0.1351660043001175),
    float2(0.9291669726371765, 0.8591570258140564), float2(0.16385400295257568, 0.8232979774475098),
    float2(0.29578500986099243, 0.8687090277671814), float2(0.14365099370479584, 0.1382489949464798),
    float2(0.6873679757118225, 0.00015259299834724516), float2(0.19272400438785553, 0.19836999475955963),
    float2(0.4761190116405487, 0.005981630180031061), float2(0.0029603000730276108, 0.05365150049328804),
    float2(0.37595799565315247, 0.8765529990196228), float2(0.48942500352859497, 0.17853300273418427),
    float2(0.44901901483535767, 0.2751240134239197), float2(0.05920590087771416, 0.10687600076198578),
    float2(0.21671199798583984, 0.33851099014282227), float2(0.4308910071849823, 0.3313089907169342),
    float2(0.3969849944114685, 0.018188999965786934), float2(0.7869200110435486, 0.34943699836730957),
    float2(0.4629659950733185, 0.5937380194664001), float2(0.32071900367736816, 0.48463401198387146),
    float2(0.5358740091323853, 0.18234799802303314), float2(0.5571150183677673, 0.07528919726610184),
    float2(0.3522450029850006, 0.9188820123672485), float2(0.5332189798355103, 0.5303199887275696),
    float2(0.004028439987450838, 0.5356910228729248), float2(0.10589899867773056, 0.14139199256896973),
    float2(0.3878290057182312, 0.83992999792099), float2(0.9102150201797485, 0.7370830178260803),
    float2(0.9266639947891235, 0.9795830249786377), float2(0.6080809831619263, 0.8122199773788452),
    float2(0.9649649858474731, 0.27631500363349915), float2(0.14288799464702606, 0.44889700412750244),
    float2(0.7782829999923706, 0.9297770261764526), float2(0.8665729761123657, 0.9796749949455261),
    float2(0.6211429834365845, 0.03671380132436752), float2(0.14835000038146973, 0.32108500599861145),
    float2(0.39762601256370544, 0.52531498670578), float2(0.09006009995937347, 0.532243013381958),
    float2(0.5182650089263916, 0.5578479766845703), float2(0.9172340035438538, 0.6475110054016113),
    float2(0.4321730136871338, 0.5689259767532349), float2(0.20355799794197083, 0.2852570116519928),
    float2(0.7240210175514221, 0.09759820252656937), float2(0.7398300170898438, 0.652974009513855),
    float2(0.026337500661611557, 0.46317899227142334), float2(0.3448899984359741, 0.5820180177688599),
    float2(0.6165649890899658, 0.2599869966506958), float2(0.8989840149879456, 0.29755499958992004),
    float2(0.6288949847221375, 0.8738669753074646), float2(0.2642289996147156, 0.7735530138015747),
    float2(0.27021101117134094, 0.05664239823818207), float2(0.25159499049186707, 0.26065900921821594),
    float2(0.4109010100364685, 0.3521530032157898), float2(0.2665790021419525, 0.8811910152435303),
    float2(0.8006229996681213, 0.5765249729156494), float2(0.04422130063176155, 0.9715870022773743),
    float2(0.7536550164222717, 0.7961670160293579), float2(0.9815670251846313, 0.6637780070304871),
    float2(0.6142150163650513, 0.19577600061893463), float2(0.9745169878005981, 0.46595698595046997),
    float2(0.7259439826011658, 0.7727289795875549), float2(0.10940899699926376, 0.18436199426651),
    float2(0.3935059905052185, 0.9672539830207825), float2(0.6308789849281311, 0.1587270051240921),
    float2(0.07803580164909363, 0.71144700050354), float2(0.2816550135612488, 0.35581499338150024),
    float2(0.6762599945068359, 0.12686499953269958), float2(0.5257419943809509, 0.029328299686312675),
    float2(0.6664940118789673, 0.3791919946670532), float2(0.09625539928674698, 0.3855710029602051),
    float2(0.9055150151252747, 0.9863280057907104), float2(0.794031023979187, 0.7696769833564758),
    float2(0.5939819812774658, 0.6646019816398621), float2(0.24237799644470215, 0.7307350039482117),
    float2(0.34241798520088196, 0.7878350019454956), float2(0.27280500531196594, 0.9724720120429993),
    float2(0.7841730117797852, 0.975737988948822), float2(0.08322399854660034, 0.7509689927101135),
    float2(0.1539350003004074, 0.9025539755821228), float2(0.5869929790496826, 0.2986850142478943),
    float2(0.6508380174636841, 0.46540701389312744), float2(0.9307839870452881, 0.41453298926353455),
    float2(0.04864649847149849, 0.1587270051240921), float2(0.2348400056362152, 0.9785450100898743),
    float2(0.9290140271186829, 0.01223789993673563), float2(0.8074589967727661, 0.3382669985294342),
    float2(0.279092013835907, 0.4622940123081207), float2(0.8978850245475769, 0.3205359876155853),
    float2(0.16229699552059174, 0.5076749920845032), float2(0.024140100926160812, 0.8540909886360168),
    float2(0.32322201132774353, 0.6404920220375061), float2(0.774528980255127, 0.44495999813079834),
    float2(0.583361029624939, 0.5878170132637024), float2(0.23020100593566895, 0.5282449722290039),
    float2(0.7177950143814087, 0.5284280180931091), float2(0.895779013633728, 0.9174169898033142),
    float2(0.05331579968333244, 0.08786279708147049), float2(0.6327099800109863, 0.2616350054740906),
    float2(0.7405930161476135, 0.10132099688053131), float2(0.7268900275230408, 0.81563800573349),
    float2(0.4935759902000427, 0.7398599982261658), float2(0.19110700488090515, 0.25623300671577454),
    float2(0.7097690105438232, 0.33460500836372375), float2(0.30463600158691406, 0.4738300144672394),
    float2(0.1699880063533783, 0.8445690274238586), float2(0.1622059941291809, 0.13837100565433502),
    float2(0.6375619769096375, 0.5273900032043457), float2(0.9686880111694336, 0.5222629904747009),
    float2(0.6437569856643677, 0.2951439917087555), float2(0.8762779831886292, 0.06378369778394699),
    float2(0.8490859866142273, 0.6306959986686707), float2(0.3152559995651245, 0.31974199414253235),
    float2(0.6399120092391968, 0.0016479999758303165), float2(0.7389140129089355, 0.8538159728050232),
    float2(0.834650993347168, 0.6953639984130859), float2(0.21842099726200104, 0.5859249830245972),
    float2(0.20313100516796112, 0.3199560046195984), float2(0.2605060040950775, 0.9350569844245911),
    float2(0.5032200217247009, 0.22409699857234955), float2(0.9806510210037231, 0.6869109869003296),
    float2(0.04492320120334625, 0.29142099618911743), float2(0.6318550109863281, 0.45756399631500244),
    float2(0.6268810033798218, 0.783715009689331), float2(0.4195989966392517, 0.6993619799613953),
    float2(0.0400707982480526, 0.055970899760723114), float2(0.03051850013434887, 0.9512010216712952),
    float2(0.9286779761314392, 0.929502010345459), float2(0.8272039890289307, 0.7477650046348572),
    float2(0.11105699837207794, 0.01834160089492798), float2(0.5337380170822144, 0.9626150131225586),
    float2(0.9068269729614258, 0.6102790236473083), float2(0.5822929739952087, 0.2507399916648865),
    float2(0.05746639892458916, 0.8055970072746277), float2(0.711355984210968, 0.3095490038394928),
    float2(0.3408310115337372, 0.9883419871330261), float2(0.8656880259513855, 0.1908629983663559),
    float2(0.28107500076293945, 0.8549759984016418), float2(0.673695981502533, 0.6842250227928162),
    float2(0.4851219952106476, 0.8502150177955627), float2(0.12683500349521637, 0.11697699874639511),
    float2(0.38239699602127075, 0.7724850177764893), float2(0.14078199863433838, 0.4656510055065155),
    float2(0.3996399939060211, 0.7349159717559814), float2(0.925961971282959, 0.6934720277786255),
    float2(0.003295999951660633, 0.4524979889392853), float2(0.018677299842238426, 0.05279700085520744),
    float2(0.31504300236701965, 0.6172059774398804), float2(0.9683520197868347, 0.2957240045070648),
    float2(0.1404460072517395, 0.21460600197315216), float2(0.8244580030441284, 0.9115880131721497),
    float2(0.6701560020446777, 0.2635580003261566), float2(0.9408860206604004, 0.07382430136203766),
    float2(0.5609909892082214, 0.855434000492096), float2(0.9088109731674194, 0.40531599521636963),
    float2(0.04318369925022125, 0.0306100994348526), float2(0.8865630030632019, 0.5467389822006226),
    float2(0.5602589845657349, 0.3935360014438629), float2(0.7865840196609497, 0.6956999897956848),
    float2(0.9813230037689209, 0.3108919858932495), float2(0.741690993309021, 0.807000994682312),
    float2(0.7927489876747131, 0.9555649757385254), float2(0.03796499967575073, 0.1091340035200119),
    float2(0.5427719950675964, 0.3266089856624603), float2(0.2568129897117615, 0.2299260050058365),
    float2(0.5290690064430237, 0.12536999583244324), float2(0.15723100304603577, 0.21924500167369843),
    float2(0.5113679766654968, 0.6803799867630005), float2(0.8819540143013, 0.08236949890851974),
    float2(0.7788019776344299, 0.3316139876842499), float2(0.8931549787521362, 0.23706799745559692),
    float2(0.9986569881439209, 0.2539139986038208), float2(0.5540030002593994, 0.3552660048007965),
    float2(0.3447679877281189, 0.32279399037361145), float2(0.06250189989805222, 0.5236979722976685),
    float2(0.48634299635887146, 0.7996150255203247), float2(0.09692680090665817, 0.8869900107383728),
    float2(0.10077200084924698, 0.3481859862804413), float2(0.30857300758361816, 0.3744010031223297),
    float2(0.11847300082445145, 0.6935030221939087), float2(0.27414798736572266, 0.17810599505901337),
    float2(0.10101599991321564, 0.6044800281524658), float2(0.44770699739456177, 0.8116400241851807),
    float2(0.6609389781951904, 0.8439589738845825), float2(0.8148750066757202, 0.7059850096702576),
    float2(0.19531799852848053, 0.06024349853396416), float2(0.17032399773597717, 0.3525800108909607),
    float2(0.14594000577926636, 0.2640460133552551), float2(0.6514790058135986, 0.8976100087165833),
    float2(0.5121009945869446, 0.04525899887084961), float2(0.9422889947891235, 0.6454660296440125),
    float2(0.8371229767799377, 0.566057026386261), float2(0.6933190226554871, 0.7099519968032837),
    float2(0.22318199276924133, 0.2987760007381439), float2(0.776665985584259, 0.991424024105072),
    float2(0.6462900042533875, 0.8787189722061157), float2(0.013794399797916412, 0.06796470284461975),
    float2(0.5755490064620972, 0.8907129764556885), float2(0.6829130053520203, 0.5961490273475647),
    float2(0.6779080033302307, 0.17645800113677979), float2(0.3838619887828827, 0.20413799583911896),
    float2(0.039155200123786926, 0.65385901927948), float2(0.7220979928970337, 0.9296240210533142),
    float2(0.4055599868297577, 0.6443679928779602), float2(0.646992027759552, 0.7561569809913635),
    float2(0.9165930151939392, 0.11093500256538391), float2(0.7798699736595154, 0.5862910151481628),
    float2(0.5788750052452087, 0.7039700150489807), float2(0.1622059941291809, 0.6864219903945923),
    float2(0.5453659892082214, 0.5935239791870117), float2(0.6008179783821106, 0.9816280007362366),
    float2(0.4918060004711151, 0.32804301381111145), float2(0.3344219923019409, 0.036591701209545135),
    float2(0.437483012676239, 0.4643389880657196), float2(0.11142300069332123, 0.2599259912967682),
    float2(0.8161569833755493, 0.5559859871864319), float2(0.5656300187110901, 0.5763729810714722),
    float2(0.4595780074596405, 0.6119269728660583), float2(0.7170630097389221, 0.06869719922542572),
    float2(0.8241519927978516, 0.5289160013198853), float2(0.09192179888486862, 0.2417680025100708),
    float2(0.07708980143070221, 0.3779410123825073), float2(0.8162180185317993, 0.9664300084114075),
    float2(0.07837150245904922, 0.5922729969024658), float2(0.10809700191020966, 0.06491290032863617),
    float2(0.664205014705658, 0.9528489708900452), float2(0.6889550089836121, 0.5806760191917419),
    float2(0.06735440343618393, 0.2725299894809723), float2(0.02386550046503544, 0.7331770062446594),
    float2(0.8414869904518127, 0.9492779970169067), float2(0.9583420157432556, 0.4991300106048584),
    float2(0.2952969968318939, 0.9075899720191956), float2(0.4260689914226532, 0.6551409959793091),
    float2(0.3744930028915405, 0.8037049770355225), float2(0.45243701338768005, 0.7263100147247314),
    float2(0.770684003829956, 0.6922510266304016), float2(0.2409130036830902, 0.5623040199279785),
    float2(0.8374890089035034, 0.1417890042066574), float2(0.9472950100898743, 0.6683549880981445),
    float2(0.6531879901885986, 0.5027620196342468), float2(0.7993410229682922, 0.4907679855823517),
    float2(0.044373899698257446, 0.9018830060958862), float2(0.23279500007629395, 0.08410900086164474),
    float2(0.039277300238609314, 0.24481900036334991), float2(0.6735739707946777, 0.050630200654268265),
    float2(0.42893800139427185, 0.6259040236473083), float2(0.8502150177955627, 0.02804649993777275),
    float2(0.13934800028800964, 0.9736009836196899), float2(0.70973801612854, 0.1386760026216507),
    float2(0.7105010151863098, 0.24466699361801147), float2(0.806207001209259, 0.4577470123767853),
    float2(0.3526720106601715, 0.48011699318885803), float2(0.019013000652194023, 0.5774409770965576),
    float2(0.30634498596191406, 0.7099519968032837), float2(0.6133000254631042, 0.8608049750328064),
    float2(0.870693027973175, 0.6712549924850464), float2(0.1938840001821518, 0.1082490012049675),
    float2(0.8637959957122803, 0.11880899965763092), float2(0.6128730177879333, 0.6519359946250916),
    float2(0.740776002407074, 0.4344309866428375), float2(0.7747730016708374, 0.8911100029945374),
    float2(0.9734179973602295, 0.8720660209655762), float2(0.29666998982429504, 0.07504499703645706),
    float2(0.6775410175323486, 0.7664719820022583), float2(0.7274090051651001, 0.737326979637146),
    float2(0.231330007314682, 0.1134679988026619), float2(0.8655660152435303, 0.5137490034103394),
    float2(0.02783289924263954, 0.39231500029563904), float2(0.676351010799408, 0.0865200012922287),
    float2(0.08197270333766937, 0.12448500096797943), float2(0.7660449743270874, 0.9399700164794922),
    float2(0.4135870039463043, 0.81392902135849), float2(0.38462498784065247, 0.9191870093345642),
    float2(0.9143959879875183, 0.8536940217018127), float2(0.8333079814910889, 0.8886989951133728),
    float2(0.5034639835357666, 0.5336470007896423), float2(0.9488199949264526, 0.04211549833416939),
    float2(0.8899199962615967, 0.51670902967453), float2(0.029145199805498123, 0.1991640031337738),
    float2(0.6961269974708557, 0.9537950158119202), float2(0.6809290051460266, 0.8413649797439575),
    float2(0.4759669899940491, 0.42362698912620544), float2(0.8825039863586426, 0.4031189978122711),
    float2(0.48719701170921326, 0.6223949790000916), float2(0.29154300689697266, 0.28327301144599915),
    float2(0.41770699620246887, 0.8735920190811157), float2(0.5697810053825378, 0.5599539875984192),
    float2(0.7090370059013367, 0.819940984249115), float2(0.9451889991760254, 0.7791069746017456),
    float2(0.20664100348949432, 0.559831976890564), float2(0.15451499819755554, 0.8005009889602661),
    float2(0.761559009552002, 0.583666980266571), float2(0.8436229825019836, 0.5150610208511353),
    float2(0.9064610004425049, 0.36847999691963196), float2(0.7637559771537781, 0.1731320023536682),
    float2(0.3397020101547241, 0.2404550015926361), float2(0.9735710024833679, 0.1291240006685257),
    float2(0.5914790034294128, 0.32502201199531555), float2(0.7144380211830139, 0.22290700674057007),
    float2(0.28156399726867676, 0.014984600245952606), float2(0.37586599588394165, 0.157383993268013),
    float2(0.8169500231742859, 0.06500440090894699), float2(0.5927299857139587, 0.043763499706983566),
    float2(0.33323198556900024, 0.9622179865837097), float2(0.8095030188560486, 0.11258299648761749),
    float2(0.8926969766616821, 0.8699300289154053), float2(0.27539899945259094, 0.6830959916114807),
    float2(0.6876429915428162, 0.7784050107002258), float2(0.04742579907178879, 0.6855679750442505),
    float2(0.8386790156364441, 0.7967159748077393), float2(0.003295999951660633, 0.960204005241394),
    float2(0.4747759997844696, 0.7012540102005005), float2(0.6989960074424744, 0.40763598680496216),
    float2(0.46705499291419983, 0.9523910284042358), float2(0.009064000099897385, 0.7844780087471008),
    float2(0.7901549935340881, 0.8426160216331482), float2(0.7426679730415344, 0.9260839819908142),
    float2(0.9539480209350586, 0.08786279708147049), float2(0.7841119766235352, 0.3865779936313629),
    float2(0.4679099917411804, 0.6360059976577759), float2(0.2775050103664398, 0.7463300228118896),
    float2(0.9962769746780396, 0.017731299623847008), float2(0.6936249732971191, 0.015411799773573875),
    float2(0.16180899739265442, 0.27713900804519653), float2(0.7480700016021729, 0.5717949867248535),
    float2(0.38032200932502747, 0.30930501222610474), float2(0.26319199800491333, 0.0722372978925705),
    float2(0.12341699749231339, 0.736503005027771), float2(0.29456499218940735, 0.09595020115375519),
    float2(0.40058600902557373, 0.26285600662231445), float2(0.7446519732475281, 0.020508399233222008),
    float2(0.2360610067844391, 0.4338510036468506), float2(0.5066990256309509, 0.3561820089817047),
    float2(0.7633900046348572, 0.7488020062446594), float2(0.18716999888420105, 0.5315709710121155),
    float2(0.5283359885215759, 0.6516619920730591), float2(0.875361979007721, 0.8960540294647217),
    float2(0.3034459948539734, 0.9632560014724731), float2(0.28626400232315063, 0.6138489842414856),
    float2(0.41810399293899536, 0.3053379952907562), float2(0.6449170112609863, 0.386821985244751),
    float2(0.47199898958206177, 0.2539750039577484), float2(0.9628890156745911, 0.5816829800605774),
    float2(0.4387950003147125, 0.18417899310588837), float2(0.6507459878921509, 0.6331980228424072),
    float2(0.8930330276489258, 0.038544900715351105), float2(0.8532059788703918, 0.003906370140612125),
    float2(0.39274299144744873, 0.16058799624443054), float2(0.20053699612617493, 0.4065679907798767),
    float2(0.38416698575019836, 0.6161990165710449), float2(0.676351010799408, 0.10593000054359436),
    float2(0.061189599335193634, 0.7376319766044617), float2(0.5238810181617737, 0.6342049837112427),
    float2(0.07486189901828766, 0.8802449703216553), float2(0.2795189917087555, 0.6369820237159729),
    float2(0.9775689840316772, 0.5641040205955505), float2(0.6579790115356445, 0.7241430282592773),
    float2(0.587602972984314, 0.43284401297569275), float2(0.1535390019416809, 0.42844900488853455),
    float2(0.5446940064430237, 0.4000059962272644), float2(0.7756890058517456, 0.48335200548171997),
    float2(0.22074000537395477, 0.04196299985051155), float2(0.08465830236673355, 0.2912989854812622),
    float2(0.23642699420452118, 0.7124850153923035), float2(0.00299080996774137, 0.349590003490448),
    float2(0.9074069857597351, 0.7080289721488953), float2(0.8657799959182739, 0.7618029713630676),
    float2(0.7648239731788635, 0.8086180090904236), float2(0.3283179998397827, 0.34629398584365845),
    float2(0.3868829905986786, 0.0961942970752716), float2(0.6354870200157166, 0.5113070011138916),
    float2(0.2108519971370697, 0.5334029793739319), float2(0.8951690196990967, 0.9441210031509399),
    float2(0.905239999294281, 0.47059500217437744), float2(0.2443619966506958, 0.9996340274810791),
    float2(0.6532179713249207, 0.06213570013642311), float2(0.18250100314617157, 0.14465799927711487),
    float2(0.9402449727058411, 0.113926000893116), float2(0.8352310061454773, 0.8336129784584045),
    float2(0.7318639755249023, 0.8900110125541687), float2(0.4682759940624237, 0.8510090112686157),
    float2(0.7259439826011658, 0.050416599959135056), float2(0.02999969944357872, 0.5138710141181946),
    float2(0.6426889896392822, 0.8466749787330627), float2(0.115665003657341, 0.6367689967155457),
    float2(0.8600119948387146, 0.31769800186157227), float2(0.651295006275177, 0.15012100338935852),
    float2(0.14667199552059174, 0.48329100012779236), float2(0.002594070043414831, 0.080690898001194),
    float2(0.2742699980735779, 0.9473559856414795), float2(0.8565629720687866, 0.884456992149353),
    float2(0.6153450012207031, 0.5258949995040894), float2(0.6161689758300781, 0.0650349035859108),
    float2(0.04464859887957573, 0.7540209889411926), float2(0.6215699911117554, 0.610614001750946),
    float2(0.9099090099334717, 0.12802499532699585), float2(0.38581499457359314, 0.7100440263748169),
    float2(0.9608139991760254, 0.8434709906578064), float2(0.2586140036582947, 0.17401699721813202),
    float2(0.3295390009880066, 0.0877406969666481), float2(0.3620719909667969, 0.010986699722707272),
    float2(0.5771049857139587, 0.7944579720497131), float2(0.9248939752578735, 0.6160770058631897),
    float2(0.35148200392723083, 0.871487021446228), float2(0.1452070027589798, 0.08713030070066452),
    float2(0.26370999217033386, 0.9180880188941956), float2(0.9304180145263672, 0.21790200471878052),
    float2(0.03839230164885521, 0.8409990072250366), float2(0.3638420104980469, 0.9659109711647034),
    float2(0.7118750214576721, 0.9094820022583008), float2(0.5462200045585632, 0.9036229848861694),
    float2(0.7722709774971008, 0.5484790205955505), float2(0.06863609701395035, 0.23969200253486633),
    float2(0.5107269883155823, 0.6626480221748352), float2(0.8228399753570557, 0.200873002409935),
    float2(0.8471019864082336, 0.7287819981575012), float2(0.8039489984512329, 0.3090910017490387),
    float2(0.16592900454998016, 0.9992070198059082), float2(0.017029300332069397, 0.258338987827301),
    float2(0.14404700696468353, 0.6911529898643494), float2(0.30634498596191406, 0.8825950026512146),
    float2(0.2857449948787689, 0.8237559795379639), float2(0.9004179835319519, 0.21585699915885925),
    float2(0.7908260226249695, 0.6521810293197632), float2(0.542618989944458, 0.09350869804620743),
    float2(0.9871519804000854, 0.40418699383735657), float2(0.586778998374939, 0.546159029006958),
    float2(0.0012207400286570191, 0.14838099479675293), float2(0.3886840045452118, 0.04126099869608879),
    float2(0.30332300066947937, 0.6541950106620789), float2(0.2299869954586029, 0.4130989909172058),
    float2(0.08926659822463989, 0.05551319941878319), float2(0.05078279972076416, 0.44187700748443604),
    float2(0.16595999896526337, 0.9257789850234985), float2(0.3691520094871521, 0.43934398889541626),
    float2(0.9942010045051575, 0.8421279788017273), float2(0.6016420125961304, 0.2378309965133667),
    float2(0.2598649859428406, 0.31934601068496704), float2(0.31193000078201294, 0.09430219978094101),
    float2(0.8503680229187012, 0.5842159986495972), float2(0.4444110095500946, 0.9432049989700317),
    float2(0.8410900235176086, 0.6138190031051636), float2(0.7526469826698303, 0.8688920140266418),
    float2(0.8423110246658325, 0.4768820106983185), float2(0.9987789988517761, 0.5566580295562744),
    float2(0.7288429737091064, 0.2999970018863678), float2(0.41907998919487, 0.51139897108078),
    float2(0.006530960090458393, 0.19009999930858612), float2(0.777184009552002, 0.8656880259513855),
    float2(0.5524160265922546, 0.8083130121231079), float2(0.9157080054283142, 0.2674950063228607),
    float2(0.2386849969625473, 0.9572740197181702), float2(0.17767900228500366, 0.6733300089836121),
    float2(0.4201180040836334, 0.7552419900894165), float2(0.6901149749755859, 0.3751029968261719),
    float2(0.6461989879608154, 0.48582398891448975), float2(0.14795400202274323, 0.8742030262947083),
    float2(0.45377999544143677, 0.5587630271911621), float2(0.8760949969291687, 0.7765129804611206),
    float2(0.09781180322170258, 0.4535660147666931), float2(0.288796991109848, 0.7829520106315613),
    float2(0.5181130170822144, 0.9272440075874329), float2(0.5347149968147278, 0.8046209812164307),
    float2(0.6116520166397095, 0.1325719952583313), float2(0.39588600397109985, 0.3677789866924286),
    float2(0.07458720356225967, 0.7817320227622986), float2(0.09790340065956116, 0.7313460111618042),
    float2(0.47846901416778564, 0.6591389775276184), float2(0.7460860013961792, 0.17899100482463837),
    float2(0.8828089833259583, 0.659932017326355), float2(0.5023350119590759, 0.5522930026054382),
    float2(0.6184269785881042, 0.745140016078949), float2(0.03329569846391678, 0.2152779996395111),
    float2(0.8204290270805359, 0.9392380118370056), float2(0.2187259942293167, 0.9186379909515381),
    float2(0.9653000235557556, 0.9029819965362549), float2(0.41251900792121887, 0.32721900939941406),
    float2(0.5757930278778076, 0.11654999852180481), float2(0.4983980059623718, 0.8133180141448975),
    float2(0.8005620241165161, 0.8046510219573975), float2(0.014648900367319584, 0.7161779999732971),
    float2(0.28116700053215027, 0.2526319921016693), float2(0.46018901467323303, 0.9919739961624146),
    float2(0.8584250211715698, 0.48796001076698303), float2(0.4055910110473633, 0.2269359976053238),
    float2(0.6199529767036438, 0.34458398818969727), float2(0.22595900297164917, 0.7304610013961792),
    float2(0.8765529990196228, 0.38453298807144165), float2(0.7404710054397583, 0.5567190051078796),
    float2(0.33588701486587524, 0.9378029704093933), float2(0.6446120142936707, 0.5851920247077942),
    float2(0.09021270275115967, 0.8339790105819702), float2(0.7720270156860352, 0.8380690217018127),
    float2(0.6318249702453613, 0.5486310124397278), float2(0.5929440259933472, 0.9261149764060974),
    float2(0.6869109869003296, 0.8998990058898926), float2(0.655538022518158, 0.8112429976463318),
    float2(0.5086519718170166, 0.12631599605083466), float2(0.732109010219574, 0.20297899842262268),
    float2(0.5872679948806763, 0.01049839984625578), float2(0.12387499958276749, 0.8373969793319702),
    float2(0.5549790263175964, 0.20761699974536896), float2(0.0631427988409996, 0.17551200091838837),
    float2(0.5942869782447815, 0.7750179767608643), float2(0.20181900262832642, 0.8092589974403381),
    float2(0.23673200607299805, 0.13409799337387085), float2(0.9876400232315063, 0.08883939683437347),
    float2(0.8188419938087463, 0.4270760118961334), float2(0.11001899838447571, 0.47312799096107483),
    float2(0.7257300019264221, 0.8386179804801941), float2(0.7529529929161072, 0.20108599960803986),
    float2(0.5498830080032349, 0.49897798895835876), float2(0.399152010679245, 0.7832580208778381),
    float2(0.44984298944473267, 0.3073819875717163), float2(0.3033849895000458, 0.014557300135493279),
    float2(0.4429759979248047, 0.3716540038585663), float2(0.4494459927082062, 0.8466749787330627),
};

#endif
// FO4RE_INLINE_INCLUDE_END shadow_poisson_kernel.hlsli
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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

#ifdef FILTER_POISSON
// FO4RE_INLINE_INCLUDE_BEGIN shadow_poisson_kernel.hlsli

#ifndef SHADOW_POISSON_KERNEL_HLSLI_INCLUDED
#define SHADOW_POISSON_KERNEL_HLSLI_INCLUDED

#define SHADOW_POISSON_KERNEL_SIZE 1000

static const float2 SHADOW_POISSON_KERNEL[SHADOW_POISSON_KERNEL_SIZE] =
{
    float2(0.4933930039405823, 0.3942689895629883), float2(0.7985470294952393, 0.8859220147132874),
    float2(0.2473219931125641, 0.9264500141143799), float2(0.051454201340675354, 0.14078199863433838),
    float2(0.8318430185317993, 0.009552289731800556), float2(0.428631991147995, 0.017151400446891785),
    float2(0.01565600000321865, 0.7497789859771729), float2(0.7583850026130676, 0.4961700141429901),
    float2(0.2234870046377182, 0.5621510148048401), float2(0.011627599596977234, 0.4069949984550476),
    float2(0.24146200716495514, 0.30463600158691406), float2(0.430310994386673, 0.7272260189056396),
    float2(0.981810986995697, 0.27835899591445923), float2(0.4070560038089752, 0.5005339980125427),
    float2(0.123478002846241, 0.4635460078716278), float2(0.8095340132713318, 0.6822720170021057),
    float2(0.6758019924163818, 0.6539199948310852), float2(0.23801399767398834, 0.06933809816837311),
    float2(0.0006714069750159979, 0.6111029982566833), float2(0.6218760013580322, 0.4990389943122864),
    float2(0.7128819823265076, 0.11529900133609772), float2(0.9136630296707153, 0.8193910121917725),
    float2(0.29545000195503235, 0.8096870183944702), float2(0.9850149750709534, 0.1178010031580925),
    float2(0.6307569742202759, 0.31321099400520325), float2(0.362621009349823, 0.1857050061225891),
    float2(0.16446399688720703, 0.7875909805297852), float2(0.0038453300949186087, 0.9388409852981567),
    float2(0.5227519869804382, 0.14627499878406525), float2(0.9875180125236511, 0.9389939904212952),
    float2(0.7701039910316467, 0.3155309855937958), float2(0.04483170062303543, 0.26883798837661743),
    float2(0.972320020198822, 0.43855100870132446), float2(0.6903589963912964, 0.9747310280799866),
    float2(0.5827199816703796, 0.8335520029067993), float2(0.49678000807762146, 0.9989929795265198),
    float2(0.498214989900589, 0.6034730076789856), float2(0.9164400100708008, 0.5907769799232483),
    float2(0.851131021976471, 0.21952000260353088), float2(0.4134649932384491, 0.893123984336853),
    float2(0.004425180144608021, 0.015686500817537308), float2(0.5808889865875244, 0.027405599132180214),
    float2(0.09085360169410706, 0.36497101187705994), float2(0.9074980020523071, 0.3878290057182312),
    float2(0.1073639988899231, 0.7465130090713501), float2(0.9870910048484802, 0.18369099497795105),
    float2(0.30414700508117676, 0.5427410006523132), float2(0.7691270112991333, 0.022675300016999245),
    float2(0.8954439759254456, 0.05838190019130707), float2(0.70967698097229, 0.20316199958324432),
    float2(0.4203920066356659, 0.4147160053253174), float2(0.8330940008163452, 0.15762799978256226),
    float2(0.29096299409866333, 0.19553199410438538), float2(0.4844200015068054, 0.9089329838752747),
    float2(0.7604910135269165, 0.9561449885368347), float2(0.03717150166630745, 0.5517749786376953),
    float2(0.14200299978256226, 0.19510500133037567), float2(0.9505599737167358, 0.7496259808540344),
    float2(0.36411601305007935, 0.7906429767608643), float2(0.22901099920272827, 0.8579360246658325),
    float2(0.7427290081977844, 0.7322310209274292), float2(0.7128509879112244, 0.3787960112094879),
    float2(0.34632399678230286, 0.4421829879283905), float2(0.4817650020122528, 0.22287699580192566),
    float2(0.6732990145683289, 0.5668200254440308), float2(0.0006408889894373715, 0.3230080008506775),
    float2(0.8751789927482605, 0.32813501358032227), float2(0.390514999628067, 0.3244419991970062),
    float2(0.9904170036315918, 0.6501359939575195), float2(0.35621199011802673, 0.9518420100212097),
    float2(0.43202000856399536, 0.796563982963562), float2(0.6701859831809998, 0.44901901483535767),
    float2(0.6072880029678345, 0.7214270234107971), float2(0.13770000636577606, 0.5319679975509644),
    float2(0.7076939940452576, 0.8413950204849243), float2(0.8066959977149963, 0.8207039833068848),
    float2(0.6437270045280457, 0.10141299664974213), float2(0.2517470121383667, 0.00012207399413455278),
    float2(0.5584890246391296, 0.4125489890575409), float2(0.5076450109481812, 0.00634784996509552),
    float2(0.8238170146942139, 0.40803200006484985), float2(0.3017059862613678, 0.35984399914741516),
    float2(0.30072900652885437, 0.6263920068740845), float2(0.9513229727745056, 0.5352029800415039),
    float2(0.11658100038766861, 0.8782010078430176), float2(0.3677479922771454, 0.0460829995572567),
    float2(0.2561720013618469, 0.7403180003166199), float2(0.16229699552059174, 0.9830009937286377),
    float2(0.5321210026741028, 0.4979709982872009), float2(0.20673200488090515, 0.23987500369548798),
    float2(0.10248100012540817, 0.6265760064125061), float2(0.34681200981140137, 0.6882839798927307),
    float2(0.903531014919281, 0.6722310185432434), float2(0.17407800257205963, 0.12005999684333801),
    float2(0.31723999977111816, 0.285317987203598), float2(0.09793390333652496, 0.9796140193939209),
    float2(0.19544099271297455, 0.38596799969673157), float2(0.11398699879646301, 0.300942987203598),
    float2(0.8307440280914307, 0.5856199860572815), float2(0.5628529787063599, 0.6620380282402039),
    float2(0.3735159933567047, 0.11416999995708466), float2(0.887935996055603, 0.9788510203361511),
    float2(0.9789119958877563, 0.8495740294456482), float2(0.5026400089263916, 0.06869719922542572),
    float2(0.1686760038137436, 0.05017239972949028), float2(0.8659319877624512, 0.5303509831428528),
    float2(0.9236429929733276, 0.1647389978170395), float2(0.04965360090136528, 0.2052370011806488),
    float2(0.8231760263442993, 0.0799890011548996), float2(0.024811500683426857, 0.6864219903945923),
    float2(0.8720660209655762, 0.871882975101471), float2(0.6138799786567688, 0.991362988948822),
    float2(0.09607230126857758, 0.0949430987238884), float2(0.8258919715881348, 0.2834559977054596),
    float2(0.18890999257564545, 0.4492020010948181), float2(0.6251720190048218, 0.9024930000305176),
    float2(0.5876340270042419, 0.5638599991798401), float2(0.012054800055921078, 0.4879299998283386),
    float2(0.3265480101108551, 0.8919640183448792), float2(0.9322789907455444, 0.8912010192871094),
    float2(0.49372801184654236, 0.6959750056266785), float2(0.6568499803543091, 0.780144989490509),
    float2(0.47056499123573303, 0.46147000789642334), float2(0.3726620078086853, 0.25141099095344543),
    float2(0.8736839890480042, 0.4529249966144562), float2(0.1747490018606186, 0.6543470025062561),
    float2(0.6952120065689087, 0.3021329939365387), float2(0.09561450034379959, 0.8131960034370422),
    float2(0.034150201827287674, 0.07660149782896042), float2(0.06680499762296677, 0.9160130023956299),
    float2(0.23401600122451782, 0.6280710101127625), float2(0.616595983505249, 0.376446008682251),
    float2(0.5639820098876953, 0.22901099920272827), float2(0.010223699733614922, 0.865444004535675),
    float2(0.41499099135398865, 0.6073489785194397), float2(0.631397008895874, 0.2421029955148697),
    float2(0.07110810279846191, 0.002563549904152751), float2(0.8778039813041687, 0.7384870052337646),
    float2(0.9971920251846313, 0.03616439923644066), float2(0.4361099898815155, 0.11423099786043167),
    float2(0.9591969847679138, 0.33796200156211853), float2(0.7051299810409546, 0.04675440117716789),
    float2(0.18008999526500702, 0.3244119882583618), float2(0.6102179884910583, 0.1621749997138977),
    float2(0.5471659898757935, 0.300942987203598), float2(0.18503400683403015, 0.9186990261077881),
    float2(0.4466080069541931, 0.9603869915008545), float2(0.954069972038269, 0.9948729872703552),
    float2(0.33497101068496704, 0.166935995221138), float2(0.3333840072154999, 0.21280600130558014),
    float2(0.4661700129508972, 0.5096290111541748), float2(0.36365899443626404, 0.34330299496650696),
    float2(0.17191100120544434, 0.17108699679374695), float2(0.759880006313324, 0.4608910083770752),
    float2(0.2912079989910126, 0.9834280014038086), float2(0.7586290240287781, 0.10745599865913391),
    float2(0.04507580026984215, 0.596759021282196), float2(0.9029819965362549, 0.5074009895324707),
    float2(0.5965149998664856, 0.7915890216827393), float2(0.17499299347400665, 0.24253100156784058),
    float2(0.4685809910297394, 0.5535449981689453), float2(0.8664510250091553, 0.6336860060691833),
    float2(0.6723840236663818, 0.06936860084533691), float2(0.23993700742721558, 0.18555299937725067),
    float2(0.6918849945068359, 0.7355570197105408), float2(0.6443979740142822, 0.7349770069122314),
    float2(0.41938498616218567, 0.558152973651886), float2(0.4960170090198517, 0.43458399176597595),
    float2(0.6652119755744934, 0.913815975189209), float2(0.2779630124568939, 0.765434980392456),
    float2(0.08548229932785034, 0.17151400446891785), float2(0.30582600831985474, 0.39497101306915283),
    float2(0.7080289721488953, 0.5744190216064453), float2(0.7287819981575012, 0.16055800020694733),
    float2(0.1861020028591156, 0.28904101252555847), float2(0.6779379844665527, 0.14361999928951263),
    float2(0.14032399654388428, 0.7077850103378296), float2(0.05908380076289177, 0.760308027267456),
    float2(0.6109809875488281, 0.4517959952354431), float2(0.03625600039958954, 0.007629630155861378),
    float2(0.9473249912261963, 0.4029659926891327), float2(0.19592900574207306, 0.6829130053520203),
    float2(0.057710498571395874, 0.46809300780296326), float2(0.5917540192604065, 0.2777490019798279),
    float2(0.7339699864387512, 0.6210520267486572), float2(0.9282199740409851, 0.7740709781646729),
    float2(0.8901939988136292, 0.18771900236606598), float2(0.740805983543396, 0.0805383026599884),
    float2(0.7708979845046997, 0.5667589902877808), float2(0.480087012052536, 0.13376300036907196),
    float2(0.33899998664855957, 0.016968300566077232), float2(0.8547930121421814, 0.8099920153617859),
    float2(0.5144199728965759, 0.2596510052680969), float2(0.1364479959011078, 0.6233410239219666),
    float2(0.3690600097179413, 0.5036159753799438), float2(0.33542901277542114, 0.6557819843292236),
    float2(0.5450000166893005, 0.6102790236473083), float2(0.9918820261955261, 0.7283239960670471),
    float2(0.27637600898742676, 0.8976100087165833), float2(0.6288340091705322, 0.9483630061149597),
    float2(0.5381019711494446, 0.7844169735908508), float2(0.06775110214948654, 0.057588398456573486),
    float2(0.09720149636268616, 0.035279400646686554), float2(0.4394359886646271, 0.6425979733467102),
    float2(0.2509230077266693, 0.3704639971256256), float2(0.45289498567581177, 0.17441299557685852),
    float2(0.20642699301242828, 0.7395550012588501), float2(0.4592120051383972, 0.3491320013999939),
    float2(0.8646810054779053, 0.020020099356770515), float2(0.6320689916610718, 0.18671199679374695),
    float2(0.792352020740509, 0.21115800738334656), float2(0.09134189784526825, 0.40156298875808716),
    float2(0.3082979917526245, 0.2425609976053238), float2(0.5730460286140442, 0.3318580090999603),
    float2(0.5030670166015625, 0.6413159966468811), float2(0.665058970451355, 0.8760030269622803),
    float2(0.2581869959831238, 0.5482649803161621), float2(0.873134970664978, 0.6994839906692505),
    float2(0.3422960042953491, 0.11078199744224548), float2(0.2691729962825775, 0.21857400238513947),
    float2(0.07446520030498505, 0.5483570098876953), float2(0.35157299041748047, 0.40360701084136963),
    float2(0.6602979898452759, 0.3413800001144409), float2(0.86285001039505, 0.09915459901094437),
    float2(0.20813600718975067, 0.952817976474762), float2(0.19370099902153015, 0.029023099690675735),
    float2(0.40882599353790283, 0.1530199944972992), float2(0.7104399800300598, 0.7962579727172852),
    float2(0.7826780080795288, 0.7365339994430542), float2(0.8307139873504639, 0.7723320126533508),
    float2(0.4948880076408386, 0.4970549941062927), float2(0.7148659825325012, 0.6980500221252441),
    float2(0.9317910075187683, 0.1991640031337738), float2(0.6378369927406311, 0.8089849948883057),
    float2(0.6657000184059143, 0.5995969772338867), float2(0.7531049847602844, 0.6726580262184143),
    float2(0.5937070250511169, 0.41236600279808044), float2(0.22888900339603424, 0.48976099491119385),
    float2(0.559099018573761, 0.10855399817228317), float2(0.6749169826507568, 0.3947260081768036),
    float2(0.7104399800300598, 0.46766600012779236), float2(0.10541100054979324, 0.22852300107479095),
    float2(0.5172579884529114, 0.10803599655628204), float2(0.9805899858474731, 0.546953022480011),
    float2(0.394665002822876, 0.8042240142822266), float2(0.8669999837875366, 0.16284699738025665),
    float2(0.8225650191307068, 0.6406139731407166), float2(0.8028500080108643, 0.25720998644828796),
    float2(0.44144999980926514, 0.2212589979171753), float2(0.959318995475769, 0.7075719833374023),
    float2(0.6282539963722229, 0.6852020025253296), float2(0.1322370022535324, 0.24506400525569916),
    float2(0.041200000792741776, 0.8709679841995239), float2(0.8260440230369568, 0.4974820017814636),
    float2(0.24625399708747864, 0.6735129952430725), float2(0.025544000789523125, 0.4378489851951599),
    float2(0.23813599348068237, 0.22241899371147156), float2(0.16843199729919434, 0.887050986289978),
    float2(0.2742390036582947, 0.33637499809265137), float2(0.35056599974632263, 0.8275700211524963),
    float2(0.20206299424171448, 0.5102999806404114), float2(0.3134250044822693, 0.9222080111503601),
    float2(0.05026400089263916, 0.7097989916801453), float2(0.36939600110054016, 0.5843070149421692),
    float2(0.07763910293579102, 0.4414199888706207), float2(0.4021419882774353, 0.7092499732971191),
    float2(0.2239139974117279, 0.7875909805297852), float2(0.9979860186576843, 0.9055449962615967),
    float2(0.4920800030231476, 0.7682120203971863), float2(0.4643999934196472, 0.04620499908924103),
    float2(0.5120700001716614, 0.9446390271186829), float2(0.6356390118598938, 0.6482740044593811),
    float2(0.8786889910697937, 0.25064900517463684), float2(0.27533799409866333, 0.1322370022535324),
    float2(0.8996549844741821, 0.7868279814720154), float2(0.30582600831985474, 0.7288129925727844),
    float2(0.9813230037689209, 0.8091070055961609), float2(0.32331299781799316, 0.7908869981765747),
    float2(0.5439310073852539, 0.3821530044078827), float2(0.2187259942293167, 0.6564840078353882),
    float2(0.5931580066680908, 0.6300240159034729), float2(0.14828899502754211, 0.2917569875717163),
    float2(0.9242839813232422, 0.038453299552202225), float2(0.5154579877853394, 0.3307900130748749),
    float2(0.16144299507141113, 0.0877406969666481), float2(0.9222999811172485, 0.5511339902877808),
    float2(0.07669299840927124, 0.32676199078559875), float2(0.1838739961385727, 0.5891600251197815),
    float2(0.4387040138244629, 0.387706995010376), float2(0.8076720237731934, 0.607990026473999),
    float2(0.9334390163421631, 0.9492779970169067), float2(0.7456279993057251, 0.28369998931884766),
    float2(0.8460950255393982, 0.9068880081176758), float2(0.8607749938964844, 0.06769009679555893),
    float2(0.5854060053825378, 0.1382180005311966), float2(0.8695639967918396, 0.9325540065765381),
    float2(0.7618640065193176, 0.4056209921836853), float2(0.3135170042514801, 0.6791890263557434),
    float2(0.5335549712181091, 0.05142369866371155), float2(0.20685400068759918, 0.11807599663734436),
    float2(0.38502201437950134, 0.7539600133895874), float2(0.4065069854259491, 0.9872130155563354),
    float2(0.7939079999923706, 0.09723199903964996), float2(0.5392929911613464, 0.993133008480072),
    float2(0.35935500264167786, 0.7271339893341064), float2(0.9175080060958862, 0.33622199296951294),
    float2(0.0383617989718914, 0.9344459772109985), float2(0.8032469749450684, 0.9429299831390381),
    float2(0.09891050308942795, 0.5130770206451416), float2(0.6351510286331177, 0.026551099494099617),
    float2(0.02212589979171753, 0.1808219999074936), float2(0.4687950015068054, 0.09451580047607422),
    float2(0.614031970500946, 0.5858640074729919), float2(0.47022899985313416, 0.31464600563049316),
    float2(0.707053005695343, 0.430525004863739), float2(0.03366189822554588, 0.7913450002670288),
    float2(0.44935500621795654, 0.8981900215148926), float2(0.8767359852790833, 0.2822049856185913),
    float2(0.17279599606990814, 0.5384079813957214), float2(0.25800299644470215, 0.4765770137310028),
    float2(0.5892509818077087, 0.8811910152435303), float2(0.34333300590515137, 0.5415509939193726),
    float2(0.25071001052856445, 0.4148379862308502), float2(0.27228599786758423, 0.8397780060768127),
    float2(0.5558339953422546, 0.9547110199928284), float2(0.013550199568271637, 0.6540729999542236),
    float2(0.3352150022983551, 0.6053959727287292), float2(0.10904300212860107, 0.5640429854393005),
    float2(0.12421000003814697, 0.16727200150489807), float2(0.3903929889202118, 0.9488199949264526),
    float2(0.8118230104446411, 0.03530989959836006), float2(0.665913999080658, 0.1921750009059906),
    float2(0.7866150140762329, 0.5288860201835632), float2(0.8380079865455627, 0.9941099882125854),
    float2(0.9034090042114258, 0.4408090114593506), float2(0.8189949989318848, 0.7336339950561523),
    float2(0.5269020199775696, 0.8884850144386292), float2(0.9153419733047485, 0.6300240159034729),
    float2(0.8071539998054504, 0.4390699863433838), float2(0.9585559964179993, 0.1530809998512268),
    float2(0.05508590117096901, 0.8280280232429504), float2(0.5516219735145569, 0.004120000172406435),
    float2(0.6189460158348083, 0.8395339846611023), float2(0.4622940123081207, 0.7839900255203247),
    float2(0.8262280225753784, 0.12442400306463242), float2(0.927702009677887, 0.09329509735107422),
    float2(0.5537580251693726, 0.26767799258232117), float2(0.03744620084762573, 0.352183997631073),
    float2(0.38016900420188904, 0.544327974319458), float2(0.8460339903831482, 0.6719570159912109),
    float2(0.7600020170211792, 0.2283090054988861), float2(0.4577470123767853, 0.9305700063705444),
    float2(0.839838981628418, 0.436598002910614), float2(0.2591629922389984, 0.8022710084915161),
    float2(0.9543439745903015, 0.9241920113563538), float2(0.7391279935836792, 0.03790400177240372),
    float2(0.195592999458313, 0.8325750231742859), float2(0.33365899324417114, 0.8562269806861877),
    float2(0.5727710127830505, 0.4569540023803711), float2(0.2591019868850708, 0.036316998302936554),
    float2(0.9794920086860657, 0.6032590270042419), float2(0.26157400012016296, 0.9602950215339661),
    float2(0.6637780070304871, 0.5244609713554382), float2(0.70100998878479, 0.882777988910675),
    float2(0.1817069947719574, 0.7600330114364624), float2(0.1148110032081604, 0.9175999760627747),
    float2(0.9108859896659851, 0.25141099095344543), float2(0.43717798590660095, 0.7649160027503967),
    float2(0.14874699711799622, 0.5772579908370972), float2(0.04138309881091118, 0.30802300572395325),
    float2(0.06421089917421341, 0.659017026424408), float2(0.06592000275850296, 0.9849849939346313),
    float2(0.9661549925804138, 0.022553199902176857), float2(0.2212589979171753, 0.4282050132751465),
    float2(0.9422590136528015, 0.29847100377082825), float2(0.5663629770278931, 0.526108980178833),
    float2(0.41761499643325806, 0.05960259959101677), float2(0.029572399333119392, 0.9823600053787231),
    float2(0.766319990158081, 0.9114660024642944), float2(0.00012207399413455278, 0.9023410081863403),
    float2(0.6978060007095337, 0.5262309908866882), float2(0.5988039970397949, 0.5335860252380371),
    float2(0.27808499336242676, 0.5724660158157349), float2(0.5938599705696106, 0.6913049817085266),
    float2(0.39493998885154724, 0.18863500654697418), float2(0.22229699790477753, 0.7057099938392639),
    float2(0.7985780239105225, 0.9938960075378418), float2(0.48066699504852295, 0.8187509775161743),
    float2(0.12372200191020966, 0.3670769929885864), float2(0.7100440263748169, 0.6486709713935852),
    float2(0.9430519938468933, 0.26413801312446594), float2(0.7608259916305542, 0.6033509969711304),
    float2(0.6530050039291382, 0.27854201197624207), float2(0.5258030295372009, 0.4099859893321991),
    float2(0.1334269940853119, 0.677174985408783), float2(0.7902460098266602, 0.2891629934310913),
    float2(0.256630003452301, 0.7044280171394348), float2(0.5891289710998535, 0.07733389735221863),
    float2(0.9811090230941772, 0.23142200708389282), float2(0.20831899344921112, 0.8974270224571228),
    float2(0.5684379935264587, 0.7473070025444031), float2(0.13040600717067719, 0.04840239882469177),
    float2(0.5238500237464905, 0.8460649847984314), float2(0.549485981464386, 0.6958829760551453),
    float2(0.3769649863243103, 0.42201000452041626), float2(0.4108709990978241, 0.6622520089149475),
    float2(0.2935880124568939, 0.055848900228738785), float2(0.4882049858570099, 0.965453028678894),
    float2(0.7264630198478699, 0.9537339806556702), float2(0.9837639927864075, 0.0706809014081955),
    float2(0.7990660071372986, 0.14807599782943726), float2(0.6926789879798889, 0.24274399876594543),
    float2(0.8107550144195557, 0.37058600783348083), float2(0.7613450288772583, 0.7735530138015747),
    float2(0.5843989849090576, 0.19711899757385254), float2(0.3822439908981323, 0.649586021900177),
    float2(0.5361189842224121, 0.5626090168952942), float2(0.1069369986653328, 0.7808769941329956),
    float2(0.04721210151910782, 0.5020599961280823), float2(0.7670519948005676, 0.25974300503730774),
    float2(0.21903100609779358, 0.269569993019104), float2(0.9999079704284668, 0.7610099911689758),
    float2(0.7401350140571594, 0.0054933298379182816), float2(0.9114959836006165, 0.0007019260083325207),
    float2(0.0054933298379182816, 0.11359000205993652), float2(0.746940016746521, 0.5364239811897278),
    float2(0.8309879899024963, 0.858119010925293), float2(0.9400010108947754, 0.46684199571609497),
    float2(0.46415600180625916, 0.7427589893341064), float2(0.3052160143852234, 0.11606200039386749),
    float2(0.2652060091495514, 0.6256899833679199), float2(0.3504140079021454, 0.7576829791069031),
    float2(0.5263530015945435, 0.22064900398254395), float2(0.43202000856399536, 0.835536003112793),
    float2(0.45939499139785767, 0.6797080039978027), float2(0.7512440085411072, 0.8253120183944702),
    float2(0.9889219999313354, 0.36979299783706665), float2(0.554764986038208, 0.9222080111503601),
    float2(0.13818800449371338, 0.33500200510025024), float2(0.06369210034608841, 0.6258739829063416),
    float2(0.7296370267868042, 0.9963380098342896), float2(0.8769490122795105, 0.5734729766845703),
    float2(0.30063799023628235, 0.15240900218486786), float2(0.41410601139068604, 0.27835899591445923),
    float2(0.4047060012817383, 0.4435249865055084), float2(0.5926390290260315, 0.9656059741973877),
    float2(0.16592900454998016, 0.39811399579048157), float2(0.40757501125335693, 0.3829460144042969),
    float2(0.14102600514888763, 0.9392679929733276), float2(0.31070899963378906, 0.44724899530410767),
    float2(0.9505910277366638, 0.8207039833068848), float2(0.185247004032135, 0.6207159757614136),
    float2(0.4216740131378174, 0.4699850082397461), float2(0.995727002620697, 0.4603720009326935),
    float2(0.6359750032424927, 0.06024349853396416), float2(0.6675620079040527, 0.7038180232048035),
    float2(0.9926760196685791, 0.50968998670578), float2(0.2269970029592514, 0.8265330195426941),
    float2(0.08923610299825668, 0.27405598759651184), float2(0.12225700169801712, 0.4274730086326599),
    float2(0.008758810348808765, 0.21646800637245178), float2(0.9691460132598877, 0.9648119807243347),
    float2(0.5273900032043457, 0.7440410256385803), float2(0.376446008682251, 0.46232500672340393),
    float2(0.2663959860801697, 0.27372100949287415), float2(0.7247229814529419, 0.5046539902687073),
    float2(0.33017998933792114, 0.0569474995136261), float2(0.10275600105524063, 0.7081519961357117),
    float2(0.2038940042257309, 0.07797479629516602), float2(0.27274399995803833, 0.09881889820098877),
    float2(0.8383129835128784, 0.5476850271224976), float2(0.13641799986362457, 0.0021057799458503723),
    float2(0.1447490006685257, 0.8574180006980896), float2(0.36631399393081665, 0.30069899559020996),
    float2(0.8690450191497803, 0.36930400133132935), float2(0.33710700273513794, 0.36228498816490173),
    float2(0.6480000019073486, 0.992247998714447), float2(0.15732300281524658, 0.4629659950733185),
    float2(0.2579120099544525, 0.5161899924278259), float2(0.756401002407074, 0.36332300305366516),
    float2(0.07925660163164139, 0.863277018070221), float2(0.49708500504493713, 0.872829020023346),
    float2(0.7334210276603699, 0.2482379972934723), float2(0.8385570049285889, 0.33674100041389465),
    float2(0.701865017414093, 0.6125680208206177), float2(0.8915070295333862, 0.1351660043001175),
    float2(0.9291669726371765, 0.8591570258140564), float2(0.16385400295257568, 0.8232979774475098),
    float2(0.29578500986099243, 0.8687090277671814), float2(0.14365099370479584, 0.1382489949464798),
    float2(0.6873679757118225, 0.00015259299834724516), float2(0.19272400438785553, 0.19836999475955963),
    float2(0.4761190116405487, 0.005981630180031061), float2(0.0029603000730276108, 0.05365150049328804),
    float2(0.37595799565315247, 0.8765529990196228), float2(0.48942500352859497, 0.17853300273418427),
    float2(0.44901901483535767, 0.2751240134239197), float2(0.05920590087771416, 0.10687600076198578),
    float2(0.21671199798583984, 0.33851099014282227), float2(0.4308910071849823, 0.3313089907169342),
    float2(0.3969849944114685, 0.018188999965786934), float2(0.7869200110435486, 0.34943699836730957),
    float2(0.4629659950733185, 0.5937380194664001), float2(0.32071900367736816, 0.48463401198387146),
    float2(0.5358740091323853, 0.18234799802303314), float2(0.5571150183677673, 0.07528919726610184),
    float2(0.3522450029850006, 0.9188820123672485), float2(0.5332189798355103, 0.5303199887275696),
    float2(0.004028439987450838, 0.5356910228729248), float2(0.10589899867773056, 0.14139199256896973),
    float2(0.3878290057182312, 0.83992999792099), float2(0.9102150201797485, 0.7370830178260803),
    float2(0.9266639947891235, 0.9795830249786377), float2(0.6080809831619263, 0.8122199773788452),
    float2(0.9649649858474731, 0.27631500363349915), float2(0.14288799464702606, 0.44889700412750244),
    float2(0.7782829999923706, 0.9297770261764526), float2(0.8665729761123657, 0.9796749949455261),
    float2(0.6211429834365845, 0.03671380132436752), float2(0.14835000038146973, 0.32108500599861145),
    float2(0.39762601256370544, 0.52531498670578), float2(0.09006009995937347, 0.532243013381958),
    float2(0.5182650089263916, 0.5578479766845703), float2(0.9172340035438538, 0.6475110054016113),
    float2(0.4321730136871338, 0.5689259767532349), float2(0.20355799794197083, 0.2852570116519928),
    float2(0.7240210175514221, 0.09759820252656937), float2(0.7398300170898438, 0.652974009513855),
    float2(0.026337500661611557, 0.46317899227142334), float2(0.3448899984359741, 0.5820180177688599),
    float2(0.6165649890899658, 0.2599869966506958), float2(0.8989840149879456, 0.29755499958992004),
    float2(0.6288949847221375, 0.8738669753074646), float2(0.2642289996147156, 0.7735530138015747),
    float2(0.27021101117134094, 0.05664239823818207), float2(0.25159499049186707, 0.26065900921821594),
    float2(0.4109010100364685, 0.3521530032157898), float2(0.2665790021419525, 0.8811910152435303),
    float2(0.8006229996681213, 0.5765249729156494), float2(0.04422130063176155, 0.9715870022773743),
    float2(0.7536550164222717, 0.7961670160293579), float2(0.9815670251846313, 0.6637780070304871),
    float2(0.6142150163650513, 0.19577600061893463), float2(0.9745169878005981, 0.46595698595046997),
    float2(0.7259439826011658, 0.7727289795875549), float2(0.10940899699926376, 0.18436199426651),
    float2(0.3935059905052185, 0.9672539830207825), float2(0.6308789849281311, 0.1587270051240921),
    float2(0.07803580164909363, 0.71144700050354), float2(0.2816550135612488, 0.35581499338150024),
    float2(0.6762599945068359, 0.12686499953269958), float2(0.5257419943809509, 0.029328299686312675),
    float2(0.6664940118789673, 0.3791919946670532), float2(0.09625539928674698, 0.3855710029602051),
    float2(0.9055150151252747, 0.9863280057907104), float2(0.794031023979187, 0.7696769833564758),
    float2(0.5939819812774658, 0.6646019816398621), float2(0.24237799644470215, 0.7307350039482117),
    float2(0.34241798520088196, 0.7878350019454956), float2(0.27280500531196594, 0.9724720120429993),
    float2(0.7841730117797852, 0.975737988948822), float2(0.08322399854660034, 0.7509689927101135),
    float2(0.1539350003004074, 0.9025539755821228), float2(0.5869929790496826, 0.2986850142478943),
    float2(0.6508380174636841, 0.46540701389312744), float2(0.9307839870452881, 0.41453298926353455),
    float2(0.04864649847149849, 0.1587270051240921), float2(0.2348400056362152, 0.9785450100898743),
    float2(0.9290140271186829, 0.01223789993673563), float2(0.8074589967727661, 0.3382669985294342),
    float2(0.279092013835907, 0.4622940123081207), float2(0.8978850245475769, 0.3205359876155853),
    float2(0.16229699552059174, 0.5076749920845032), float2(0.024140100926160812, 0.8540909886360168),
    float2(0.32322201132774353, 0.6404920220375061), float2(0.774528980255127, 0.44495999813079834),
    float2(0.583361029624939, 0.5878170132637024), float2(0.23020100593566895, 0.5282449722290039),
    float2(0.7177950143814087, 0.5284280180931091), float2(0.895779013633728, 0.9174169898033142),
    float2(0.05331579968333244, 0.08786279708147049), float2(0.6327099800109863, 0.2616350054740906),
    float2(0.7405930161476135, 0.10132099688053131), float2(0.7268900275230408, 0.81563800573349),
    float2(0.4935759902000427, 0.7398599982261658), float2(0.19110700488090515, 0.25623300671577454),
    float2(0.7097690105438232, 0.33460500836372375), float2(0.30463600158691406, 0.4738300144672394),
    float2(0.1699880063533783, 0.8445690274238586), float2(0.1622059941291809, 0.13837100565433502),
    float2(0.6375619769096375, 0.5273900032043457), float2(0.9686880111694336, 0.5222629904747009),
    float2(0.6437569856643677, 0.2951439917087555), float2(0.8762779831886292, 0.06378369778394699),
    float2(0.8490859866142273, 0.6306959986686707), float2(0.3152559995651245, 0.31974199414253235),
    float2(0.6399120092391968, 0.0016479999758303165), float2(0.7389140129089355, 0.8538159728050232),
    float2(0.834650993347168, 0.6953639984130859), float2(0.21842099726200104, 0.5859249830245972),
    float2(0.20313100516796112, 0.3199560046195984), float2(0.2605060040950775, 0.9350569844245911),
    float2(0.5032200217247009, 0.22409699857234955), float2(0.9806510210037231, 0.6869109869003296),
    float2(0.04492320120334625, 0.29142099618911743), float2(0.6318550109863281, 0.45756399631500244),
    float2(0.6268810033798218, 0.783715009689331), float2(0.4195989966392517, 0.6993619799613953),
    float2(0.0400707982480526, 0.055970899760723114), float2(0.03051850013434887, 0.9512010216712952),
    float2(0.9286779761314392, 0.929502010345459), float2(0.8272039890289307, 0.7477650046348572),
    float2(0.11105699837207794, 0.01834160089492798), float2(0.5337380170822144, 0.9626150131225586),
    float2(0.9068269729614258, 0.6102790236473083), float2(0.5822929739952087, 0.2507399916648865),
    float2(0.05746639892458916, 0.8055970072746277), float2(0.711355984210968, 0.3095490038394928),
    float2(0.3408310115337372, 0.9883419871330261), float2(0.8656880259513855, 0.1908629983663559),
    float2(0.28107500076293945, 0.8549759984016418), float2(0.673695981502533, 0.6842250227928162),
    float2(0.4851219952106476, 0.8502150177955627), float2(0.12683500349521637, 0.11697699874639511),
    float2(0.38239699602127075, 0.7724850177764893), float2(0.14078199863433838, 0.4656510055065155),
    float2(0.3996399939060211, 0.7349159717559814), float2(0.925961971282959, 0.6934720277786255),
    float2(0.003295999951660633, 0.4524979889392853), float2(0.018677299842238426, 0.05279700085520744),
    float2(0.31504300236701965, 0.6172059774398804), float2(0.9683520197868347, 0.2957240045070648),
    float2(0.1404460072517395, 0.21460600197315216), float2(0.8244580030441284, 0.9115880131721497),
    float2(0.6701560020446777, 0.2635580003261566), float2(0.9408860206604004, 0.07382430136203766),
    float2(0.5609909892082214, 0.855434000492096), float2(0.9088109731674194, 0.40531599521636963),
    float2(0.04318369925022125, 0.0306100994348526), float2(0.8865630030632019, 0.5467389822006226),
    float2(0.5602589845657349, 0.3935360014438629), float2(0.7865840196609497, 0.6956999897956848),
    float2(0.9813230037689209, 0.3108919858932495), float2(0.741690993309021, 0.807000994682312),
    float2(0.7927489876747131, 0.9555649757385254), float2(0.03796499967575073, 0.1091340035200119),
    float2(0.5427719950675964, 0.3266089856624603), float2(0.2568129897117615, 0.2299260050058365),
    float2(0.5290690064430237, 0.12536999583244324), float2(0.15723100304603577, 0.21924500167369843),
    float2(0.5113679766654968, 0.6803799867630005), float2(0.8819540143013, 0.08236949890851974),
    float2(0.7788019776344299, 0.3316139876842499), float2(0.8931549787521362, 0.23706799745559692),
    float2(0.9986569881439209, 0.2539139986038208), float2(0.5540030002593994, 0.3552660048007965),
    float2(0.3447679877281189, 0.32279399037361145), float2(0.06250189989805222, 0.5236979722976685),
    float2(0.48634299635887146, 0.7996150255203247), float2(0.09692680090665817, 0.8869900107383728),
    float2(0.10077200084924698, 0.3481859862804413), float2(0.30857300758361816, 0.3744010031223297),
    float2(0.11847300082445145, 0.6935030221939087), float2(0.27414798736572266, 0.17810599505901337),
    float2(0.10101599991321564, 0.6044800281524658), float2(0.44770699739456177, 0.8116400241851807),
    float2(0.6609389781951904, 0.8439589738845825), float2(0.8148750066757202, 0.7059850096702576),
    float2(0.19531799852848053, 0.06024349853396416), float2(0.17032399773597717, 0.3525800108909607),
    float2(0.14594000577926636, 0.2640460133552551), float2(0.6514790058135986, 0.8976100087165833),
    float2(0.5121009945869446, 0.04525899887084961), float2(0.9422889947891235, 0.6454660296440125),
    float2(0.8371229767799377, 0.566057026386261), float2(0.6933190226554871, 0.7099519968032837),
    float2(0.22318199276924133, 0.2987760007381439), float2(0.776665985584259, 0.991424024105072),
    float2(0.6462900042533875, 0.8787189722061157), float2(0.013794399797916412, 0.06796470284461975),
    float2(0.5755490064620972, 0.8907129764556885), float2(0.6829130053520203, 0.5961490273475647),
    float2(0.6779080033302307, 0.17645800113677979), float2(0.3838619887828827, 0.20413799583911896),
    float2(0.039155200123786926, 0.65385901927948), float2(0.7220979928970337, 0.9296240210533142),
    float2(0.4055599868297577, 0.6443679928779602), float2(0.646992027759552, 0.7561569809913635),
    float2(0.9165930151939392, 0.11093500256538391), float2(0.7798699736595154, 0.5862910151481628),
    float2(0.5788750052452087, 0.7039700150489807), float2(0.1622059941291809, 0.6864219903945923),
    float2(0.5453659892082214, 0.5935239791870117), float2(0.6008179783821106, 0.9816280007362366),
    float2(0.4918060004711151, 0.32804301381111145), float2(0.3344219923019409, 0.036591701209545135),
    float2(0.437483012676239, 0.4643389880657196), float2(0.11142300069332123, 0.2599259912967682),
    float2(0.8161569833755493, 0.5559859871864319), float2(0.5656300187110901, 0.5763729810714722),
    float2(0.4595780074596405, 0.6119269728660583), float2(0.7170630097389221, 0.06869719922542572),
    float2(0.8241519927978516, 0.5289160013198853), float2(0.09192179888486862, 0.2417680025100708),
    float2(0.07708980143070221, 0.3779410123825073), float2(0.8162180185317993, 0.9664300084114075),
    float2(0.07837150245904922, 0.5922729969024658), float2(0.10809700191020966, 0.06491290032863617),
    float2(0.664205014705658, 0.9528489708900452), float2(0.6889550089836121, 0.5806760191917419),
    float2(0.06735440343618393, 0.2725299894809723), float2(0.02386550046503544, 0.7331770062446594),
    float2(0.8414869904518127, 0.9492779970169067), float2(0.9583420157432556, 0.4991300106048584),
    float2(0.2952969968318939, 0.9075899720191956), float2(0.4260689914226532, 0.6551409959793091),
    float2(0.3744930028915405, 0.8037049770355225), float2(0.45243701338768005, 0.7263100147247314),
    float2(0.770684003829956, 0.6922510266304016), float2(0.2409130036830902, 0.5623040199279785),
    float2(0.8374890089035034, 0.1417890042066574), float2(0.9472950100898743, 0.6683549880981445),
    float2(0.6531879901885986, 0.5027620196342468), float2(0.7993410229682922, 0.4907679855823517),
    float2(0.044373899698257446, 0.9018830060958862), float2(0.23279500007629395, 0.08410900086164474),
    float2(0.039277300238609314, 0.24481900036334991), float2(0.6735739707946777, 0.050630200654268265),
    float2(0.42893800139427185, 0.6259040236473083), float2(0.8502150177955627, 0.02804649993777275),
    float2(0.13934800028800964, 0.9736009836196899), float2(0.70973801612854, 0.1386760026216507),
    float2(0.7105010151863098, 0.24466699361801147), float2(0.806207001209259, 0.4577470123767853),
    float2(0.3526720106601715, 0.48011699318885803), float2(0.019013000652194023, 0.5774409770965576),
    float2(0.30634498596191406, 0.7099519968032837), float2(0.6133000254631042, 0.8608049750328064),
    float2(0.870693027973175, 0.6712549924850464), float2(0.1938840001821518, 0.1082490012049675),
    float2(0.8637959957122803, 0.11880899965763092), float2(0.6128730177879333, 0.6519359946250916),
    float2(0.740776002407074, 0.4344309866428375), float2(0.7747730016708374, 0.8911100029945374),
    float2(0.9734179973602295, 0.8720660209655762), float2(0.29666998982429504, 0.07504499703645706),
    float2(0.6775410175323486, 0.7664719820022583), float2(0.7274090051651001, 0.737326979637146),
    float2(0.231330007314682, 0.1134679988026619), float2(0.8655660152435303, 0.5137490034103394),
    float2(0.02783289924263954, 0.39231500029563904), float2(0.676351010799408, 0.0865200012922287),
    float2(0.08197270333766937, 0.12448500096797943), float2(0.7660449743270874, 0.9399700164794922),
    float2(0.4135870039463043, 0.81392902135849), float2(0.38462498784065247, 0.9191870093345642),
    float2(0.9143959879875183, 0.8536940217018127), float2(0.8333079814910889, 0.8886989951133728),
    float2(0.5034639835357666, 0.5336470007896423), float2(0.9488199949264526, 0.04211549833416939),
    float2(0.8899199962615967, 0.51670902967453), float2(0.029145199805498123, 0.1991640031337738),
    float2(0.6961269974708557, 0.9537950158119202), float2(0.6809290051460266, 0.8413649797439575),
    float2(0.4759669899940491, 0.42362698912620544), float2(0.8825039863586426, 0.4031189978122711),
    float2(0.48719701170921326, 0.6223949790000916), float2(0.29154300689697266, 0.28327301144599915),
    float2(0.41770699620246887, 0.8735920190811157), float2(0.5697810053825378, 0.5599539875984192),
    float2(0.7090370059013367, 0.819940984249115), float2(0.9451889991760254, 0.7791069746017456),
    float2(0.20664100348949432, 0.559831976890564), float2(0.15451499819755554, 0.8005009889602661),
    float2(0.761559009552002, 0.583666980266571), float2(0.8436229825019836, 0.5150610208511353),
    float2(0.9064610004425049, 0.36847999691963196), float2(0.7637559771537781, 0.1731320023536682),
    float2(0.3397020101547241, 0.2404550015926361), float2(0.9735710024833679, 0.1291240006685257),
    float2(0.5914790034294128, 0.32502201199531555), float2(0.7144380211830139, 0.22290700674057007),
    float2(0.28156399726867676, 0.014984600245952606), float2(0.37586599588394165, 0.157383993268013),
    float2(0.8169500231742859, 0.06500440090894699), float2(0.5927299857139587, 0.043763499706983566),
    float2(0.33323198556900024, 0.9622179865837097), float2(0.8095030188560486, 0.11258299648761749),
    float2(0.8926969766616821, 0.8699300289154053), float2(0.27539899945259094, 0.6830959916114807),
    float2(0.6876429915428162, 0.7784050107002258), float2(0.04742579907178879, 0.6855679750442505),
    float2(0.8386790156364441, 0.7967159748077393), float2(0.003295999951660633, 0.960204005241394),
    float2(0.4747759997844696, 0.7012540102005005), float2(0.6989960074424744, 0.40763598680496216),
    float2(0.46705499291419983, 0.9523910284042358), float2(0.009064000099897385, 0.7844780087471008),
    float2(0.7901549935340881, 0.8426160216331482), float2(0.7426679730415344, 0.9260839819908142),
    float2(0.9539480209350586, 0.08786279708147049), float2(0.7841119766235352, 0.3865779936313629),
    float2(0.4679099917411804, 0.6360059976577759), float2(0.2775050103664398, 0.7463300228118896),
    float2(0.9962769746780396, 0.017731299623847008), float2(0.6936249732971191, 0.015411799773573875),
    float2(0.16180899739265442, 0.27713900804519653), float2(0.7480700016021729, 0.5717949867248535),
    float2(0.38032200932502747, 0.30930501222610474), float2(0.26319199800491333, 0.0722372978925705),
    float2(0.12341699749231339, 0.736503005027771), float2(0.29456499218940735, 0.09595020115375519),
    float2(0.40058600902557373, 0.26285600662231445), float2(0.7446519732475281, 0.020508399233222008),
    float2(0.2360610067844391, 0.4338510036468506), float2(0.5066990256309509, 0.3561820089817047),
    float2(0.7633900046348572, 0.7488020062446594), float2(0.18716999888420105, 0.5315709710121155),
    float2(0.5283359885215759, 0.6516619920730591), float2(0.875361979007721, 0.8960540294647217),
    float2(0.3034459948539734, 0.9632560014724731), float2(0.28626400232315063, 0.6138489842414856),
    float2(0.41810399293899536, 0.3053379952907562), float2(0.6449170112609863, 0.386821985244751),
    float2(0.47199898958206177, 0.2539750039577484), float2(0.9628890156745911, 0.5816829800605774),
    float2(0.4387950003147125, 0.18417899310588837), float2(0.6507459878921509, 0.6331980228424072),
    float2(0.8930330276489258, 0.038544900715351105), float2(0.8532059788703918, 0.003906370140612125),
    float2(0.39274299144744873, 0.16058799624443054), float2(0.20053699612617493, 0.4065679907798767),
    float2(0.38416698575019836, 0.6161990165710449), float2(0.676351010799408, 0.10593000054359436),
    float2(0.061189599335193634, 0.7376319766044617), float2(0.5238810181617737, 0.6342049837112427),
    float2(0.07486189901828766, 0.8802449703216553), float2(0.2795189917087555, 0.6369820237159729),
    float2(0.9775689840316772, 0.5641040205955505), float2(0.6579790115356445, 0.7241430282592773),
    float2(0.587602972984314, 0.43284401297569275), float2(0.1535390019416809, 0.42844900488853455),
    float2(0.5446940064430237, 0.4000059962272644), float2(0.7756890058517456, 0.48335200548171997),
    float2(0.22074000537395477, 0.04196299985051155), float2(0.08465830236673355, 0.2912989854812622),
    float2(0.23642699420452118, 0.7124850153923035), float2(0.00299080996774137, 0.349590003490448),
    float2(0.9074069857597351, 0.7080289721488953), float2(0.8657799959182739, 0.7618029713630676),
    float2(0.7648239731788635, 0.8086180090904236), float2(0.3283179998397827, 0.34629398584365845),
    float2(0.3868829905986786, 0.0961942970752716), float2(0.6354870200157166, 0.5113070011138916),
    float2(0.2108519971370697, 0.5334029793739319), float2(0.8951690196990967, 0.9441210031509399),
    float2(0.905239999294281, 0.47059500217437744), float2(0.2443619966506958, 0.9996340274810791),
    float2(0.6532179713249207, 0.06213570013642311), float2(0.18250100314617157, 0.14465799927711487),
    float2(0.9402449727058411, 0.113926000893116), float2(0.8352310061454773, 0.8336129784584045),
    float2(0.7318639755249023, 0.8900110125541687), float2(0.4682759940624237, 0.8510090112686157),
    float2(0.7259439826011658, 0.050416599959135056), float2(0.02999969944357872, 0.5138710141181946),
    float2(0.6426889896392822, 0.8466749787330627), float2(0.115665003657341, 0.6367689967155457),
    float2(0.8600119948387146, 0.31769800186157227), float2(0.651295006275177, 0.15012100338935852),
    float2(0.14667199552059174, 0.48329100012779236), float2(0.002594070043414831, 0.080690898001194),
    float2(0.2742699980735779, 0.9473559856414795), float2(0.8565629720687866, 0.884456992149353),
    float2(0.6153450012207031, 0.5258949995040894), float2(0.6161689758300781, 0.0650349035859108),
    float2(0.04464859887957573, 0.7540209889411926), float2(0.6215699911117554, 0.610614001750946),
    float2(0.9099090099334717, 0.12802499532699585), float2(0.38581499457359314, 0.7100440263748169),
    float2(0.9608139991760254, 0.8434709906578064), float2(0.2586140036582947, 0.17401699721813202),
    float2(0.3295390009880066, 0.0877406969666481), float2(0.3620719909667969, 0.010986699722707272),
    float2(0.5771049857139587, 0.7944579720497131), float2(0.9248939752578735, 0.6160770058631897),
    float2(0.35148200392723083, 0.871487021446228), float2(0.1452070027589798, 0.08713030070066452),
    float2(0.26370999217033386, 0.9180880188941956), float2(0.9304180145263672, 0.21790200471878052),
    float2(0.03839230164885521, 0.8409990072250366), float2(0.3638420104980469, 0.9659109711647034),
    float2(0.7118750214576721, 0.9094820022583008), float2(0.5462200045585632, 0.9036229848861694),
    float2(0.7722709774971008, 0.5484790205955505), float2(0.06863609701395035, 0.23969200253486633),
    float2(0.5107269883155823, 0.6626480221748352), float2(0.8228399753570557, 0.200873002409935),
    float2(0.8471019864082336, 0.7287819981575012), float2(0.8039489984512329, 0.3090910017490387),
    float2(0.16592900454998016, 0.9992070198059082), float2(0.017029300332069397, 0.258338987827301),
    float2(0.14404700696468353, 0.6911529898643494), float2(0.30634498596191406, 0.8825950026512146),
    float2(0.2857449948787689, 0.8237559795379639), float2(0.9004179835319519, 0.21585699915885925),
    float2(0.7908260226249695, 0.6521810293197632), float2(0.542618989944458, 0.09350869804620743),
    float2(0.9871519804000854, 0.40418699383735657), float2(0.586778998374939, 0.546159029006958),
    float2(0.0012207400286570191, 0.14838099479675293), float2(0.3886840045452118, 0.04126099869608879),
    float2(0.30332300066947937, 0.6541950106620789), float2(0.2299869954586029, 0.4130989909172058),
    float2(0.08926659822463989, 0.05551319941878319), float2(0.05078279972076416, 0.44187700748443604),
    float2(0.16595999896526337, 0.9257789850234985), float2(0.3691520094871521, 0.43934398889541626),
    float2(0.9942010045051575, 0.8421279788017273), float2(0.6016420125961304, 0.2378309965133667),
    float2(0.2598649859428406, 0.31934601068496704), float2(0.31193000078201294, 0.09430219978094101),
    float2(0.8503680229187012, 0.5842159986495972), float2(0.4444110095500946, 0.9432049989700317),
    float2(0.8410900235176086, 0.6138190031051636), float2(0.7526469826698303, 0.8688920140266418),
    float2(0.8423110246658325, 0.4768820106983185), float2(0.9987789988517761, 0.5566580295562744),
    float2(0.7288429737091064, 0.2999970018863678), float2(0.41907998919487, 0.51139897108078),
    float2(0.006530960090458393, 0.19009999930858612), float2(0.777184009552002, 0.8656880259513855),
    float2(0.5524160265922546, 0.8083130121231079), float2(0.9157080054283142, 0.2674950063228607),
    float2(0.2386849969625473, 0.9572740197181702), float2(0.17767900228500366, 0.6733300089836121),
    float2(0.4201180040836334, 0.7552419900894165), float2(0.6901149749755859, 0.3751029968261719),
    float2(0.6461989879608154, 0.48582398891448975), float2(0.14795400202274323, 0.8742030262947083),
    float2(0.45377999544143677, 0.5587630271911621), float2(0.8760949969291687, 0.7765129804611206),
    float2(0.09781180322170258, 0.4535660147666931), float2(0.288796991109848, 0.7829520106315613),
    float2(0.5181130170822144, 0.9272440075874329), float2(0.5347149968147278, 0.8046209812164307),
    float2(0.6116520166397095, 0.1325719952583313), float2(0.39588600397109985, 0.3677789866924286),
    float2(0.07458720356225967, 0.7817320227622986), float2(0.09790340065956116, 0.7313460111618042),
    float2(0.47846901416778564, 0.6591389775276184), float2(0.7460860013961792, 0.17899100482463837),
    float2(0.8828089833259583, 0.659932017326355), float2(0.5023350119590759, 0.5522930026054382),
    float2(0.6184269785881042, 0.745140016078949), float2(0.03329569846391678, 0.2152779996395111),
    float2(0.8204290270805359, 0.9392380118370056), float2(0.2187259942293167, 0.9186379909515381),
    float2(0.9653000235557556, 0.9029819965362549), float2(0.41251900792121887, 0.32721900939941406),
    float2(0.5757930278778076, 0.11654999852180481), float2(0.4983980059623718, 0.8133180141448975),
    float2(0.8005620241165161, 0.8046510219573975), float2(0.014648900367319584, 0.7161779999732971),
    float2(0.28116700053215027, 0.2526319921016693), float2(0.46018901467323303, 0.9919739961624146),
    float2(0.8584250211715698, 0.48796001076698303), float2(0.4055910110473633, 0.2269359976053238),
    float2(0.6199529767036438, 0.34458398818969727), float2(0.22595900297164917, 0.7304610013961792),
    float2(0.8765529990196228, 0.38453298807144165), float2(0.7404710054397583, 0.5567190051078796),
    float2(0.33588701486587524, 0.9378029704093933), float2(0.6446120142936707, 0.5851920247077942),
    float2(0.09021270275115967, 0.8339790105819702), float2(0.7720270156860352, 0.8380690217018127),
    float2(0.6318249702453613, 0.5486310124397278), float2(0.5929440259933472, 0.9261149764060974),
    float2(0.6869109869003296, 0.8998990058898926), float2(0.655538022518158, 0.8112429976463318),
    float2(0.5086519718170166, 0.12631599605083466), float2(0.732109010219574, 0.20297899842262268),
    float2(0.5872679948806763, 0.01049839984625578), float2(0.12387499958276749, 0.8373969793319702),
    float2(0.5549790263175964, 0.20761699974536896), float2(0.0631427988409996, 0.17551200091838837),
    float2(0.5942869782447815, 0.7750179767608643), float2(0.20181900262832642, 0.8092589974403381),
    float2(0.23673200607299805, 0.13409799337387085), float2(0.9876400232315063, 0.08883939683437347),
    float2(0.8188419938087463, 0.4270760118961334), float2(0.11001899838447571, 0.47312799096107483),
    float2(0.7257300019264221, 0.8386179804801941), float2(0.7529529929161072, 0.20108599960803986),
    float2(0.5498830080032349, 0.49897798895835876), float2(0.399152010679245, 0.7832580208778381),
    float2(0.44984298944473267, 0.3073819875717163), float2(0.3033849895000458, 0.014557300135493279),
    float2(0.4429759979248047, 0.3716540038585663), float2(0.4494459927082062, 0.8466749787330627),
};

#endif
// FO4RE_INLINE_INCLUDE_END shadow_poisson_kernel.hlsli
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

    float centreDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float centreLit = (centreDepth >= shadowZ) ? 1.0 : 0.0;

    float averageBlocker = blocker.x / blocker.y;
    float worldRange     = cascadeScale.w - cascadeScale.z;
    float receiverWorld  = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld   = worldRange * averageBlocker + cascadeScale.z;
    float separation     = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra       = (blockerWorld < cascadeScale.z + 0.001)
        ? 1.9
        : (separation * 1.8 + 0.1);

    float sum = centreLit;
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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
// FO4RE_INLINE_INCLUDE_BEGIN shadow_poisson_kernel.hlsli

#ifndef SHADOW_POISSON_KERNEL_HLSLI_INCLUDED
#define SHADOW_POISSON_KERNEL_HLSLI_INCLUDED

#define SHADOW_POISSON_KERNEL_SIZE 1000

static const float2 SHADOW_POISSON_KERNEL[SHADOW_POISSON_KERNEL_SIZE] =
{
    float2(0.4933930039405823, 0.3942689895629883), float2(0.7985470294952393, 0.8859220147132874),
    float2(0.2473219931125641, 0.9264500141143799), float2(0.051454201340675354, 0.14078199863433838),
    float2(0.8318430185317993, 0.009552289731800556), float2(0.428631991147995, 0.017151400446891785),
    float2(0.01565600000321865, 0.7497789859771729), float2(0.7583850026130676, 0.4961700141429901),
    float2(0.2234870046377182, 0.5621510148048401), float2(0.011627599596977234, 0.4069949984550476),
    float2(0.24146200716495514, 0.30463600158691406), float2(0.430310994386673, 0.7272260189056396),
    float2(0.981810986995697, 0.27835899591445923), float2(0.4070560038089752, 0.5005339980125427),
    float2(0.123478002846241, 0.4635460078716278), float2(0.8095340132713318, 0.6822720170021057),
    float2(0.6758019924163818, 0.6539199948310852), float2(0.23801399767398834, 0.06933809816837311),
    float2(0.0006714069750159979, 0.6111029982566833), float2(0.6218760013580322, 0.4990389943122864),
    float2(0.7128819823265076, 0.11529900133609772), float2(0.9136630296707153, 0.8193910121917725),
    float2(0.29545000195503235, 0.8096870183944702), float2(0.9850149750709534, 0.1178010031580925),
    float2(0.6307569742202759, 0.31321099400520325), float2(0.362621009349823, 0.1857050061225891),
    float2(0.16446399688720703, 0.7875909805297852), float2(0.0038453300949186087, 0.9388409852981567),
    float2(0.5227519869804382, 0.14627499878406525), float2(0.9875180125236511, 0.9389939904212952),
    float2(0.7701039910316467, 0.3155309855937958), float2(0.04483170062303543, 0.26883798837661743),
    float2(0.972320020198822, 0.43855100870132446), float2(0.6903589963912964, 0.9747310280799866),
    float2(0.5827199816703796, 0.8335520029067993), float2(0.49678000807762146, 0.9989929795265198),
    float2(0.498214989900589, 0.6034730076789856), float2(0.9164400100708008, 0.5907769799232483),
    float2(0.851131021976471, 0.21952000260353088), float2(0.4134649932384491, 0.893123984336853),
    float2(0.004425180144608021, 0.015686500817537308), float2(0.5808889865875244, 0.027405599132180214),
    float2(0.09085360169410706, 0.36497101187705994), float2(0.9074980020523071, 0.3878290057182312),
    float2(0.1073639988899231, 0.7465130090713501), float2(0.9870910048484802, 0.18369099497795105),
    float2(0.30414700508117676, 0.5427410006523132), float2(0.7691270112991333, 0.022675300016999245),
    float2(0.8954439759254456, 0.05838190019130707), float2(0.70967698097229, 0.20316199958324432),
    float2(0.4203920066356659, 0.4147160053253174), float2(0.8330940008163452, 0.15762799978256226),
    float2(0.29096299409866333, 0.19553199410438538), float2(0.4844200015068054, 0.9089329838752747),
    float2(0.7604910135269165, 0.9561449885368347), float2(0.03717150166630745, 0.5517749786376953),
    float2(0.14200299978256226, 0.19510500133037567), float2(0.9505599737167358, 0.7496259808540344),
    float2(0.36411601305007935, 0.7906429767608643), float2(0.22901099920272827, 0.8579360246658325),
    float2(0.7427290081977844, 0.7322310209274292), float2(0.7128509879112244, 0.3787960112094879),
    float2(0.34632399678230286, 0.4421829879283905), float2(0.4817650020122528, 0.22287699580192566),
    float2(0.6732990145683289, 0.5668200254440308), float2(0.0006408889894373715, 0.3230080008506775),
    float2(0.8751789927482605, 0.32813501358032227), float2(0.390514999628067, 0.3244419991970062),
    float2(0.9904170036315918, 0.6501359939575195), float2(0.35621199011802673, 0.9518420100212097),
    float2(0.43202000856399536, 0.796563982963562), float2(0.6701859831809998, 0.44901901483535767),
    float2(0.6072880029678345, 0.7214270234107971), float2(0.13770000636577606, 0.5319679975509644),
    float2(0.7076939940452576, 0.8413950204849243), float2(0.8066959977149963, 0.8207039833068848),
    float2(0.6437270045280457, 0.10141299664974213), float2(0.2517470121383667, 0.00012207399413455278),
    float2(0.5584890246391296, 0.4125489890575409), float2(0.5076450109481812, 0.00634784996509552),
    float2(0.8238170146942139, 0.40803200006484985), float2(0.3017059862613678, 0.35984399914741516),
    float2(0.30072900652885437, 0.6263920068740845), float2(0.9513229727745056, 0.5352029800415039),
    float2(0.11658100038766861, 0.8782010078430176), float2(0.3677479922771454, 0.0460829995572567),
    float2(0.2561720013618469, 0.7403180003166199), float2(0.16229699552059174, 0.9830009937286377),
    float2(0.5321210026741028, 0.4979709982872009), float2(0.20673200488090515, 0.23987500369548798),
    float2(0.10248100012540817, 0.6265760064125061), float2(0.34681200981140137, 0.6882839798927307),
    float2(0.903531014919281, 0.6722310185432434), float2(0.17407800257205963, 0.12005999684333801),
    float2(0.31723999977111816, 0.285317987203598), float2(0.09793390333652496, 0.9796140193939209),
    float2(0.19544099271297455, 0.38596799969673157), float2(0.11398699879646301, 0.300942987203598),
    float2(0.8307440280914307, 0.5856199860572815), float2(0.5628529787063599, 0.6620380282402039),
    float2(0.3735159933567047, 0.11416999995708466), float2(0.887935996055603, 0.9788510203361511),
    float2(0.9789119958877563, 0.8495740294456482), float2(0.5026400089263916, 0.06869719922542572),
    float2(0.1686760038137436, 0.05017239972949028), float2(0.8659319877624512, 0.5303509831428528),
    float2(0.9236429929733276, 0.1647389978170395), float2(0.04965360090136528, 0.2052370011806488),
    float2(0.8231760263442993, 0.0799890011548996), float2(0.024811500683426857, 0.6864219903945923),
    float2(0.8720660209655762, 0.871882975101471), float2(0.6138799786567688, 0.991362988948822),
    float2(0.09607230126857758, 0.0949430987238884), float2(0.8258919715881348, 0.2834559977054596),
    float2(0.18890999257564545, 0.4492020010948181), float2(0.6251720190048218, 0.9024930000305176),
    float2(0.5876340270042419, 0.5638599991798401), float2(0.012054800055921078, 0.4879299998283386),
    float2(0.3265480101108551, 0.8919640183448792), float2(0.9322789907455444, 0.8912010192871094),
    float2(0.49372801184654236, 0.6959750056266785), float2(0.6568499803543091, 0.780144989490509),
    float2(0.47056499123573303, 0.46147000789642334), float2(0.3726620078086853, 0.25141099095344543),
    float2(0.8736839890480042, 0.4529249966144562), float2(0.1747490018606186, 0.6543470025062561),
    float2(0.6952120065689087, 0.3021329939365387), float2(0.09561450034379959, 0.8131960034370422),
    float2(0.034150201827287674, 0.07660149782896042), float2(0.06680499762296677, 0.9160130023956299),
    float2(0.23401600122451782, 0.6280710101127625), float2(0.616595983505249, 0.376446008682251),
    float2(0.5639820098876953, 0.22901099920272827), float2(0.010223699733614922, 0.865444004535675),
    float2(0.41499099135398865, 0.6073489785194397), float2(0.631397008895874, 0.2421029955148697),
    float2(0.07110810279846191, 0.002563549904152751), float2(0.8778039813041687, 0.7384870052337646),
    float2(0.9971920251846313, 0.03616439923644066), float2(0.4361099898815155, 0.11423099786043167),
    float2(0.9591969847679138, 0.33796200156211853), float2(0.7051299810409546, 0.04675440117716789),
    float2(0.18008999526500702, 0.3244119882583618), float2(0.6102179884910583, 0.1621749997138977),
    float2(0.5471659898757935, 0.300942987203598), float2(0.18503400683403015, 0.9186990261077881),
    float2(0.4466080069541931, 0.9603869915008545), float2(0.954069972038269, 0.9948729872703552),
    float2(0.33497101068496704, 0.166935995221138), float2(0.3333840072154999, 0.21280600130558014),
    float2(0.4661700129508972, 0.5096290111541748), float2(0.36365899443626404, 0.34330299496650696),
    float2(0.17191100120544434, 0.17108699679374695), float2(0.759880006313324, 0.4608910083770752),
    float2(0.2912079989910126, 0.9834280014038086), float2(0.7586290240287781, 0.10745599865913391),
    float2(0.04507580026984215, 0.596759021282196), float2(0.9029819965362549, 0.5074009895324707),
    float2(0.5965149998664856, 0.7915890216827393), float2(0.17499299347400665, 0.24253100156784058),
    float2(0.4685809910297394, 0.5535449981689453), float2(0.8664510250091553, 0.6336860060691833),
    float2(0.6723840236663818, 0.06936860084533691), float2(0.23993700742721558, 0.18555299937725067),
    float2(0.6918849945068359, 0.7355570197105408), float2(0.6443979740142822, 0.7349770069122314),
    float2(0.41938498616218567, 0.558152973651886), float2(0.4960170090198517, 0.43458399176597595),
    float2(0.6652119755744934, 0.913815975189209), float2(0.2779630124568939, 0.765434980392456),
    float2(0.08548229932785034, 0.17151400446891785), float2(0.30582600831985474, 0.39497101306915283),
    float2(0.7080289721488953, 0.5744190216064453), float2(0.7287819981575012, 0.16055800020694733),
    float2(0.1861020028591156, 0.28904101252555847), float2(0.6779379844665527, 0.14361999928951263),
    float2(0.14032399654388428, 0.7077850103378296), float2(0.05908380076289177, 0.760308027267456),
    float2(0.6109809875488281, 0.4517959952354431), float2(0.03625600039958954, 0.007629630155861378),
    float2(0.9473249912261963, 0.4029659926891327), float2(0.19592900574207306, 0.6829130053520203),
    float2(0.057710498571395874, 0.46809300780296326), float2(0.5917540192604065, 0.2777490019798279),
    float2(0.7339699864387512, 0.6210520267486572), float2(0.9282199740409851, 0.7740709781646729),
    float2(0.8901939988136292, 0.18771900236606598), float2(0.740805983543396, 0.0805383026599884),
    float2(0.7708979845046997, 0.5667589902877808), float2(0.480087012052536, 0.13376300036907196),
    float2(0.33899998664855957, 0.016968300566077232), float2(0.8547930121421814, 0.8099920153617859),
    float2(0.5144199728965759, 0.2596510052680969), float2(0.1364479959011078, 0.6233410239219666),
    float2(0.3690600097179413, 0.5036159753799438), float2(0.33542901277542114, 0.6557819843292236),
    float2(0.5450000166893005, 0.6102790236473083), float2(0.9918820261955261, 0.7283239960670471),
    float2(0.27637600898742676, 0.8976100087165833), float2(0.6288340091705322, 0.9483630061149597),
    float2(0.5381019711494446, 0.7844169735908508), float2(0.06775110214948654, 0.057588398456573486),
    float2(0.09720149636268616, 0.035279400646686554), float2(0.4394359886646271, 0.6425979733467102),
    float2(0.2509230077266693, 0.3704639971256256), float2(0.45289498567581177, 0.17441299557685852),
    float2(0.20642699301242828, 0.7395550012588501), float2(0.4592120051383972, 0.3491320013999939),
    float2(0.8646810054779053, 0.020020099356770515), float2(0.6320689916610718, 0.18671199679374695),
    float2(0.792352020740509, 0.21115800738334656), float2(0.09134189784526825, 0.40156298875808716),
    float2(0.3082979917526245, 0.2425609976053238), float2(0.5730460286140442, 0.3318580090999603),
    float2(0.5030670166015625, 0.6413159966468811), float2(0.665058970451355, 0.8760030269622803),
    float2(0.2581869959831238, 0.5482649803161621), float2(0.873134970664978, 0.6994839906692505),
    float2(0.3422960042953491, 0.11078199744224548), float2(0.2691729962825775, 0.21857400238513947),
    float2(0.07446520030498505, 0.5483570098876953), float2(0.35157299041748047, 0.40360701084136963),
    float2(0.6602979898452759, 0.3413800001144409), float2(0.86285001039505, 0.09915459901094437),
    float2(0.20813600718975067, 0.952817976474762), float2(0.19370099902153015, 0.029023099690675735),
    float2(0.40882599353790283, 0.1530199944972992), float2(0.7104399800300598, 0.7962579727172852),
    float2(0.7826780080795288, 0.7365339994430542), float2(0.8307139873504639, 0.7723320126533508),
    float2(0.4948880076408386, 0.4970549941062927), float2(0.7148659825325012, 0.6980500221252441),
    float2(0.9317910075187683, 0.1991640031337738), float2(0.6378369927406311, 0.8089849948883057),
    float2(0.6657000184059143, 0.5995969772338867), float2(0.7531049847602844, 0.6726580262184143),
    float2(0.5937070250511169, 0.41236600279808044), float2(0.22888900339603424, 0.48976099491119385),
    float2(0.559099018573761, 0.10855399817228317), float2(0.6749169826507568, 0.3947260081768036),
    float2(0.7104399800300598, 0.46766600012779236), float2(0.10541100054979324, 0.22852300107479095),
    float2(0.5172579884529114, 0.10803599655628204), float2(0.9805899858474731, 0.546953022480011),
    float2(0.394665002822876, 0.8042240142822266), float2(0.8669999837875366, 0.16284699738025665),
    float2(0.8225650191307068, 0.6406139731407166), float2(0.8028500080108643, 0.25720998644828796),
    float2(0.44144999980926514, 0.2212589979171753), float2(0.959318995475769, 0.7075719833374023),
    float2(0.6282539963722229, 0.6852020025253296), float2(0.1322370022535324, 0.24506400525569916),
    float2(0.041200000792741776, 0.8709679841995239), float2(0.8260440230369568, 0.4974820017814636),
    float2(0.24625399708747864, 0.6735129952430725), float2(0.025544000789523125, 0.4378489851951599),
    float2(0.23813599348068237, 0.22241899371147156), float2(0.16843199729919434, 0.887050986289978),
    float2(0.2742390036582947, 0.33637499809265137), float2(0.35056599974632263, 0.8275700211524963),
    float2(0.20206299424171448, 0.5102999806404114), float2(0.3134250044822693, 0.9222080111503601),
    float2(0.05026400089263916, 0.7097989916801453), float2(0.36939600110054016, 0.5843070149421692),
    float2(0.07763910293579102, 0.4414199888706207), float2(0.4021419882774353, 0.7092499732971191),
    float2(0.2239139974117279, 0.7875909805297852), float2(0.9979860186576843, 0.9055449962615967),
    float2(0.4920800030231476, 0.7682120203971863), float2(0.4643999934196472, 0.04620499908924103),
    float2(0.5120700001716614, 0.9446390271186829), float2(0.6356390118598938, 0.6482740044593811),
    float2(0.8786889910697937, 0.25064900517463684), float2(0.27533799409866333, 0.1322370022535324),
    float2(0.8996549844741821, 0.7868279814720154), float2(0.30582600831985474, 0.7288129925727844),
    float2(0.9813230037689209, 0.8091070055961609), float2(0.32331299781799316, 0.7908869981765747),
    float2(0.5439310073852539, 0.3821530044078827), float2(0.2187259942293167, 0.6564840078353882),
    float2(0.5931580066680908, 0.6300240159034729), float2(0.14828899502754211, 0.2917569875717163),
    float2(0.9242839813232422, 0.038453299552202225), float2(0.5154579877853394, 0.3307900130748749),
    float2(0.16144299507141113, 0.0877406969666481), float2(0.9222999811172485, 0.5511339902877808),
    float2(0.07669299840927124, 0.32676199078559875), float2(0.1838739961385727, 0.5891600251197815),
    float2(0.4387040138244629, 0.387706995010376), float2(0.8076720237731934, 0.607990026473999),
    float2(0.9334390163421631, 0.9492779970169067), float2(0.7456279993057251, 0.28369998931884766),
    float2(0.8460950255393982, 0.9068880081176758), float2(0.8607749938964844, 0.06769009679555893),
    float2(0.5854060053825378, 0.1382180005311966), float2(0.8695639967918396, 0.9325540065765381),
    float2(0.7618640065193176, 0.4056209921836853), float2(0.3135170042514801, 0.6791890263557434),
    float2(0.5335549712181091, 0.05142369866371155), float2(0.20685400068759918, 0.11807599663734436),
    float2(0.38502201437950134, 0.7539600133895874), float2(0.4065069854259491, 0.9872130155563354),
    float2(0.7939079999923706, 0.09723199903964996), float2(0.5392929911613464, 0.993133008480072),
    float2(0.35935500264167786, 0.7271339893341064), float2(0.9175080060958862, 0.33622199296951294),
    float2(0.0383617989718914, 0.9344459772109985), float2(0.8032469749450684, 0.9429299831390381),
    float2(0.09891050308942795, 0.5130770206451416), float2(0.6351510286331177, 0.026551099494099617),
    float2(0.02212589979171753, 0.1808219999074936), float2(0.4687950015068054, 0.09451580047607422),
    float2(0.614031970500946, 0.5858640074729919), float2(0.47022899985313416, 0.31464600563049316),
    float2(0.707053005695343, 0.430525004863739), float2(0.03366189822554588, 0.7913450002670288),
    float2(0.44935500621795654, 0.8981900215148926), float2(0.8767359852790833, 0.2822049856185913),
    float2(0.17279599606990814, 0.5384079813957214), float2(0.25800299644470215, 0.4765770137310028),
    float2(0.5892509818077087, 0.8811910152435303), float2(0.34333300590515137, 0.5415509939193726),
    float2(0.25071001052856445, 0.4148379862308502), float2(0.27228599786758423, 0.8397780060768127),
    float2(0.5558339953422546, 0.9547110199928284), float2(0.013550199568271637, 0.6540729999542236),
    float2(0.3352150022983551, 0.6053959727287292), float2(0.10904300212860107, 0.5640429854393005),
    float2(0.12421000003814697, 0.16727200150489807), float2(0.3903929889202118, 0.9488199949264526),
    float2(0.8118230104446411, 0.03530989959836006), float2(0.665913999080658, 0.1921750009059906),
    float2(0.7866150140762329, 0.5288860201835632), float2(0.8380079865455627, 0.9941099882125854),
    float2(0.9034090042114258, 0.4408090114593506), float2(0.8189949989318848, 0.7336339950561523),
    float2(0.5269020199775696, 0.8884850144386292), float2(0.9153419733047485, 0.6300240159034729),
    float2(0.8071539998054504, 0.4390699863433838), float2(0.9585559964179993, 0.1530809998512268),
    float2(0.05508590117096901, 0.8280280232429504), float2(0.5516219735145569, 0.004120000172406435),
    float2(0.6189460158348083, 0.8395339846611023), float2(0.4622940123081207, 0.7839900255203247),
    float2(0.8262280225753784, 0.12442400306463242), float2(0.927702009677887, 0.09329509735107422),
    float2(0.5537580251693726, 0.26767799258232117), float2(0.03744620084762573, 0.352183997631073),
    float2(0.38016900420188904, 0.544327974319458), float2(0.8460339903831482, 0.6719570159912109),
    float2(0.7600020170211792, 0.2283090054988861), float2(0.4577470123767853, 0.9305700063705444),
    float2(0.839838981628418, 0.436598002910614), float2(0.2591629922389984, 0.8022710084915161),
    float2(0.9543439745903015, 0.9241920113563538), float2(0.7391279935836792, 0.03790400177240372),
    float2(0.195592999458313, 0.8325750231742859), float2(0.33365899324417114, 0.8562269806861877),
    float2(0.5727710127830505, 0.4569540023803711), float2(0.2591019868850708, 0.036316998302936554),
    float2(0.9794920086860657, 0.6032590270042419), float2(0.26157400012016296, 0.9602950215339661),
    float2(0.6637780070304871, 0.5244609713554382), float2(0.70100998878479, 0.882777988910675),
    float2(0.1817069947719574, 0.7600330114364624), float2(0.1148110032081604, 0.9175999760627747),
    float2(0.9108859896659851, 0.25141099095344543), float2(0.43717798590660095, 0.7649160027503967),
    float2(0.14874699711799622, 0.5772579908370972), float2(0.04138309881091118, 0.30802300572395325),
    float2(0.06421089917421341, 0.659017026424408), float2(0.06592000275850296, 0.9849849939346313),
    float2(0.9661549925804138, 0.022553199902176857), float2(0.2212589979171753, 0.4282050132751465),
    float2(0.9422590136528015, 0.29847100377082825), float2(0.5663629770278931, 0.526108980178833),
    float2(0.41761499643325806, 0.05960259959101677), float2(0.029572399333119392, 0.9823600053787231),
    float2(0.766319990158081, 0.9114660024642944), float2(0.00012207399413455278, 0.9023410081863403),
    float2(0.6978060007095337, 0.5262309908866882), float2(0.5988039970397949, 0.5335860252380371),
    float2(0.27808499336242676, 0.5724660158157349), float2(0.5938599705696106, 0.6913049817085266),
    float2(0.39493998885154724, 0.18863500654697418), float2(0.22229699790477753, 0.7057099938392639),
    float2(0.7985780239105225, 0.9938960075378418), float2(0.48066699504852295, 0.8187509775161743),
    float2(0.12372200191020966, 0.3670769929885864), float2(0.7100440263748169, 0.6486709713935852),
    float2(0.9430519938468933, 0.26413801312446594), float2(0.7608259916305542, 0.6033509969711304),
    float2(0.6530050039291382, 0.27854201197624207), float2(0.5258030295372009, 0.4099859893321991),
    float2(0.1334269940853119, 0.677174985408783), float2(0.7902460098266602, 0.2891629934310913),
    float2(0.256630003452301, 0.7044280171394348), float2(0.5891289710998535, 0.07733389735221863),
    float2(0.9811090230941772, 0.23142200708389282), float2(0.20831899344921112, 0.8974270224571228),
    float2(0.5684379935264587, 0.7473070025444031), float2(0.13040600717067719, 0.04840239882469177),
    float2(0.5238500237464905, 0.8460649847984314), float2(0.549485981464386, 0.6958829760551453),
    float2(0.3769649863243103, 0.42201000452041626), float2(0.4108709990978241, 0.6622520089149475),
    float2(0.2935880124568939, 0.055848900228738785), float2(0.4882049858570099, 0.965453028678894),
    float2(0.7264630198478699, 0.9537339806556702), float2(0.9837639927864075, 0.0706809014081955),
    float2(0.7990660071372986, 0.14807599782943726), float2(0.6926789879798889, 0.24274399876594543),
    float2(0.8107550144195557, 0.37058600783348083), float2(0.7613450288772583, 0.7735530138015747),
    float2(0.5843989849090576, 0.19711899757385254), float2(0.3822439908981323, 0.649586021900177),
    float2(0.5361189842224121, 0.5626090168952942), float2(0.1069369986653328, 0.7808769941329956),
    float2(0.04721210151910782, 0.5020599961280823), float2(0.7670519948005676, 0.25974300503730774),
    float2(0.21903100609779358, 0.269569993019104), float2(0.9999079704284668, 0.7610099911689758),
    float2(0.7401350140571594, 0.0054933298379182816), float2(0.9114959836006165, 0.0007019260083325207),
    float2(0.0054933298379182816, 0.11359000205993652), float2(0.746940016746521, 0.5364239811897278),
    float2(0.8309879899024963, 0.858119010925293), float2(0.9400010108947754, 0.46684199571609497),
    float2(0.46415600180625916, 0.7427589893341064), float2(0.3052160143852234, 0.11606200039386749),
    float2(0.2652060091495514, 0.6256899833679199), float2(0.3504140079021454, 0.7576829791069031),
    float2(0.5263530015945435, 0.22064900398254395), float2(0.43202000856399536, 0.835536003112793),
    float2(0.45939499139785767, 0.6797080039978027), float2(0.7512440085411072, 0.8253120183944702),
    float2(0.9889219999313354, 0.36979299783706665), float2(0.554764986038208, 0.9222080111503601),
    float2(0.13818800449371338, 0.33500200510025024), float2(0.06369210034608841, 0.6258739829063416),
    float2(0.7296370267868042, 0.9963380098342896), float2(0.8769490122795105, 0.5734729766845703),
    float2(0.30063799023628235, 0.15240900218486786), float2(0.41410601139068604, 0.27835899591445923),
    float2(0.4047060012817383, 0.4435249865055084), float2(0.5926390290260315, 0.9656059741973877),
    float2(0.16592900454998016, 0.39811399579048157), float2(0.40757501125335693, 0.3829460144042969),
    float2(0.14102600514888763, 0.9392679929733276), float2(0.31070899963378906, 0.44724899530410767),
    float2(0.9505910277366638, 0.8207039833068848), float2(0.185247004032135, 0.6207159757614136),
    float2(0.4216740131378174, 0.4699850082397461), float2(0.995727002620697, 0.4603720009326935),
    float2(0.6359750032424927, 0.06024349853396416), float2(0.6675620079040527, 0.7038180232048035),
    float2(0.9926760196685791, 0.50968998670578), float2(0.2269970029592514, 0.8265330195426941),
    float2(0.08923610299825668, 0.27405598759651184), float2(0.12225700169801712, 0.4274730086326599),
    float2(0.008758810348808765, 0.21646800637245178), float2(0.9691460132598877, 0.9648119807243347),
    float2(0.5273900032043457, 0.7440410256385803), float2(0.376446008682251, 0.46232500672340393),
    float2(0.2663959860801697, 0.27372100949287415), float2(0.7247229814529419, 0.5046539902687073),
    float2(0.33017998933792114, 0.0569474995136261), float2(0.10275600105524063, 0.7081519961357117),
    float2(0.2038940042257309, 0.07797479629516602), float2(0.27274399995803833, 0.09881889820098877),
    float2(0.8383129835128784, 0.5476850271224976), float2(0.13641799986362457, 0.0021057799458503723),
    float2(0.1447490006685257, 0.8574180006980896), float2(0.36631399393081665, 0.30069899559020996),
    float2(0.8690450191497803, 0.36930400133132935), float2(0.33710700273513794, 0.36228498816490173),
    float2(0.6480000019073486, 0.992247998714447), float2(0.15732300281524658, 0.4629659950733185),
    float2(0.2579120099544525, 0.5161899924278259), float2(0.756401002407074, 0.36332300305366516),
    float2(0.07925660163164139, 0.863277018070221), float2(0.49708500504493713, 0.872829020023346),
    float2(0.7334210276603699, 0.2482379972934723), float2(0.8385570049285889, 0.33674100041389465),
    float2(0.701865017414093, 0.6125680208206177), float2(0.8915070295333862, 0.1351660043001175),
    float2(0.9291669726371765, 0.8591570258140564), float2(0.16385400295257568, 0.8232979774475098),
    float2(0.29578500986099243, 0.8687090277671814), float2(0.14365099370479584, 0.1382489949464798),
    float2(0.6873679757118225, 0.00015259299834724516), float2(0.19272400438785553, 0.19836999475955963),
    float2(0.4761190116405487, 0.005981630180031061), float2(0.0029603000730276108, 0.05365150049328804),
    float2(0.37595799565315247, 0.8765529990196228), float2(0.48942500352859497, 0.17853300273418427),
    float2(0.44901901483535767, 0.2751240134239197), float2(0.05920590087771416, 0.10687600076198578),
    float2(0.21671199798583984, 0.33851099014282227), float2(0.4308910071849823, 0.3313089907169342),
    float2(0.3969849944114685, 0.018188999965786934), float2(0.7869200110435486, 0.34943699836730957),
    float2(0.4629659950733185, 0.5937380194664001), float2(0.32071900367736816, 0.48463401198387146),
    float2(0.5358740091323853, 0.18234799802303314), float2(0.5571150183677673, 0.07528919726610184),
    float2(0.3522450029850006, 0.9188820123672485), float2(0.5332189798355103, 0.5303199887275696),
    float2(0.004028439987450838, 0.5356910228729248), float2(0.10589899867773056, 0.14139199256896973),
    float2(0.3878290057182312, 0.83992999792099), float2(0.9102150201797485, 0.7370830178260803),
    float2(0.9266639947891235, 0.9795830249786377), float2(0.6080809831619263, 0.8122199773788452),
    float2(0.9649649858474731, 0.27631500363349915), float2(0.14288799464702606, 0.44889700412750244),
    float2(0.7782829999923706, 0.9297770261764526), float2(0.8665729761123657, 0.9796749949455261),
    float2(0.6211429834365845, 0.03671380132436752), float2(0.14835000038146973, 0.32108500599861145),
    float2(0.39762601256370544, 0.52531498670578), float2(0.09006009995937347, 0.532243013381958),
    float2(0.5182650089263916, 0.5578479766845703), float2(0.9172340035438538, 0.6475110054016113),
    float2(0.4321730136871338, 0.5689259767532349), float2(0.20355799794197083, 0.2852570116519928),
    float2(0.7240210175514221, 0.09759820252656937), float2(0.7398300170898438, 0.652974009513855),
    float2(0.026337500661611557, 0.46317899227142334), float2(0.3448899984359741, 0.5820180177688599),
    float2(0.6165649890899658, 0.2599869966506958), float2(0.8989840149879456, 0.29755499958992004),
    float2(0.6288949847221375, 0.8738669753074646), float2(0.2642289996147156, 0.7735530138015747),
    float2(0.27021101117134094, 0.05664239823818207), float2(0.25159499049186707, 0.26065900921821594),
    float2(0.4109010100364685, 0.3521530032157898), float2(0.2665790021419525, 0.8811910152435303),
    float2(0.8006229996681213, 0.5765249729156494), float2(0.04422130063176155, 0.9715870022773743),
    float2(0.7536550164222717, 0.7961670160293579), float2(0.9815670251846313, 0.6637780070304871),
    float2(0.6142150163650513, 0.19577600061893463), float2(0.9745169878005981, 0.46595698595046997),
    float2(0.7259439826011658, 0.7727289795875549), float2(0.10940899699926376, 0.18436199426651),
    float2(0.3935059905052185, 0.9672539830207825), float2(0.6308789849281311, 0.1587270051240921),
    float2(0.07803580164909363, 0.71144700050354), float2(0.2816550135612488, 0.35581499338150024),
    float2(0.6762599945068359, 0.12686499953269958), float2(0.5257419943809509, 0.029328299686312675),
    float2(0.6664940118789673, 0.3791919946670532), float2(0.09625539928674698, 0.3855710029602051),
    float2(0.9055150151252747, 0.9863280057907104), float2(0.794031023979187, 0.7696769833564758),
    float2(0.5939819812774658, 0.6646019816398621), float2(0.24237799644470215, 0.7307350039482117),
    float2(0.34241798520088196, 0.7878350019454956), float2(0.27280500531196594, 0.9724720120429993),
    float2(0.7841730117797852, 0.975737988948822), float2(0.08322399854660034, 0.7509689927101135),
    float2(0.1539350003004074, 0.9025539755821228), float2(0.5869929790496826, 0.2986850142478943),
    float2(0.6508380174636841, 0.46540701389312744), float2(0.9307839870452881, 0.41453298926353455),
    float2(0.04864649847149849, 0.1587270051240921), float2(0.2348400056362152, 0.9785450100898743),
    float2(0.9290140271186829, 0.01223789993673563), float2(0.8074589967727661, 0.3382669985294342),
    float2(0.279092013835907, 0.4622940123081207), float2(0.8978850245475769, 0.3205359876155853),
    float2(0.16229699552059174, 0.5076749920845032), float2(0.024140100926160812, 0.8540909886360168),
    float2(0.32322201132774353, 0.6404920220375061), float2(0.774528980255127, 0.44495999813079834),
    float2(0.583361029624939, 0.5878170132637024), float2(0.23020100593566895, 0.5282449722290039),
    float2(0.7177950143814087, 0.5284280180931091), float2(0.895779013633728, 0.9174169898033142),
    float2(0.05331579968333244, 0.08786279708147049), float2(0.6327099800109863, 0.2616350054740906),
    float2(0.7405930161476135, 0.10132099688053131), float2(0.7268900275230408, 0.81563800573349),
    float2(0.4935759902000427, 0.7398599982261658), float2(0.19110700488090515, 0.25623300671577454),
    float2(0.7097690105438232, 0.33460500836372375), float2(0.30463600158691406, 0.4738300144672394),
    float2(0.1699880063533783, 0.8445690274238586), float2(0.1622059941291809, 0.13837100565433502),
    float2(0.6375619769096375, 0.5273900032043457), float2(0.9686880111694336, 0.5222629904747009),
    float2(0.6437569856643677, 0.2951439917087555), float2(0.8762779831886292, 0.06378369778394699),
    float2(0.8490859866142273, 0.6306959986686707), float2(0.3152559995651245, 0.31974199414253235),
    float2(0.6399120092391968, 0.0016479999758303165), float2(0.7389140129089355, 0.8538159728050232),
    float2(0.834650993347168, 0.6953639984130859), float2(0.21842099726200104, 0.5859249830245972),
    float2(0.20313100516796112, 0.3199560046195984), float2(0.2605060040950775, 0.9350569844245911),
    float2(0.5032200217247009, 0.22409699857234955), float2(0.9806510210037231, 0.6869109869003296),
    float2(0.04492320120334625, 0.29142099618911743), float2(0.6318550109863281, 0.45756399631500244),
    float2(0.6268810033798218, 0.783715009689331), float2(0.4195989966392517, 0.6993619799613953),
    float2(0.0400707982480526, 0.055970899760723114), float2(0.03051850013434887, 0.9512010216712952),
    float2(0.9286779761314392, 0.929502010345459), float2(0.8272039890289307, 0.7477650046348572),
    float2(0.11105699837207794, 0.01834160089492798), float2(0.5337380170822144, 0.9626150131225586),
    float2(0.9068269729614258, 0.6102790236473083), float2(0.5822929739952087, 0.2507399916648865),
    float2(0.05746639892458916, 0.8055970072746277), float2(0.711355984210968, 0.3095490038394928),
    float2(0.3408310115337372, 0.9883419871330261), float2(0.8656880259513855, 0.1908629983663559),
    float2(0.28107500076293945, 0.8549759984016418), float2(0.673695981502533, 0.6842250227928162),
    float2(0.4851219952106476, 0.8502150177955627), float2(0.12683500349521637, 0.11697699874639511),
    float2(0.38239699602127075, 0.7724850177764893), float2(0.14078199863433838, 0.4656510055065155),
    float2(0.3996399939060211, 0.7349159717559814), float2(0.925961971282959, 0.6934720277786255),
    float2(0.003295999951660633, 0.4524979889392853), float2(0.018677299842238426, 0.05279700085520744),
    float2(0.31504300236701965, 0.6172059774398804), float2(0.9683520197868347, 0.2957240045070648),
    float2(0.1404460072517395, 0.21460600197315216), float2(0.8244580030441284, 0.9115880131721497),
    float2(0.6701560020446777, 0.2635580003261566), float2(0.9408860206604004, 0.07382430136203766),
    float2(0.5609909892082214, 0.855434000492096), float2(0.9088109731674194, 0.40531599521636963),
    float2(0.04318369925022125, 0.0306100994348526), float2(0.8865630030632019, 0.5467389822006226),
    float2(0.5602589845657349, 0.3935360014438629), float2(0.7865840196609497, 0.6956999897956848),
    float2(0.9813230037689209, 0.3108919858932495), float2(0.741690993309021, 0.807000994682312),
    float2(0.7927489876747131, 0.9555649757385254), float2(0.03796499967575073, 0.1091340035200119),
    float2(0.5427719950675964, 0.3266089856624603), float2(0.2568129897117615, 0.2299260050058365),
    float2(0.5290690064430237, 0.12536999583244324), float2(0.15723100304603577, 0.21924500167369843),
    float2(0.5113679766654968, 0.6803799867630005), float2(0.8819540143013, 0.08236949890851974),
    float2(0.7788019776344299, 0.3316139876842499), float2(0.8931549787521362, 0.23706799745559692),
    float2(0.9986569881439209, 0.2539139986038208), float2(0.5540030002593994, 0.3552660048007965),
    float2(0.3447679877281189, 0.32279399037361145), float2(0.06250189989805222, 0.5236979722976685),
    float2(0.48634299635887146, 0.7996150255203247), float2(0.09692680090665817, 0.8869900107383728),
    float2(0.10077200084924698, 0.3481859862804413), float2(0.30857300758361816, 0.3744010031223297),
    float2(0.11847300082445145, 0.6935030221939087), float2(0.27414798736572266, 0.17810599505901337),
    float2(0.10101599991321564, 0.6044800281524658), float2(0.44770699739456177, 0.8116400241851807),
    float2(0.6609389781951904, 0.8439589738845825), float2(0.8148750066757202, 0.7059850096702576),
    float2(0.19531799852848053, 0.06024349853396416), float2(0.17032399773597717, 0.3525800108909607),
    float2(0.14594000577926636, 0.2640460133552551), float2(0.6514790058135986, 0.8976100087165833),
    float2(0.5121009945869446, 0.04525899887084961), float2(0.9422889947891235, 0.6454660296440125),
    float2(0.8371229767799377, 0.566057026386261), float2(0.6933190226554871, 0.7099519968032837),
    float2(0.22318199276924133, 0.2987760007381439), float2(0.776665985584259, 0.991424024105072),
    float2(0.6462900042533875, 0.8787189722061157), float2(0.013794399797916412, 0.06796470284461975),
    float2(0.5755490064620972, 0.8907129764556885), float2(0.6829130053520203, 0.5961490273475647),
    float2(0.6779080033302307, 0.17645800113677979), float2(0.3838619887828827, 0.20413799583911896),
    float2(0.039155200123786926, 0.65385901927948), float2(0.7220979928970337, 0.9296240210533142),
    float2(0.4055599868297577, 0.6443679928779602), float2(0.646992027759552, 0.7561569809913635),
    float2(0.9165930151939392, 0.11093500256538391), float2(0.7798699736595154, 0.5862910151481628),
    float2(0.5788750052452087, 0.7039700150489807), float2(0.1622059941291809, 0.6864219903945923),
    float2(0.5453659892082214, 0.5935239791870117), float2(0.6008179783821106, 0.9816280007362366),
    float2(0.4918060004711151, 0.32804301381111145), float2(0.3344219923019409, 0.036591701209545135),
    float2(0.437483012676239, 0.4643389880657196), float2(0.11142300069332123, 0.2599259912967682),
    float2(0.8161569833755493, 0.5559859871864319), float2(0.5656300187110901, 0.5763729810714722),
    float2(0.4595780074596405, 0.6119269728660583), float2(0.7170630097389221, 0.06869719922542572),
    float2(0.8241519927978516, 0.5289160013198853), float2(0.09192179888486862, 0.2417680025100708),
    float2(0.07708980143070221, 0.3779410123825073), float2(0.8162180185317993, 0.9664300084114075),
    float2(0.07837150245904922, 0.5922729969024658), float2(0.10809700191020966, 0.06491290032863617),
    float2(0.664205014705658, 0.9528489708900452), float2(0.6889550089836121, 0.5806760191917419),
    float2(0.06735440343618393, 0.2725299894809723), float2(0.02386550046503544, 0.7331770062446594),
    float2(0.8414869904518127, 0.9492779970169067), float2(0.9583420157432556, 0.4991300106048584),
    float2(0.2952969968318939, 0.9075899720191956), float2(0.4260689914226532, 0.6551409959793091),
    float2(0.3744930028915405, 0.8037049770355225), float2(0.45243701338768005, 0.7263100147247314),
    float2(0.770684003829956, 0.6922510266304016), float2(0.2409130036830902, 0.5623040199279785),
    float2(0.8374890089035034, 0.1417890042066574), float2(0.9472950100898743, 0.6683549880981445),
    float2(0.6531879901885986, 0.5027620196342468), float2(0.7993410229682922, 0.4907679855823517),
    float2(0.044373899698257446, 0.9018830060958862), float2(0.23279500007629395, 0.08410900086164474),
    float2(0.039277300238609314, 0.24481900036334991), float2(0.6735739707946777, 0.050630200654268265),
    float2(0.42893800139427185, 0.6259040236473083), float2(0.8502150177955627, 0.02804649993777275),
    float2(0.13934800028800964, 0.9736009836196899), float2(0.70973801612854, 0.1386760026216507),
    float2(0.7105010151863098, 0.24466699361801147), float2(0.806207001209259, 0.4577470123767853),
    float2(0.3526720106601715, 0.48011699318885803), float2(0.019013000652194023, 0.5774409770965576),
    float2(0.30634498596191406, 0.7099519968032837), float2(0.6133000254631042, 0.8608049750328064),
    float2(0.870693027973175, 0.6712549924850464), float2(0.1938840001821518, 0.1082490012049675),
    float2(0.8637959957122803, 0.11880899965763092), float2(0.6128730177879333, 0.6519359946250916),
    float2(0.740776002407074, 0.4344309866428375), float2(0.7747730016708374, 0.8911100029945374),
    float2(0.9734179973602295, 0.8720660209655762), float2(0.29666998982429504, 0.07504499703645706),
    float2(0.6775410175323486, 0.7664719820022583), float2(0.7274090051651001, 0.737326979637146),
    float2(0.231330007314682, 0.1134679988026619), float2(0.8655660152435303, 0.5137490034103394),
    float2(0.02783289924263954, 0.39231500029563904), float2(0.676351010799408, 0.0865200012922287),
    float2(0.08197270333766937, 0.12448500096797943), float2(0.7660449743270874, 0.9399700164794922),
    float2(0.4135870039463043, 0.81392902135849), float2(0.38462498784065247, 0.9191870093345642),
    float2(0.9143959879875183, 0.8536940217018127), float2(0.8333079814910889, 0.8886989951133728),
    float2(0.5034639835357666, 0.5336470007896423), float2(0.9488199949264526, 0.04211549833416939),
    float2(0.8899199962615967, 0.51670902967453), float2(0.029145199805498123, 0.1991640031337738),
    float2(0.6961269974708557, 0.9537950158119202), float2(0.6809290051460266, 0.8413649797439575),
    float2(0.4759669899940491, 0.42362698912620544), float2(0.8825039863586426, 0.4031189978122711),
    float2(0.48719701170921326, 0.6223949790000916), float2(0.29154300689697266, 0.28327301144599915),
    float2(0.41770699620246887, 0.8735920190811157), float2(0.5697810053825378, 0.5599539875984192),
    float2(0.7090370059013367, 0.819940984249115), float2(0.9451889991760254, 0.7791069746017456),
    float2(0.20664100348949432, 0.559831976890564), float2(0.15451499819755554, 0.8005009889602661),
    float2(0.761559009552002, 0.583666980266571), float2(0.8436229825019836, 0.5150610208511353),
    float2(0.9064610004425049, 0.36847999691963196), float2(0.7637559771537781, 0.1731320023536682),
    float2(0.3397020101547241, 0.2404550015926361), float2(0.9735710024833679, 0.1291240006685257),
    float2(0.5914790034294128, 0.32502201199531555), float2(0.7144380211830139, 0.22290700674057007),
    float2(0.28156399726867676, 0.014984600245952606), float2(0.37586599588394165, 0.157383993268013),
    float2(0.8169500231742859, 0.06500440090894699), float2(0.5927299857139587, 0.043763499706983566),
    float2(0.33323198556900024, 0.9622179865837097), float2(0.8095030188560486, 0.11258299648761749),
    float2(0.8926969766616821, 0.8699300289154053), float2(0.27539899945259094, 0.6830959916114807),
    float2(0.6876429915428162, 0.7784050107002258), float2(0.04742579907178879, 0.6855679750442505),
    float2(0.8386790156364441, 0.7967159748077393), float2(0.003295999951660633, 0.960204005241394),
    float2(0.4747759997844696, 0.7012540102005005), float2(0.6989960074424744, 0.40763598680496216),
    float2(0.46705499291419983, 0.9523910284042358), float2(0.009064000099897385, 0.7844780087471008),
    float2(0.7901549935340881, 0.8426160216331482), float2(0.7426679730415344, 0.9260839819908142),
    float2(0.9539480209350586, 0.08786279708147049), float2(0.7841119766235352, 0.3865779936313629),
    float2(0.4679099917411804, 0.6360059976577759), float2(0.2775050103664398, 0.7463300228118896),
    float2(0.9962769746780396, 0.017731299623847008), float2(0.6936249732971191, 0.015411799773573875),
    float2(0.16180899739265442, 0.27713900804519653), float2(0.7480700016021729, 0.5717949867248535),
    float2(0.38032200932502747, 0.30930501222610474), float2(0.26319199800491333, 0.0722372978925705),
    float2(0.12341699749231339, 0.736503005027771), float2(0.29456499218940735, 0.09595020115375519),
    float2(0.40058600902557373, 0.26285600662231445), float2(0.7446519732475281, 0.020508399233222008),
    float2(0.2360610067844391, 0.4338510036468506), float2(0.5066990256309509, 0.3561820089817047),
    float2(0.7633900046348572, 0.7488020062446594), float2(0.18716999888420105, 0.5315709710121155),
    float2(0.5283359885215759, 0.6516619920730591), float2(0.875361979007721, 0.8960540294647217),
    float2(0.3034459948539734, 0.9632560014724731), float2(0.28626400232315063, 0.6138489842414856),
    float2(0.41810399293899536, 0.3053379952907562), float2(0.6449170112609863, 0.386821985244751),
    float2(0.47199898958206177, 0.2539750039577484), float2(0.9628890156745911, 0.5816829800605774),
    float2(0.4387950003147125, 0.18417899310588837), float2(0.6507459878921509, 0.6331980228424072),
    float2(0.8930330276489258, 0.038544900715351105), float2(0.8532059788703918, 0.003906370140612125),
    float2(0.39274299144744873, 0.16058799624443054), float2(0.20053699612617493, 0.4065679907798767),
    float2(0.38416698575019836, 0.6161990165710449), float2(0.676351010799408, 0.10593000054359436),
    float2(0.061189599335193634, 0.7376319766044617), float2(0.5238810181617737, 0.6342049837112427),
    float2(0.07486189901828766, 0.8802449703216553), float2(0.2795189917087555, 0.6369820237159729),
    float2(0.9775689840316772, 0.5641040205955505), float2(0.6579790115356445, 0.7241430282592773),
    float2(0.587602972984314, 0.43284401297569275), float2(0.1535390019416809, 0.42844900488853455),
    float2(0.5446940064430237, 0.4000059962272644), float2(0.7756890058517456, 0.48335200548171997),
    float2(0.22074000537395477, 0.04196299985051155), float2(0.08465830236673355, 0.2912989854812622),
    float2(0.23642699420452118, 0.7124850153923035), float2(0.00299080996774137, 0.349590003490448),
    float2(0.9074069857597351, 0.7080289721488953), float2(0.8657799959182739, 0.7618029713630676),
    float2(0.7648239731788635, 0.8086180090904236), float2(0.3283179998397827, 0.34629398584365845),
    float2(0.3868829905986786, 0.0961942970752716), float2(0.6354870200157166, 0.5113070011138916),
    float2(0.2108519971370697, 0.5334029793739319), float2(0.8951690196990967, 0.9441210031509399),
    float2(0.905239999294281, 0.47059500217437744), float2(0.2443619966506958, 0.9996340274810791),
    float2(0.6532179713249207, 0.06213570013642311), float2(0.18250100314617157, 0.14465799927711487),
    float2(0.9402449727058411, 0.113926000893116), float2(0.8352310061454773, 0.8336129784584045),
    float2(0.7318639755249023, 0.8900110125541687), float2(0.4682759940624237, 0.8510090112686157),
    float2(0.7259439826011658, 0.050416599959135056), float2(0.02999969944357872, 0.5138710141181946),
    float2(0.6426889896392822, 0.8466749787330627), float2(0.115665003657341, 0.6367689967155457),
    float2(0.8600119948387146, 0.31769800186157227), float2(0.651295006275177, 0.15012100338935852),
    float2(0.14667199552059174, 0.48329100012779236), float2(0.002594070043414831, 0.080690898001194),
    float2(0.2742699980735779, 0.9473559856414795), float2(0.8565629720687866, 0.884456992149353),
    float2(0.6153450012207031, 0.5258949995040894), float2(0.6161689758300781, 0.0650349035859108),
    float2(0.04464859887957573, 0.7540209889411926), float2(0.6215699911117554, 0.610614001750946),
    float2(0.9099090099334717, 0.12802499532699585), float2(0.38581499457359314, 0.7100440263748169),
    float2(0.9608139991760254, 0.8434709906578064), float2(0.2586140036582947, 0.17401699721813202),
    float2(0.3295390009880066, 0.0877406969666481), float2(0.3620719909667969, 0.010986699722707272),
    float2(0.5771049857139587, 0.7944579720497131), float2(0.9248939752578735, 0.6160770058631897),
    float2(0.35148200392723083, 0.871487021446228), float2(0.1452070027589798, 0.08713030070066452),
    float2(0.26370999217033386, 0.9180880188941956), float2(0.9304180145263672, 0.21790200471878052),
    float2(0.03839230164885521, 0.8409990072250366), float2(0.3638420104980469, 0.9659109711647034),
    float2(0.7118750214576721, 0.9094820022583008), float2(0.5462200045585632, 0.9036229848861694),
    float2(0.7722709774971008, 0.5484790205955505), float2(0.06863609701395035, 0.23969200253486633),
    float2(0.5107269883155823, 0.6626480221748352), float2(0.8228399753570557, 0.200873002409935),
    float2(0.8471019864082336, 0.7287819981575012), float2(0.8039489984512329, 0.3090910017490387),
    float2(0.16592900454998016, 0.9992070198059082), float2(0.017029300332069397, 0.258338987827301),
    float2(0.14404700696468353, 0.6911529898643494), float2(0.30634498596191406, 0.8825950026512146),
    float2(0.2857449948787689, 0.8237559795379639), float2(0.9004179835319519, 0.21585699915885925),
    float2(0.7908260226249695, 0.6521810293197632), float2(0.542618989944458, 0.09350869804620743),
    float2(0.9871519804000854, 0.40418699383735657), float2(0.586778998374939, 0.546159029006958),
    float2(0.0012207400286570191, 0.14838099479675293), float2(0.3886840045452118, 0.04126099869608879),
    float2(0.30332300066947937, 0.6541950106620789), float2(0.2299869954586029, 0.4130989909172058),
    float2(0.08926659822463989, 0.05551319941878319), float2(0.05078279972076416, 0.44187700748443604),
    float2(0.16595999896526337, 0.9257789850234985), float2(0.3691520094871521, 0.43934398889541626),
    float2(0.9942010045051575, 0.8421279788017273), float2(0.6016420125961304, 0.2378309965133667),
    float2(0.2598649859428406, 0.31934601068496704), float2(0.31193000078201294, 0.09430219978094101),
    float2(0.8503680229187012, 0.5842159986495972), float2(0.4444110095500946, 0.9432049989700317),
    float2(0.8410900235176086, 0.6138190031051636), float2(0.7526469826698303, 0.8688920140266418),
    float2(0.8423110246658325, 0.4768820106983185), float2(0.9987789988517761, 0.5566580295562744),
    float2(0.7288429737091064, 0.2999970018863678), float2(0.41907998919487, 0.51139897108078),
    float2(0.006530960090458393, 0.19009999930858612), float2(0.777184009552002, 0.8656880259513855),
    float2(0.5524160265922546, 0.8083130121231079), float2(0.9157080054283142, 0.2674950063228607),
    float2(0.2386849969625473, 0.9572740197181702), float2(0.17767900228500366, 0.6733300089836121),
    float2(0.4201180040836334, 0.7552419900894165), float2(0.6901149749755859, 0.3751029968261719),
    float2(0.6461989879608154, 0.48582398891448975), float2(0.14795400202274323, 0.8742030262947083),
    float2(0.45377999544143677, 0.5587630271911621), float2(0.8760949969291687, 0.7765129804611206),
    float2(0.09781180322170258, 0.4535660147666931), float2(0.288796991109848, 0.7829520106315613),
    float2(0.5181130170822144, 0.9272440075874329), float2(0.5347149968147278, 0.8046209812164307),
    float2(0.6116520166397095, 0.1325719952583313), float2(0.39588600397109985, 0.3677789866924286),
    float2(0.07458720356225967, 0.7817320227622986), float2(0.09790340065956116, 0.7313460111618042),
    float2(0.47846901416778564, 0.6591389775276184), float2(0.7460860013961792, 0.17899100482463837),
    float2(0.8828089833259583, 0.659932017326355), float2(0.5023350119590759, 0.5522930026054382),
    float2(0.6184269785881042, 0.745140016078949), float2(0.03329569846391678, 0.2152779996395111),
    float2(0.8204290270805359, 0.9392380118370056), float2(0.2187259942293167, 0.9186379909515381),
    float2(0.9653000235557556, 0.9029819965362549), float2(0.41251900792121887, 0.32721900939941406),
    float2(0.5757930278778076, 0.11654999852180481), float2(0.4983980059623718, 0.8133180141448975),
    float2(0.8005620241165161, 0.8046510219573975), float2(0.014648900367319584, 0.7161779999732971),
    float2(0.28116700053215027, 0.2526319921016693), float2(0.46018901467323303, 0.9919739961624146),
    float2(0.8584250211715698, 0.48796001076698303), float2(0.4055910110473633, 0.2269359976053238),
    float2(0.6199529767036438, 0.34458398818969727), float2(0.22595900297164917, 0.7304610013961792),
    float2(0.8765529990196228, 0.38453298807144165), float2(0.7404710054397583, 0.5567190051078796),
    float2(0.33588701486587524, 0.9378029704093933), float2(0.6446120142936707, 0.5851920247077942),
    float2(0.09021270275115967, 0.8339790105819702), float2(0.7720270156860352, 0.8380690217018127),
    float2(0.6318249702453613, 0.5486310124397278), float2(0.5929440259933472, 0.9261149764060974),
    float2(0.6869109869003296, 0.8998990058898926), float2(0.655538022518158, 0.8112429976463318),
    float2(0.5086519718170166, 0.12631599605083466), float2(0.732109010219574, 0.20297899842262268),
    float2(0.5872679948806763, 0.01049839984625578), float2(0.12387499958276749, 0.8373969793319702),
    float2(0.5549790263175964, 0.20761699974536896), float2(0.0631427988409996, 0.17551200091838837),
    float2(0.5942869782447815, 0.7750179767608643), float2(0.20181900262832642, 0.8092589974403381),
    float2(0.23673200607299805, 0.13409799337387085), float2(0.9876400232315063, 0.08883939683437347),
    float2(0.8188419938087463, 0.4270760118961334), float2(0.11001899838447571, 0.47312799096107483),
    float2(0.7257300019264221, 0.8386179804801941), float2(0.7529529929161072, 0.20108599960803986),
    float2(0.5498830080032349, 0.49897798895835876), float2(0.399152010679245, 0.7832580208778381),
    float2(0.44984298944473267, 0.3073819875717163), float2(0.3033849895000458, 0.014557300135493279),
    float2(0.4429759979248047, 0.3716540038585663), float2(0.4494459927082062, 0.8466749787330627),
};

#endif
// FO4RE_INLINE_INCLUDE_END shadow_poisson_kernel.hlsli
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

    float centreDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float centreLit = (centreDepth >= shadowZ) ? 1.0 : 0.0;

    float averageBlocker = blocker.x / blocker.y;
    float worldRange     = cascadeScale.w - cascadeScale.z;
    float receiverWorld  = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld   = worldRange * averageBlocker + cascadeScale.z;
    float separation     = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra       = (blockerWorld < cascadeScale.z + 0.001)
        ? 1.9
        : (separation * 1.8 + 0.1);

    float sum = centreLit;
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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

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
#ifdef SPECULAR
    output.specular.xyz = (brdfSpecular * cookieRGB) * attenuation;
#else
    output.specular.xyz = float3(0, 0, 0);
#endif
    output.specular.w = 1.0;
    output.diffuse = float4(diffuseAccum, 0.0);
    output.diffuse *= attenuation;
    output.diffuse /= 3.0;
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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
// FO4RE_INLINE_INCLUDE_BEGIN shadow_poisson_kernel.hlsli

#ifndef SHADOW_POISSON_KERNEL_HLSLI_INCLUDED
#define SHADOW_POISSON_KERNEL_HLSLI_INCLUDED

#define SHADOW_POISSON_KERNEL_SIZE 1000

static const float2 SHADOW_POISSON_KERNEL[SHADOW_POISSON_KERNEL_SIZE] =
{
    float2(0.4933930039405823, 0.3942689895629883), float2(0.7985470294952393, 0.8859220147132874),
    float2(0.2473219931125641, 0.9264500141143799), float2(0.051454201340675354, 0.14078199863433838),
    float2(0.8318430185317993, 0.009552289731800556), float2(0.428631991147995, 0.017151400446891785),
    float2(0.01565600000321865, 0.7497789859771729), float2(0.7583850026130676, 0.4961700141429901),
    float2(0.2234870046377182, 0.5621510148048401), float2(0.011627599596977234, 0.4069949984550476),
    float2(0.24146200716495514, 0.30463600158691406), float2(0.430310994386673, 0.7272260189056396),
    float2(0.981810986995697, 0.27835899591445923), float2(0.4070560038089752, 0.5005339980125427),
    float2(0.123478002846241, 0.4635460078716278), float2(0.8095340132713318, 0.6822720170021057),
    float2(0.6758019924163818, 0.6539199948310852), float2(0.23801399767398834, 0.06933809816837311),
    float2(0.0006714069750159979, 0.6111029982566833), float2(0.6218760013580322, 0.4990389943122864),
    float2(0.7128819823265076, 0.11529900133609772), float2(0.9136630296707153, 0.8193910121917725),
    float2(0.29545000195503235, 0.8096870183944702), float2(0.9850149750709534, 0.1178010031580925),
    float2(0.6307569742202759, 0.31321099400520325), float2(0.362621009349823, 0.1857050061225891),
    float2(0.16446399688720703, 0.7875909805297852), float2(0.0038453300949186087, 0.9388409852981567),
    float2(0.5227519869804382, 0.14627499878406525), float2(0.9875180125236511, 0.9389939904212952),
    float2(0.7701039910316467, 0.3155309855937958), float2(0.04483170062303543, 0.26883798837661743),
    float2(0.972320020198822, 0.43855100870132446), float2(0.6903589963912964, 0.9747310280799866),
    float2(0.5827199816703796, 0.8335520029067993), float2(0.49678000807762146, 0.9989929795265198),
    float2(0.498214989900589, 0.6034730076789856), float2(0.9164400100708008, 0.5907769799232483),
    float2(0.851131021976471, 0.21952000260353088), float2(0.4134649932384491, 0.893123984336853),
    float2(0.004425180144608021, 0.015686500817537308), float2(0.5808889865875244, 0.027405599132180214),
    float2(0.09085360169410706, 0.36497101187705994), float2(0.9074980020523071, 0.3878290057182312),
    float2(0.1073639988899231, 0.7465130090713501), float2(0.9870910048484802, 0.18369099497795105),
    float2(0.30414700508117676, 0.5427410006523132), float2(0.7691270112991333, 0.022675300016999245),
    float2(0.8954439759254456, 0.05838190019130707), float2(0.70967698097229, 0.20316199958324432),
    float2(0.4203920066356659, 0.4147160053253174), float2(0.8330940008163452, 0.15762799978256226),
    float2(0.29096299409866333, 0.19553199410438538), float2(0.4844200015068054, 0.9089329838752747),
    float2(0.7604910135269165, 0.9561449885368347), float2(0.03717150166630745, 0.5517749786376953),
    float2(0.14200299978256226, 0.19510500133037567), float2(0.9505599737167358, 0.7496259808540344),
    float2(0.36411601305007935, 0.7906429767608643), float2(0.22901099920272827, 0.8579360246658325),
    float2(0.7427290081977844, 0.7322310209274292), float2(0.7128509879112244, 0.3787960112094879),
    float2(0.34632399678230286, 0.4421829879283905), float2(0.4817650020122528, 0.22287699580192566),
    float2(0.6732990145683289, 0.5668200254440308), float2(0.0006408889894373715, 0.3230080008506775),
    float2(0.8751789927482605, 0.32813501358032227), float2(0.390514999628067, 0.3244419991970062),
    float2(0.9904170036315918, 0.6501359939575195), float2(0.35621199011802673, 0.9518420100212097),
    float2(0.43202000856399536, 0.796563982963562), float2(0.6701859831809998, 0.44901901483535767),
    float2(0.6072880029678345, 0.7214270234107971), float2(0.13770000636577606, 0.5319679975509644),
    float2(0.7076939940452576, 0.8413950204849243), float2(0.8066959977149963, 0.8207039833068848),
    float2(0.6437270045280457, 0.10141299664974213), float2(0.2517470121383667, 0.00012207399413455278),
    float2(0.5584890246391296, 0.4125489890575409), float2(0.5076450109481812, 0.00634784996509552),
    float2(0.8238170146942139, 0.40803200006484985), float2(0.3017059862613678, 0.35984399914741516),
    float2(0.30072900652885437, 0.6263920068740845), float2(0.9513229727745056, 0.5352029800415039),
    float2(0.11658100038766861, 0.8782010078430176), float2(0.3677479922771454, 0.0460829995572567),
    float2(0.2561720013618469, 0.7403180003166199), float2(0.16229699552059174, 0.9830009937286377),
    float2(0.5321210026741028, 0.4979709982872009), float2(0.20673200488090515, 0.23987500369548798),
    float2(0.10248100012540817, 0.6265760064125061), float2(0.34681200981140137, 0.6882839798927307),
    float2(0.903531014919281, 0.6722310185432434), float2(0.17407800257205963, 0.12005999684333801),
    float2(0.31723999977111816, 0.285317987203598), float2(0.09793390333652496, 0.9796140193939209),
    float2(0.19544099271297455, 0.38596799969673157), float2(0.11398699879646301, 0.300942987203598),
    float2(0.8307440280914307, 0.5856199860572815), float2(0.5628529787063599, 0.6620380282402039),
    float2(0.3735159933567047, 0.11416999995708466), float2(0.887935996055603, 0.9788510203361511),
    float2(0.9789119958877563, 0.8495740294456482), float2(0.5026400089263916, 0.06869719922542572),
    float2(0.1686760038137436, 0.05017239972949028), float2(0.8659319877624512, 0.5303509831428528),
    float2(0.9236429929733276, 0.1647389978170395), float2(0.04965360090136528, 0.2052370011806488),
    float2(0.8231760263442993, 0.0799890011548996), float2(0.024811500683426857, 0.6864219903945923),
    float2(0.8720660209655762, 0.871882975101471), float2(0.6138799786567688, 0.991362988948822),
    float2(0.09607230126857758, 0.0949430987238884), float2(0.8258919715881348, 0.2834559977054596),
    float2(0.18890999257564545, 0.4492020010948181), float2(0.6251720190048218, 0.9024930000305176),
    float2(0.5876340270042419, 0.5638599991798401), float2(0.012054800055921078, 0.4879299998283386),
    float2(0.3265480101108551, 0.8919640183448792), float2(0.9322789907455444, 0.8912010192871094),
    float2(0.49372801184654236, 0.6959750056266785), float2(0.6568499803543091, 0.780144989490509),
    float2(0.47056499123573303, 0.46147000789642334), float2(0.3726620078086853, 0.25141099095344543),
    float2(0.8736839890480042, 0.4529249966144562), float2(0.1747490018606186, 0.6543470025062561),
    float2(0.6952120065689087, 0.3021329939365387), float2(0.09561450034379959, 0.8131960034370422),
    float2(0.034150201827287674, 0.07660149782896042), float2(0.06680499762296677, 0.9160130023956299),
    float2(0.23401600122451782, 0.6280710101127625), float2(0.616595983505249, 0.376446008682251),
    float2(0.5639820098876953, 0.22901099920272827), float2(0.010223699733614922, 0.865444004535675),
    float2(0.41499099135398865, 0.6073489785194397), float2(0.631397008895874, 0.2421029955148697),
    float2(0.07110810279846191, 0.002563549904152751), float2(0.8778039813041687, 0.7384870052337646),
    float2(0.9971920251846313, 0.03616439923644066), float2(0.4361099898815155, 0.11423099786043167),
    float2(0.9591969847679138, 0.33796200156211853), float2(0.7051299810409546, 0.04675440117716789),
    float2(0.18008999526500702, 0.3244119882583618), float2(0.6102179884910583, 0.1621749997138977),
    float2(0.5471659898757935, 0.300942987203598), float2(0.18503400683403015, 0.9186990261077881),
    float2(0.4466080069541931, 0.9603869915008545), float2(0.954069972038269, 0.9948729872703552),
    float2(0.33497101068496704, 0.166935995221138), float2(0.3333840072154999, 0.21280600130558014),
    float2(0.4661700129508972, 0.5096290111541748), float2(0.36365899443626404, 0.34330299496650696),
    float2(0.17191100120544434, 0.17108699679374695), float2(0.759880006313324, 0.4608910083770752),
    float2(0.2912079989910126, 0.9834280014038086), float2(0.7586290240287781, 0.10745599865913391),
    float2(0.04507580026984215, 0.596759021282196), float2(0.9029819965362549, 0.5074009895324707),
    float2(0.5965149998664856, 0.7915890216827393), float2(0.17499299347400665, 0.24253100156784058),
    float2(0.4685809910297394, 0.5535449981689453), float2(0.8664510250091553, 0.6336860060691833),
    float2(0.6723840236663818, 0.06936860084533691), float2(0.23993700742721558, 0.18555299937725067),
    float2(0.6918849945068359, 0.7355570197105408), float2(0.6443979740142822, 0.7349770069122314),
    float2(0.41938498616218567, 0.558152973651886), float2(0.4960170090198517, 0.43458399176597595),
    float2(0.6652119755744934, 0.913815975189209), float2(0.2779630124568939, 0.765434980392456),
    float2(0.08548229932785034, 0.17151400446891785), float2(0.30582600831985474, 0.39497101306915283),
    float2(0.7080289721488953, 0.5744190216064453), float2(0.7287819981575012, 0.16055800020694733),
    float2(0.1861020028591156, 0.28904101252555847), float2(0.6779379844665527, 0.14361999928951263),
    float2(0.14032399654388428, 0.7077850103378296), float2(0.05908380076289177, 0.760308027267456),
    float2(0.6109809875488281, 0.4517959952354431), float2(0.03625600039958954, 0.007629630155861378),
    float2(0.9473249912261963, 0.4029659926891327), float2(0.19592900574207306, 0.6829130053520203),
    float2(0.057710498571395874, 0.46809300780296326), float2(0.5917540192604065, 0.2777490019798279),
    float2(0.7339699864387512, 0.6210520267486572), float2(0.9282199740409851, 0.7740709781646729),
    float2(0.8901939988136292, 0.18771900236606598), float2(0.740805983543396, 0.0805383026599884),
    float2(0.7708979845046997, 0.5667589902877808), float2(0.480087012052536, 0.13376300036907196),
    float2(0.33899998664855957, 0.016968300566077232), float2(0.8547930121421814, 0.8099920153617859),
    float2(0.5144199728965759, 0.2596510052680969), float2(0.1364479959011078, 0.6233410239219666),
    float2(0.3690600097179413, 0.5036159753799438), float2(0.33542901277542114, 0.6557819843292236),
    float2(0.5450000166893005, 0.6102790236473083), float2(0.9918820261955261, 0.7283239960670471),
    float2(0.27637600898742676, 0.8976100087165833), float2(0.6288340091705322, 0.9483630061149597),
    float2(0.5381019711494446, 0.7844169735908508), float2(0.06775110214948654, 0.057588398456573486),
    float2(0.09720149636268616, 0.035279400646686554), float2(0.4394359886646271, 0.6425979733467102),
    float2(0.2509230077266693, 0.3704639971256256), float2(0.45289498567581177, 0.17441299557685852),
    float2(0.20642699301242828, 0.7395550012588501), float2(0.4592120051383972, 0.3491320013999939),
    float2(0.8646810054779053, 0.020020099356770515), float2(0.6320689916610718, 0.18671199679374695),
    float2(0.792352020740509, 0.21115800738334656), float2(0.09134189784526825, 0.40156298875808716),
    float2(0.3082979917526245, 0.2425609976053238), float2(0.5730460286140442, 0.3318580090999603),
    float2(0.5030670166015625, 0.6413159966468811), float2(0.665058970451355, 0.8760030269622803),
    float2(0.2581869959831238, 0.5482649803161621), float2(0.873134970664978, 0.6994839906692505),
    float2(0.3422960042953491, 0.11078199744224548), float2(0.2691729962825775, 0.21857400238513947),
    float2(0.07446520030498505, 0.5483570098876953), float2(0.35157299041748047, 0.40360701084136963),
    float2(0.6602979898452759, 0.3413800001144409), float2(0.86285001039505, 0.09915459901094437),
    float2(0.20813600718975067, 0.952817976474762), float2(0.19370099902153015, 0.029023099690675735),
    float2(0.40882599353790283, 0.1530199944972992), float2(0.7104399800300598, 0.7962579727172852),
    float2(0.7826780080795288, 0.7365339994430542), float2(0.8307139873504639, 0.7723320126533508),
    float2(0.4948880076408386, 0.4970549941062927), float2(0.7148659825325012, 0.6980500221252441),
    float2(0.9317910075187683, 0.1991640031337738), float2(0.6378369927406311, 0.8089849948883057),
    float2(0.6657000184059143, 0.5995969772338867), float2(0.7531049847602844, 0.6726580262184143),
    float2(0.5937070250511169, 0.41236600279808044), float2(0.22888900339603424, 0.48976099491119385),
    float2(0.559099018573761, 0.10855399817228317), float2(0.6749169826507568, 0.3947260081768036),
    float2(0.7104399800300598, 0.46766600012779236), float2(0.10541100054979324, 0.22852300107479095),
    float2(0.5172579884529114, 0.10803599655628204), float2(0.9805899858474731, 0.546953022480011),
    float2(0.394665002822876, 0.8042240142822266), float2(0.8669999837875366, 0.16284699738025665),
    float2(0.8225650191307068, 0.6406139731407166), float2(0.8028500080108643, 0.25720998644828796),
    float2(0.44144999980926514, 0.2212589979171753), float2(0.959318995475769, 0.7075719833374023),
    float2(0.6282539963722229, 0.6852020025253296), float2(0.1322370022535324, 0.24506400525569916),
    float2(0.041200000792741776, 0.8709679841995239), float2(0.8260440230369568, 0.4974820017814636),
    float2(0.24625399708747864, 0.6735129952430725), float2(0.025544000789523125, 0.4378489851951599),
    float2(0.23813599348068237, 0.22241899371147156), float2(0.16843199729919434, 0.887050986289978),
    float2(0.2742390036582947, 0.33637499809265137), float2(0.35056599974632263, 0.8275700211524963),
    float2(0.20206299424171448, 0.5102999806404114), float2(0.3134250044822693, 0.9222080111503601),
    float2(0.05026400089263916, 0.7097989916801453), float2(0.36939600110054016, 0.5843070149421692),
    float2(0.07763910293579102, 0.4414199888706207), float2(0.4021419882774353, 0.7092499732971191),
    float2(0.2239139974117279, 0.7875909805297852), float2(0.9979860186576843, 0.9055449962615967),
    float2(0.4920800030231476, 0.7682120203971863), float2(0.4643999934196472, 0.04620499908924103),
    float2(0.5120700001716614, 0.9446390271186829), float2(0.6356390118598938, 0.6482740044593811),
    float2(0.8786889910697937, 0.25064900517463684), float2(0.27533799409866333, 0.1322370022535324),
    float2(0.8996549844741821, 0.7868279814720154), float2(0.30582600831985474, 0.7288129925727844),
    float2(0.9813230037689209, 0.8091070055961609), float2(0.32331299781799316, 0.7908869981765747),
    float2(0.5439310073852539, 0.3821530044078827), float2(0.2187259942293167, 0.6564840078353882),
    float2(0.5931580066680908, 0.6300240159034729), float2(0.14828899502754211, 0.2917569875717163),
    float2(0.9242839813232422, 0.038453299552202225), float2(0.5154579877853394, 0.3307900130748749),
    float2(0.16144299507141113, 0.0877406969666481), float2(0.9222999811172485, 0.5511339902877808),
    float2(0.07669299840927124, 0.32676199078559875), float2(0.1838739961385727, 0.5891600251197815),
    float2(0.4387040138244629, 0.387706995010376), float2(0.8076720237731934, 0.607990026473999),
    float2(0.9334390163421631, 0.9492779970169067), float2(0.7456279993057251, 0.28369998931884766),
    float2(0.8460950255393982, 0.9068880081176758), float2(0.8607749938964844, 0.06769009679555893),
    float2(0.5854060053825378, 0.1382180005311966), float2(0.8695639967918396, 0.9325540065765381),
    float2(0.7618640065193176, 0.4056209921836853), float2(0.3135170042514801, 0.6791890263557434),
    float2(0.5335549712181091, 0.05142369866371155), float2(0.20685400068759918, 0.11807599663734436),
    float2(0.38502201437950134, 0.7539600133895874), float2(0.4065069854259491, 0.9872130155563354),
    float2(0.7939079999923706, 0.09723199903964996), float2(0.5392929911613464, 0.993133008480072),
    float2(0.35935500264167786, 0.7271339893341064), float2(0.9175080060958862, 0.33622199296951294),
    float2(0.0383617989718914, 0.9344459772109985), float2(0.8032469749450684, 0.9429299831390381),
    float2(0.09891050308942795, 0.5130770206451416), float2(0.6351510286331177, 0.026551099494099617),
    float2(0.02212589979171753, 0.1808219999074936), float2(0.4687950015068054, 0.09451580047607422),
    float2(0.614031970500946, 0.5858640074729919), float2(0.47022899985313416, 0.31464600563049316),
    float2(0.707053005695343, 0.430525004863739), float2(0.03366189822554588, 0.7913450002670288),
    float2(0.44935500621795654, 0.8981900215148926), float2(0.8767359852790833, 0.2822049856185913),
    float2(0.17279599606990814, 0.5384079813957214), float2(0.25800299644470215, 0.4765770137310028),
    float2(0.5892509818077087, 0.8811910152435303), float2(0.34333300590515137, 0.5415509939193726),
    float2(0.25071001052856445, 0.4148379862308502), float2(0.27228599786758423, 0.8397780060768127),
    float2(0.5558339953422546, 0.9547110199928284), float2(0.013550199568271637, 0.6540729999542236),
    float2(0.3352150022983551, 0.6053959727287292), float2(0.10904300212860107, 0.5640429854393005),
    float2(0.12421000003814697, 0.16727200150489807), float2(0.3903929889202118, 0.9488199949264526),
    float2(0.8118230104446411, 0.03530989959836006), float2(0.665913999080658, 0.1921750009059906),
    float2(0.7866150140762329, 0.5288860201835632), float2(0.8380079865455627, 0.9941099882125854),
    float2(0.9034090042114258, 0.4408090114593506), float2(0.8189949989318848, 0.7336339950561523),
    float2(0.5269020199775696, 0.8884850144386292), float2(0.9153419733047485, 0.6300240159034729),
    float2(0.8071539998054504, 0.4390699863433838), float2(0.9585559964179993, 0.1530809998512268),
    float2(0.05508590117096901, 0.8280280232429504), float2(0.5516219735145569, 0.004120000172406435),
    float2(0.6189460158348083, 0.8395339846611023), float2(0.4622940123081207, 0.7839900255203247),
    float2(0.8262280225753784, 0.12442400306463242), float2(0.927702009677887, 0.09329509735107422),
    float2(0.5537580251693726, 0.26767799258232117), float2(0.03744620084762573, 0.352183997631073),
    float2(0.38016900420188904, 0.544327974319458), float2(0.8460339903831482, 0.6719570159912109),
    float2(0.7600020170211792, 0.2283090054988861), float2(0.4577470123767853, 0.9305700063705444),
    float2(0.839838981628418, 0.436598002910614), float2(0.2591629922389984, 0.8022710084915161),
    float2(0.9543439745903015, 0.9241920113563538), float2(0.7391279935836792, 0.03790400177240372),
    float2(0.195592999458313, 0.8325750231742859), float2(0.33365899324417114, 0.8562269806861877),
    float2(0.5727710127830505, 0.4569540023803711), float2(0.2591019868850708, 0.036316998302936554),
    float2(0.9794920086860657, 0.6032590270042419), float2(0.26157400012016296, 0.9602950215339661),
    float2(0.6637780070304871, 0.5244609713554382), float2(0.70100998878479, 0.882777988910675),
    float2(0.1817069947719574, 0.7600330114364624), float2(0.1148110032081604, 0.9175999760627747),
    float2(0.9108859896659851, 0.25141099095344543), float2(0.43717798590660095, 0.7649160027503967),
    float2(0.14874699711799622, 0.5772579908370972), float2(0.04138309881091118, 0.30802300572395325),
    float2(0.06421089917421341, 0.659017026424408), float2(0.06592000275850296, 0.9849849939346313),
    float2(0.9661549925804138, 0.022553199902176857), float2(0.2212589979171753, 0.4282050132751465),
    float2(0.9422590136528015, 0.29847100377082825), float2(0.5663629770278931, 0.526108980178833),
    float2(0.41761499643325806, 0.05960259959101677), float2(0.029572399333119392, 0.9823600053787231),
    float2(0.766319990158081, 0.9114660024642944), float2(0.00012207399413455278, 0.9023410081863403),
    float2(0.6978060007095337, 0.5262309908866882), float2(0.5988039970397949, 0.5335860252380371),
    float2(0.27808499336242676, 0.5724660158157349), float2(0.5938599705696106, 0.6913049817085266),
    float2(0.39493998885154724, 0.18863500654697418), float2(0.22229699790477753, 0.7057099938392639),
    float2(0.7985780239105225, 0.9938960075378418), float2(0.48066699504852295, 0.8187509775161743),
    float2(0.12372200191020966, 0.3670769929885864), float2(0.7100440263748169, 0.6486709713935852),
    float2(0.9430519938468933, 0.26413801312446594), float2(0.7608259916305542, 0.6033509969711304),
    float2(0.6530050039291382, 0.27854201197624207), float2(0.5258030295372009, 0.4099859893321991),
    float2(0.1334269940853119, 0.677174985408783), float2(0.7902460098266602, 0.2891629934310913),
    float2(0.256630003452301, 0.7044280171394348), float2(0.5891289710998535, 0.07733389735221863),
    float2(0.9811090230941772, 0.23142200708389282), float2(0.20831899344921112, 0.8974270224571228),
    float2(0.5684379935264587, 0.7473070025444031), float2(0.13040600717067719, 0.04840239882469177),
    float2(0.5238500237464905, 0.8460649847984314), float2(0.549485981464386, 0.6958829760551453),
    float2(0.3769649863243103, 0.42201000452041626), float2(0.4108709990978241, 0.6622520089149475),
    float2(0.2935880124568939, 0.055848900228738785), float2(0.4882049858570099, 0.965453028678894),
    float2(0.7264630198478699, 0.9537339806556702), float2(0.9837639927864075, 0.0706809014081955),
    float2(0.7990660071372986, 0.14807599782943726), float2(0.6926789879798889, 0.24274399876594543),
    float2(0.8107550144195557, 0.37058600783348083), float2(0.7613450288772583, 0.7735530138015747),
    float2(0.5843989849090576, 0.19711899757385254), float2(0.3822439908981323, 0.649586021900177),
    float2(0.5361189842224121, 0.5626090168952942), float2(0.1069369986653328, 0.7808769941329956),
    float2(0.04721210151910782, 0.5020599961280823), float2(0.7670519948005676, 0.25974300503730774),
    float2(0.21903100609779358, 0.269569993019104), float2(0.9999079704284668, 0.7610099911689758),
    float2(0.7401350140571594, 0.0054933298379182816), float2(0.9114959836006165, 0.0007019260083325207),
    float2(0.0054933298379182816, 0.11359000205993652), float2(0.746940016746521, 0.5364239811897278),
    float2(0.8309879899024963, 0.858119010925293), float2(0.9400010108947754, 0.46684199571609497),
    float2(0.46415600180625916, 0.7427589893341064), float2(0.3052160143852234, 0.11606200039386749),
    float2(0.2652060091495514, 0.6256899833679199), float2(0.3504140079021454, 0.7576829791069031),
    float2(0.5263530015945435, 0.22064900398254395), float2(0.43202000856399536, 0.835536003112793),
    float2(0.45939499139785767, 0.6797080039978027), float2(0.7512440085411072, 0.8253120183944702),
    float2(0.9889219999313354, 0.36979299783706665), float2(0.554764986038208, 0.9222080111503601),
    float2(0.13818800449371338, 0.33500200510025024), float2(0.06369210034608841, 0.6258739829063416),
    float2(0.7296370267868042, 0.9963380098342896), float2(0.8769490122795105, 0.5734729766845703),
    float2(0.30063799023628235, 0.15240900218486786), float2(0.41410601139068604, 0.27835899591445923),
    float2(0.4047060012817383, 0.4435249865055084), float2(0.5926390290260315, 0.9656059741973877),
    float2(0.16592900454998016, 0.39811399579048157), float2(0.40757501125335693, 0.3829460144042969),
    float2(0.14102600514888763, 0.9392679929733276), float2(0.31070899963378906, 0.44724899530410767),
    float2(0.9505910277366638, 0.8207039833068848), float2(0.185247004032135, 0.6207159757614136),
    float2(0.4216740131378174, 0.4699850082397461), float2(0.995727002620697, 0.4603720009326935),
    float2(0.6359750032424927, 0.06024349853396416), float2(0.6675620079040527, 0.7038180232048035),
    float2(0.9926760196685791, 0.50968998670578), float2(0.2269970029592514, 0.8265330195426941),
    float2(0.08923610299825668, 0.27405598759651184), float2(0.12225700169801712, 0.4274730086326599),
    float2(0.008758810348808765, 0.21646800637245178), float2(0.9691460132598877, 0.9648119807243347),
    float2(0.5273900032043457, 0.7440410256385803), float2(0.376446008682251, 0.46232500672340393),
    float2(0.2663959860801697, 0.27372100949287415), float2(0.7247229814529419, 0.5046539902687073),
    float2(0.33017998933792114, 0.0569474995136261), float2(0.10275600105524063, 0.7081519961357117),
    float2(0.2038940042257309, 0.07797479629516602), float2(0.27274399995803833, 0.09881889820098877),
    float2(0.8383129835128784, 0.5476850271224976), float2(0.13641799986362457, 0.0021057799458503723),
    float2(0.1447490006685257, 0.8574180006980896), float2(0.36631399393081665, 0.30069899559020996),
    float2(0.8690450191497803, 0.36930400133132935), float2(0.33710700273513794, 0.36228498816490173),
    float2(0.6480000019073486, 0.992247998714447), float2(0.15732300281524658, 0.4629659950733185),
    float2(0.2579120099544525, 0.5161899924278259), float2(0.756401002407074, 0.36332300305366516),
    float2(0.07925660163164139, 0.863277018070221), float2(0.49708500504493713, 0.872829020023346),
    float2(0.7334210276603699, 0.2482379972934723), float2(0.8385570049285889, 0.33674100041389465),
    float2(0.701865017414093, 0.6125680208206177), float2(0.8915070295333862, 0.1351660043001175),
    float2(0.9291669726371765, 0.8591570258140564), float2(0.16385400295257568, 0.8232979774475098),
    float2(0.29578500986099243, 0.8687090277671814), float2(0.14365099370479584, 0.1382489949464798),
    float2(0.6873679757118225, 0.00015259299834724516), float2(0.19272400438785553, 0.19836999475955963),
    float2(0.4761190116405487, 0.005981630180031061), float2(0.0029603000730276108, 0.05365150049328804),
    float2(0.37595799565315247, 0.8765529990196228), float2(0.48942500352859497, 0.17853300273418427),
    float2(0.44901901483535767, 0.2751240134239197), float2(0.05920590087771416, 0.10687600076198578),
    float2(0.21671199798583984, 0.33851099014282227), float2(0.4308910071849823, 0.3313089907169342),
    float2(0.3969849944114685, 0.018188999965786934), float2(0.7869200110435486, 0.34943699836730957),
    float2(0.4629659950733185, 0.5937380194664001), float2(0.32071900367736816, 0.48463401198387146),
    float2(0.5358740091323853, 0.18234799802303314), float2(0.5571150183677673, 0.07528919726610184),
    float2(0.3522450029850006, 0.9188820123672485), float2(0.5332189798355103, 0.5303199887275696),
    float2(0.004028439987450838, 0.5356910228729248), float2(0.10589899867773056, 0.14139199256896973),
    float2(0.3878290057182312, 0.83992999792099), float2(0.9102150201797485, 0.7370830178260803),
    float2(0.9266639947891235, 0.9795830249786377), float2(0.6080809831619263, 0.8122199773788452),
    float2(0.9649649858474731, 0.27631500363349915), float2(0.14288799464702606, 0.44889700412750244),
    float2(0.7782829999923706, 0.9297770261764526), float2(0.8665729761123657, 0.9796749949455261),
    float2(0.6211429834365845, 0.03671380132436752), float2(0.14835000038146973, 0.32108500599861145),
    float2(0.39762601256370544, 0.52531498670578), float2(0.09006009995937347, 0.532243013381958),
    float2(0.5182650089263916, 0.5578479766845703), float2(0.9172340035438538, 0.6475110054016113),
    float2(0.4321730136871338, 0.5689259767532349), float2(0.20355799794197083, 0.2852570116519928),
    float2(0.7240210175514221, 0.09759820252656937), float2(0.7398300170898438, 0.652974009513855),
    float2(0.026337500661611557, 0.46317899227142334), float2(0.3448899984359741, 0.5820180177688599),
    float2(0.6165649890899658, 0.2599869966506958), float2(0.8989840149879456, 0.29755499958992004),
    float2(0.6288949847221375, 0.8738669753074646), float2(0.2642289996147156, 0.7735530138015747),
    float2(0.27021101117134094, 0.05664239823818207), float2(0.25159499049186707, 0.26065900921821594),
    float2(0.4109010100364685, 0.3521530032157898), float2(0.2665790021419525, 0.8811910152435303),
    float2(0.8006229996681213, 0.5765249729156494), float2(0.04422130063176155, 0.9715870022773743),
    float2(0.7536550164222717, 0.7961670160293579), float2(0.9815670251846313, 0.6637780070304871),
    float2(0.6142150163650513, 0.19577600061893463), float2(0.9745169878005981, 0.46595698595046997),
    float2(0.7259439826011658, 0.7727289795875549), float2(0.10940899699926376, 0.18436199426651),
    float2(0.3935059905052185, 0.9672539830207825), float2(0.6308789849281311, 0.1587270051240921),
    float2(0.07803580164909363, 0.71144700050354), float2(0.2816550135612488, 0.35581499338150024),
    float2(0.6762599945068359, 0.12686499953269958), float2(0.5257419943809509, 0.029328299686312675),
    float2(0.6664940118789673, 0.3791919946670532), float2(0.09625539928674698, 0.3855710029602051),
    float2(0.9055150151252747, 0.9863280057907104), float2(0.794031023979187, 0.7696769833564758),
    float2(0.5939819812774658, 0.6646019816398621), float2(0.24237799644470215, 0.7307350039482117),
    float2(0.34241798520088196, 0.7878350019454956), float2(0.27280500531196594, 0.9724720120429993),
    float2(0.7841730117797852, 0.975737988948822), float2(0.08322399854660034, 0.7509689927101135),
    float2(0.1539350003004074, 0.9025539755821228), float2(0.5869929790496826, 0.2986850142478943),
    float2(0.6508380174636841, 0.46540701389312744), float2(0.9307839870452881, 0.41453298926353455),
    float2(0.04864649847149849, 0.1587270051240921), float2(0.2348400056362152, 0.9785450100898743),
    float2(0.9290140271186829, 0.01223789993673563), float2(0.8074589967727661, 0.3382669985294342),
    float2(0.279092013835907, 0.4622940123081207), float2(0.8978850245475769, 0.3205359876155853),
    float2(0.16229699552059174, 0.5076749920845032), float2(0.024140100926160812, 0.8540909886360168),
    float2(0.32322201132774353, 0.6404920220375061), float2(0.774528980255127, 0.44495999813079834),
    float2(0.583361029624939, 0.5878170132637024), float2(0.23020100593566895, 0.5282449722290039),
    float2(0.7177950143814087, 0.5284280180931091), float2(0.895779013633728, 0.9174169898033142),
    float2(0.05331579968333244, 0.08786279708147049), float2(0.6327099800109863, 0.2616350054740906),
    float2(0.7405930161476135, 0.10132099688053131), float2(0.7268900275230408, 0.81563800573349),
    float2(0.4935759902000427, 0.7398599982261658), float2(0.19110700488090515, 0.25623300671577454),
    float2(0.7097690105438232, 0.33460500836372375), float2(0.30463600158691406, 0.4738300144672394),
    float2(0.1699880063533783, 0.8445690274238586), float2(0.1622059941291809, 0.13837100565433502),
    float2(0.6375619769096375, 0.5273900032043457), float2(0.9686880111694336, 0.5222629904747009),
    float2(0.6437569856643677, 0.2951439917087555), float2(0.8762779831886292, 0.06378369778394699),
    float2(0.8490859866142273, 0.6306959986686707), float2(0.3152559995651245, 0.31974199414253235),
    float2(0.6399120092391968, 0.0016479999758303165), float2(0.7389140129089355, 0.8538159728050232),
    float2(0.834650993347168, 0.6953639984130859), float2(0.21842099726200104, 0.5859249830245972),
    float2(0.20313100516796112, 0.3199560046195984), float2(0.2605060040950775, 0.9350569844245911),
    float2(0.5032200217247009, 0.22409699857234955), float2(0.9806510210037231, 0.6869109869003296),
    float2(0.04492320120334625, 0.29142099618911743), float2(0.6318550109863281, 0.45756399631500244),
    float2(0.6268810033798218, 0.783715009689331), float2(0.4195989966392517, 0.6993619799613953),
    float2(0.0400707982480526, 0.055970899760723114), float2(0.03051850013434887, 0.9512010216712952),
    float2(0.9286779761314392, 0.929502010345459), float2(0.8272039890289307, 0.7477650046348572),
    float2(0.11105699837207794, 0.01834160089492798), float2(0.5337380170822144, 0.9626150131225586),
    float2(0.9068269729614258, 0.6102790236473083), float2(0.5822929739952087, 0.2507399916648865),
    float2(0.05746639892458916, 0.8055970072746277), float2(0.711355984210968, 0.3095490038394928),
    float2(0.3408310115337372, 0.9883419871330261), float2(0.8656880259513855, 0.1908629983663559),
    float2(0.28107500076293945, 0.8549759984016418), float2(0.673695981502533, 0.6842250227928162),
    float2(0.4851219952106476, 0.8502150177955627), float2(0.12683500349521637, 0.11697699874639511),
    float2(0.38239699602127075, 0.7724850177764893), float2(0.14078199863433838, 0.4656510055065155),
    float2(0.3996399939060211, 0.7349159717559814), float2(0.925961971282959, 0.6934720277786255),
    float2(0.003295999951660633, 0.4524979889392853), float2(0.018677299842238426, 0.05279700085520744),
    float2(0.31504300236701965, 0.6172059774398804), float2(0.9683520197868347, 0.2957240045070648),
    float2(0.1404460072517395, 0.21460600197315216), float2(0.8244580030441284, 0.9115880131721497),
    float2(0.6701560020446777, 0.2635580003261566), float2(0.9408860206604004, 0.07382430136203766),
    float2(0.5609909892082214, 0.855434000492096), float2(0.9088109731674194, 0.40531599521636963),
    float2(0.04318369925022125, 0.0306100994348526), float2(0.8865630030632019, 0.5467389822006226),
    float2(0.5602589845657349, 0.3935360014438629), float2(0.7865840196609497, 0.6956999897956848),
    float2(0.9813230037689209, 0.3108919858932495), float2(0.741690993309021, 0.807000994682312),
    float2(0.7927489876747131, 0.9555649757385254), float2(0.03796499967575073, 0.1091340035200119),
    float2(0.5427719950675964, 0.3266089856624603), float2(0.2568129897117615, 0.2299260050058365),
    float2(0.5290690064430237, 0.12536999583244324), float2(0.15723100304603577, 0.21924500167369843),
    float2(0.5113679766654968, 0.6803799867630005), float2(0.8819540143013, 0.08236949890851974),
    float2(0.7788019776344299, 0.3316139876842499), float2(0.8931549787521362, 0.23706799745559692),
    float2(0.9986569881439209, 0.2539139986038208), float2(0.5540030002593994, 0.3552660048007965),
    float2(0.3447679877281189, 0.32279399037361145), float2(0.06250189989805222, 0.5236979722976685),
    float2(0.48634299635887146, 0.7996150255203247), float2(0.09692680090665817, 0.8869900107383728),
    float2(0.10077200084924698, 0.3481859862804413), float2(0.30857300758361816, 0.3744010031223297),
    float2(0.11847300082445145, 0.6935030221939087), float2(0.27414798736572266, 0.17810599505901337),
    float2(0.10101599991321564, 0.6044800281524658), float2(0.44770699739456177, 0.8116400241851807),
    float2(0.6609389781951904, 0.8439589738845825), float2(0.8148750066757202, 0.7059850096702576),
    float2(0.19531799852848053, 0.06024349853396416), float2(0.17032399773597717, 0.3525800108909607),
    float2(0.14594000577926636, 0.2640460133552551), float2(0.6514790058135986, 0.8976100087165833),
    float2(0.5121009945869446, 0.04525899887084961), float2(0.9422889947891235, 0.6454660296440125),
    float2(0.8371229767799377, 0.566057026386261), float2(0.6933190226554871, 0.7099519968032837),
    float2(0.22318199276924133, 0.2987760007381439), float2(0.776665985584259, 0.991424024105072),
    float2(0.6462900042533875, 0.8787189722061157), float2(0.013794399797916412, 0.06796470284461975),
    float2(0.5755490064620972, 0.8907129764556885), float2(0.6829130053520203, 0.5961490273475647),
    float2(0.6779080033302307, 0.17645800113677979), float2(0.3838619887828827, 0.20413799583911896),
    float2(0.039155200123786926, 0.65385901927948), float2(0.7220979928970337, 0.9296240210533142),
    float2(0.4055599868297577, 0.6443679928779602), float2(0.646992027759552, 0.7561569809913635),
    float2(0.9165930151939392, 0.11093500256538391), float2(0.7798699736595154, 0.5862910151481628),
    float2(0.5788750052452087, 0.7039700150489807), float2(0.1622059941291809, 0.6864219903945923),
    float2(0.5453659892082214, 0.5935239791870117), float2(0.6008179783821106, 0.9816280007362366),
    float2(0.4918060004711151, 0.32804301381111145), float2(0.3344219923019409, 0.036591701209545135),
    float2(0.437483012676239, 0.4643389880657196), float2(0.11142300069332123, 0.2599259912967682),
    float2(0.8161569833755493, 0.5559859871864319), float2(0.5656300187110901, 0.5763729810714722),
    float2(0.4595780074596405, 0.6119269728660583), float2(0.7170630097389221, 0.06869719922542572),
    float2(0.8241519927978516, 0.5289160013198853), float2(0.09192179888486862, 0.2417680025100708),
    float2(0.07708980143070221, 0.3779410123825073), float2(0.8162180185317993, 0.9664300084114075),
    float2(0.07837150245904922, 0.5922729969024658), float2(0.10809700191020966, 0.06491290032863617),
    float2(0.664205014705658, 0.9528489708900452), float2(0.6889550089836121, 0.5806760191917419),
    float2(0.06735440343618393, 0.2725299894809723), float2(0.02386550046503544, 0.7331770062446594),
    float2(0.8414869904518127, 0.9492779970169067), float2(0.9583420157432556, 0.4991300106048584),
    float2(0.2952969968318939, 0.9075899720191956), float2(0.4260689914226532, 0.6551409959793091),
    float2(0.3744930028915405, 0.8037049770355225), float2(0.45243701338768005, 0.7263100147247314),
    float2(0.770684003829956, 0.6922510266304016), float2(0.2409130036830902, 0.5623040199279785),
    float2(0.8374890089035034, 0.1417890042066574), float2(0.9472950100898743, 0.6683549880981445),
    float2(0.6531879901885986, 0.5027620196342468), float2(0.7993410229682922, 0.4907679855823517),
    float2(0.044373899698257446, 0.9018830060958862), float2(0.23279500007629395, 0.08410900086164474),
    float2(0.039277300238609314, 0.24481900036334991), float2(0.6735739707946777, 0.050630200654268265),
    float2(0.42893800139427185, 0.6259040236473083), float2(0.8502150177955627, 0.02804649993777275),
    float2(0.13934800028800964, 0.9736009836196899), float2(0.70973801612854, 0.1386760026216507),
    float2(0.7105010151863098, 0.24466699361801147), float2(0.806207001209259, 0.4577470123767853),
    float2(0.3526720106601715, 0.48011699318885803), float2(0.019013000652194023, 0.5774409770965576),
    float2(0.30634498596191406, 0.7099519968032837), float2(0.6133000254631042, 0.8608049750328064),
    float2(0.870693027973175, 0.6712549924850464), float2(0.1938840001821518, 0.1082490012049675),
    float2(0.8637959957122803, 0.11880899965763092), float2(0.6128730177879333, 0.6519359946250916),
    float2(0.740776002407074, 0.4344309866428375), float2(0.7747730016708374, 0.8911100029945374),
    float2(0.9734179973602295, 0.8720660209655762), float2(0.29666998982429504, 0.07504499703645706),
    float2(0.6775410175323486, 0.7664719820022583), float2(0.7274090051651001, 0.737326979637146),
    float2(0.231330007314682, 0.1134679988026619), float2(0.8655660152435303, 0.5137490034103394),
    float2(0.02783289924263954, 0.39231500029563904), float2(0.676351010799408, 0.0865200012922287),
    float2(0.08197270333766937, 0.12448500096797943), float2(0.7660449743270874, 0.9399700164794922),
    float2(0.4135870039463043, 0.81392902135849), float2(0.38462498784065247, 0.9191870093345642),
    float2(0.9143959879875183, 0.8536940217018127), float2(0.8333079814910889, 0.8886989951133728),
    float2(0.5034639835357666, 0.5336470007896423), float2(0.9488199949264526, 0.04211549833416939),
    float2(0.8899199962615967, 0.51670902967453), float2(0.029145199805498123, 0.1991640031337738),
    float2(0.6961269974708557, 0.9537950158119202), float2(0.6809290051460266, 0.8413649797439575),
    float2(0.4759669899940491, 0.42362698912620544), float2(0.8825039863586426, 0.4031189978122711),
    float2(0.48719701170921326, 0.6223949790000916), float2(0.29154300689697266, 0.28327301144599915),
    float2(0.41770699620246887, 0.8735920190811157), float2(0.5697810053825378, 0.5599539875984192),
    float2(0.7090370059013367, 0.819940984249115), float2(0.9451889991760254, 0.7791069746017456),
    float2(0.20664100348949432, 0.559831976890564), float2(0.15451499819755554, 0.8005009889602661),
    float2(0.761559009552002, 0.583666980266571), float2(0.8436229825019836, 0.5150610208511353),
    float2(0.9064610004425049, 0.36847999691963196), float2(0.7637559771537781, 0.1731320023536682),
    float2(0.3397020101547241, 0.2404550015926361), float2(0.9735710024833679, 0.1291240006685257),
    float2(0.5914790034294128, 0.32502201199531555), float2(0.7144380211830139, 0.22290700674057007),
    float2(0.28156399726867676, 0.014984600245952606), float2(0.37586599588394165, 0.157383993268013),
    float2(0.8169500231742859, 0.06500440090894699), float2(0.5927299857139587, 0.043763499706983566),
    float2(0.33323198556900024, 0.9622179865837097), float2(0.8095030188560486, 0.11258299648761749),
    float2(0.8926969766616821, 0.8699300289154053), float2(0.27539899945259094, 0.6830959916114807),
    float2(0.6876429915428162, 0.7784050107002258), float2(0.04742579907178879, 0.6855679750442505),
    float2(0.8386790156364441, 0.7967159748077393), float2(0.003295999951660633, 0.960204005241394),
    float2(0.4747759997844696, 0.7012540102005005), float2(0.6989960074424744, 0.40763598680496216),
    float2(0.46705499291419983, 0.9523910284042358), float2(0.009064000099897385, 0.7844780087471008),
    float2(0.7901549935340881, 0.8426160216331482), float2(0.7426679730415344, 0.9260839819908142),
    float2(0.9539480209350586, 0.08786279708147049), float2(0.7841119766235352, 0.3865779936313629),
    float2(0.4679099917411804, 0.6360059976577759), float2(0.2775050103664398, 0.7463300228118896),
    float2(0.9962769746780396, 0.017731299623847008), float2(0.6936249732971191, 0.015411799773573875),
    float2(0.16180899739265442, 0.27713900804519653), float2(0.7480700016021729, 0.5717949867248535),
    float2(0.38032200932502747, 0.30930501222610474), float2(0.26319199800491333, 0.0722372978925705),
    float2(0.12341699749231339, 0.736503005027771), float2(0.29456499218940735, 0.09595020115375519),
    float2(0.40058600902557373, 0.26285600662231445), float2(0.7446519732475281, 0.020508399233222008),
    float2(0.2360610067844391, 0.4338510036468506), float2(0.5066990256309509, 0.3561820089817047),
    float2(0.7633900046348572, 0.7488020062446594), float2(0.18716999888420105, 0.5315709710121155),
    float2(0.5283359885215759, 0.6516619920730591), float2(0.875361979007721, 0.8960540294647217),
    float2(0.3034459948539734, 0.9632560014724731), float2(0.28626400232315063, 0.6138489842414856),
    float2(0.41810399293899536, 0.3053379952907562), float2(0.6449170112609863, 0.386821985244751),
    float2(0.47199898958206177, 0.2539750039577484), float2(0.9628890156745911, 0.5816829800605774),
    float2(0.4387950003147125, 0.18417899310588837), float2(0.6507459878921509, 0.6331980228424072),
    float2(0.8930330276489258, 0.038544900715351105), float2(0.8532059788703918, 0.003906370140612125),
    float2(0.39274299144744873, 0.16058799624443054), float2(0.20053699612617493, 0.4065679907798767),
    float2(0.38416698575019836, 0.6161990165710449), float2(0.676351010799408, 0.10593000054359436),
    float2(0.061189599335193634, 0.7376319766044617), float2(0.5238810181617737, 0.6342049837112427),
    float2(0.07486189901828766, 0.8802449703216553), float2(0.2795189917087555, 0.6369820237159729),
    float2(0.9775689840316772, 0.5641040205955505), float2(0.6579790115356445, 0.7241430282592773),
    float2(0.587602972984314, 0.43284401297569275), float2(0.1535390019416809, 0.42844900488853455),
    float2(0.5446940064430237, 0.4000059962272644), float2(0.7756890058517456, 0.48335200548171997),
    float2(0.22074000537395477, 0.04196299985051155), float2(0.08465830236673355, 0.2912989854812622),
    float2(0.23642699420452118, 0.7124850153923035), float2(0.00299080996774137, 0.349590003490448),
    float2(0.9074069857597351, 0.7080289721488953), float2(0.8657799959182739, 0.7618029713630676),
    float2(0.7648239731788635, 0.8086180090904236), float2(0.3283179998397827, 0.34629398584365845),
    float2(0.3868829905986786, 0.0961942970752716), float2(0.6354870200157166, 0.5113070011138916),
    float2(0.2108519971370697, 0.5334029793739319), float2(0.8951690196990967, 0.9441210031509399),
    float2(0.905239999294281, 0.47059500217437744), float2(0.2443619966506958, 0.9996340274810791),
    float2(0.6532179713249207, 0.06213570013642311), float2(0.18250100314617157, 0.14465799927711487),
    float2(0.9402449727058411, 0.113926000893116), float2(0.8352310061454773, 0.8336129784584045),
    float2(0.7318639755249023, 0.8900110125541687), float2(0.4682759940624237, 0.8510090112686157),
    float2(0.7259439826011658, 0.050416599959135056), float2(0.02999969944357872, 0.5138710141181946),
    float2(0.6426889896392822, 0.8466749787330627), float2(0.115665003657341, 0.6367689967155457),
    float2(0.8600119948387146, 0.31769800186157227), float2(0.651295006275177, 0.15012100338935852),
    float2(0.14667199552059174, 0.48329100012779236), float2(0.002594070043414831, 0.080690898001194),
    float2(0.2742699980735779, 0.9473559856414795), float2(0.8565629720687866, 0.884456992149353),
    float2(0.6153450012207031, 0.5258949995040894), float2(0.6161689758300781, 0.0650349035859108),
    float2(0.04464859887957573, 0.7540209889411926), float2(0.6215699911117554, 0.610614001750946),
    float2(0.9099090099334717, 0.12802499532699585), float2(0.38581499457359314, 0.7100440263748169),
    float2(0.9608139991760254, 0.8434709906578064), float2(0.2586140036582947, 0.17401699721813202),
    float2(0.3295390009880066, 0.0877406969666481), float2(0.3620719909667969, 0.010986699722707272),
    float2(0.5771049857139587, 0.7944579720497131), float2(0.9248939752578735, 0.6160770058631897),
    float2(0.35148200392723083, 0.871487021446228), float2(0.1452070027589798, 0.08713030070066452),
    float2(0.26370999217033386, 0.9180880188941956), float2(0.9304180145263672, 0.21790200471878052),
    float2(0.03839230164885521, 0.8409990072250366), float2(0.3638420104980469, 0.9659109711647034),
    float2(0.7118750214576721, 0.9094820022583008), float2(0.5462200045585632, 0.9036229848861694),
    float2(0.7722709774971008, 0.5484790205955505), float2(0.06863609701395035, 0.23969200253486633),
    float2(0.5107269883155823, 0.6626480221748352), float2(0.8228399753570557, 0.200873002409935),
    float2(0.8471019864082336, 0.7287819981575012), float2(0.8039489984512329, 0.3090910017490387),
    float2(0.16592900454998016, 0.9992070198059082), float2(0.017029300332069397, 0.258338987827301),
    float2(0.14404700696468353, 0.6911529898643494), float2(0.30634498596191406, 0.8825950026512146),
    float2(0.2857449948787689, 0.8237559795379639), float2(0.9004179835319519, 0.21585699915885925),
    float2(0.7908260226249695, 0.6521810293197632), float2(0.542618989944458, 0.09350869804620743),
    float2(0.9871519804000854, 0.40418699383735657), float2(0.586778998374939, 0.546159029006958),
    float2(0.0012207400286570191, 0.14838099479675293), float2(0.3886840045452118, 0.04126099869608879),
    float2(0.30332300066947937, 0.6541950106620789), float2(0.2299869954586029, 0.4130989909172058),
    float2(0.08926659822463989, 0.05551319941878319), float2(0.05078279972076416, 0.44187700748443604),
    float2(0.16595999896526337, 0.9257789850234985), float2(0.3691520094871521, 0.43934398889541626),
    float2(0.9942010045051575, 0.8421279788017273), float2(0.6016420125961304, 0.2378309965133667),
    float2(0.2598649859428406, 0.31934601068496704), float2(0.31193000078201294, 0.09430219978094101),
    float2(0.8503680229187012, 0.5842159986495972), float2(0.4444110095500946, 0.9432049989700317),
    float2(0.8410900235176086, 0.6138190031051636), float2(0.7526469826698303, 0.8688920140266418),
    float2(0.8423110246658325, 0.4768820106983185), float2(0.9987789988517761, 0.5566580295562744),
    float2(0.7288429737091064, 0.2999970018863678), float2(0.41907998919487, 0.51139897108078),
    float2(0.006530960090458393, 0.19009999930858612), float2(0.777184009552002, 0.8656880259513855),
    float2(0.5524160265922546, 0.8083130121231079), float2(0.9157080054283142, 0.2674950063228607),
    float2(0.2386849969625473, 0.9572740197181702), float2(0.17767900228500366, 0.6733300089836121),
    float2(0.4201180040836334, 0.7552419900894165), float2(0.6901149749755859, 0.3751029968261719),
    float2(0.6461989879608154, 0.48582398891448975), float2(0.14795400202274323, 0.8742030262947083),
    float2(0.45377999544143677, 0.5587630271911621), float2(0.8760949969291687, 0.7765129804611206),
    float2(0.09781180322170258, 0.4535660147666931), float2(0.288796991109848, 0.7829520106315613),
    float2(0.5181130170822144, 0.9272440075874329), float2(0.5347149968147278, 0.8046209812164307),
    float2(0.6116520166397095, 0.1325719952583313), float2(0.39588600397109985, 0.3677789866924286),
    float2(0.07458720356225967, 0.7817320227622986), float2(0.09790340065956116, 0.7313460111618042),
    float2(0.47846901416778564, 0.6591389775276184), float2(0.7460860013961792, 0.17899100482463837),
    float2(0.8828089833259583, 0.659932017326355), float2(0.5023350119590759, 0.5522930026054382),
    float2(0.6184269785881042, 0.745140016078949), float2(0.03329569846391678, 0.2152779996395111),
    float2(0.8204290270805359, 0.9392380118370056), float2(0.2187259942293167, 0.9186379909515381),
    float2(0.9653000235557556, 0.9029819965362549), float2(0.41251900792121887, 0.32721900939941406),
    float2(0.5757930278778076, 0.11654999852180481), float2(0.4983980059623718, 0.8133180141448975),
    float2(0.8005620241165161, 0.8046510219573975), float2(0.014648900367319584, 0.7161779999732971),
    float2(0.28116700053215027, 0.2526319921016693), float2(0.46018901467323303, 0.9919739961624146),
    float2(0.8584250211715698, 0.48796001076698303), float2(0.4055910110473633, 0.2269359976053238),
    float2(0.6199529767036438, 0.34458398818969727), float2(0.22595900297164917, 0.7304610013961792),
    float2(0.8765529990196228, 0.38453298807144165), float2(0.7404710054397583, 0.5567190051078796),
    float2(0.33588701486587524, 0.9378029704093933), float2(0.6446120142936707, 0.5851920247077942),
    float2(0.09021270275115967, 0.8339790105819702), float2(0.7720270156860352, 0.8380690217018127),
    float2(0.6318249702453613, 0.5486310124397278), float2(0.5929440259933472, 0.9261149764060974),
    float2(0.6869109869003296, 0.8998990058898926), float2(0.655538022518158, 0.8112429976463318),
    float2(0.5086519718170166, 0.12631599605083466), float2(0.732109010219574, 0.20297899842262268),
    float2(0.5872679948806763, 0.01049839984625578), float2(0.12387499958276749, 0.8373969793319702),
    float2(0.5549790263175964, 0.20761699974536896), float2(0.0631427988409996, 0.17551200091838837),
    float2(0.5942869782447815, 0.7750179767608643), float2(0.20181900262832642, 0.8092589974403381),
    float2(0.23673200607299805, 0.13409799337387085), float2(0.9876400232315063, 0.08883939683437347),
    float2(0.8188419938087463, 0.4270760118961334), float2(0.11001899838447571, 0.47312799096107483),
    float2(0.7257300019264221, 0.8386179804801941), float2(0.7529529929161072, 0.20108599960803986),
    float2(0.5498830080032349, 0.49897798895835876), float2(0.399152010679245, 0.7832580208778381),
    float2(0.44984298944473267, 0.3073819875717163), float2(0.3033849895000458, 0.014557300135493279),
    float2(0.4429759979248047, 0.3716540038585663), float2(0.4494459927082062, 0.8466749787330627),
};

#endif
// FO4RE_INLINE_INCLUDE_END shadow_poisson_kernel.hlsli
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

        float centreDepth = g_tCascadeShadowRaw.Sample(
            g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
        float centreLit = (centreDepth >= zRef) ? 1.0 : 0.0;

        float averageBlocker = blocker.x / blocker.y;
        float worldRange = cascadeScale.w - cascadeScale.z;
        float receiverWorld = worldRange * zRef + cascadeScale.z;
        float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
        float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
        float penumbra = (blockerWorld < cascadeScale.z + 0.001)
            ? 1.9
            : (separation * 1.8 + 0.1);

        float sum = centreLit;
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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

#ifdef FILTER_POISSON
// FO4RE_INLINE_INCLUDE_BEGIN shadow_poisson_kernel.hlsli

#ifndef SHADOW_POISSON_KERNEL_HLSLI_INCLUDED
#define SHADOW_POISSON_KERNEL_HLSLI_INCLUDED

#define SHADOW_POISSON_KERNEL_SIZE 1000

static const float2 SHADOW_POISSON_KERNEL[SHADOW_POISSON_KERNEL_SIZE] =
{
    float2(0.4933930039405823, 0.3942689895629883), float2(0.7985470294952393, 0.8859220147132874),
    float2(0.2473219931125641, 0.9264500141143799), float2(0.051454201340675354, 0.14078199863433838),
    float2(0.8318430185317993, 0.009552289731800556), float2(0.428631991147995, 0.017151400446891785),
    float2(0.01565600000321865, 0.7497789859771729), float2(0.7583850026130676, 0.4961700141429901),
    float2(0.2234870046377182, 0.5621510148048401), float2(0.011627599596977234, 0.4069949984550476),
    float2(0.24146200716495514, 0.30463600158691406), float2(0.430310994386673, 0.7272260189056396),
    float2(0.981810986995697, 0.27835899591445923), float2(0.4070560038089752, 0.5005339980125427),
    float2(0.123478002846241, 0.4635460078716278), float2(0.8095340132713318, 0.6822720170021057),
    float2(0.6758019924163818, 0.6539199948310852), float2(0.23801399767398834, 0.06933809816837311),
    float2(0.0006714069750159979, 0.6111029982566833), float2(0.6218760013580322, 0.4990389943122864),
    float2(0.7128819823265076, 0.11529900133609772), float2(0.9136630296707153, 0.8193910121917725),
    float2(0.29545000195503235, 0.8096870183944702), float2(0.9850149750709534, 0.1178010031580925),
    float2(0.6307569742202759, 0.31321099400520325), float2(0.362621009349823, 0.1857050061225891),
    float2(0.16446399688720703, 0.7875909805297852), float2(0.0038453300949186087, 0.9388409852981567),
    float2(0.5227519869804382, 0.14627499878406525), float2(0.9875180125236511, 0.9389939904212952),
    float2(0.7701039910316467, 0.3155309855937958), float2(0.04483170062303543, 0.26883798837661743),
    float2(0.972320020198822, 0.43855100870132446), float2(0.6903589963912964, 0.9747310280799866),
    float2(0.5827199816703796, 0.8335520029067993), float2(0.49678000807762146, 0.9989929795265198),
    float2(0.498214989900589, 0.6034730076789856), float2(0.9164400100708008, 0.5907769799232483),
    float2(0.851131021976471, 0.21952000260353088), float2(0.4134649932384491, 0.893123984336853),
    float2(0.004425180144608021, 0.015686500817537308), float2(0.5808889865875244, 0.027405599132180214),
    float2(0.09085360169410706, 0.36497101187705994), float2(0.9074980020523071, 0.3878290057182312),
    float2(0.1073639988899231, 0.7465130090713501), float2(0.9870910048484802, 0.18369099497795105),
    float2(0.30414700508117676, 0.5427410006523132), float2(0.7691270112991333, 0.022675300016999245),
    float2(0.8954439759254456, 0.05838190019130707), float2(0.70967698097229, 0.20316199958324432),
    float2(0.4203920066356659, 0.4147160053253174), float2(0.8330940008163452, 0.15762799978256226),
    float2(0.29096299409866333, 0.19553199410438538), float2(0.4844200015068054, 0.9089329838752747),
    float2(0.7604910135269165, 0.9561449885368347), float2(0.03717150166630745, 0.5517749786376953),
    float2(0.14200299978256226, 0.19510500133037567), float2(0.9505599737167358, 0.7496259808540344),
    float2(0.36411601305007935, 0.7906429767608643), float2(0.22901099920272827, 0.8579360246658325),
    float2(0.7427290081977844, 0.7322310209274292), float2(0.7128509879112244, 0.3787960112094879),
    float2(0.34632399678230286, 0.4421829879283905), float2(0.4817650020122528, 0.22287699580192566),
    float2(0.6732990145683289, 0.5668200254440308), float2(0.0006408889894373715, 0.3230080008506775),
    float2(0.8751789927482605, 0.32813501358032227), float2(0.390514999628067, 0.3244419991970062),
    float2(0.9904170036315918, 0.6501359939575195), float2(0.35621199011802673, 0.9518420100212097),
    float2(0.43202000856399536, 0.796563982963562), float2(0.6701859831809998, 0.44901901483535767),
    float2(0.6072880029678345, 0.7214270234107971), float2(0.13770000636577606, 0.5319679975509644),
    float2(0.7076939940452576, 0.8413950204849243), float2(0.8066959977149963, 0.8207039833068848),
    float2(0.6437270045280457, 0.10141299664974213), float2(0.2517470121383667, 0.00012207399413455278),
    float2(0.5584890246391296, 0.4125489890575409), float2(0.5076450109481812, 0.00634784996509552),
    float2(0.8238170146942139, 0.40803200006484985), float2(0.3017059862613678, 0.35984399914741516),
    float2(0.30072900652885437, 0.6263920068740845), float2(0.9513229727745056, 0.5352029800415039),
    float2(0.11658100038766861, 0.8782010078430176), float2(0.3677479922771454, 0.0460829995572567),
    float2(0.2561720013618469, 0.7403180003166199), float2(0.16229699552059174, 0.9830009937286377),
    float2(0.5321210026741028, 0.4979709982872009), float2(0.20673200488090515, 0.23987500369548798),
    float2(0.10248100012540817, 0.6265760064125061), float2(0.34681200981140137, 0.6882839798927307),
    float2(0.903531014919281, 0.6722310185432434), float2(0.17407800257205963, 0.12005999684333801),
    float2(0.31723999977111816, 0.285317987203598), float2(0.09793390333652496, 0.9796140193939209),
    float2(0.19544099271297455, 0.38596799969673157), float2(0.11398699879646301, 0.300942987203598),
    float2(0.8307440280914307, 0.5856199860572815), float2(0.5628529787063599, 0.6620380282402039),
    float2(0.3735159933567047, 0.11416999995708466), float2(0.887935996055603, 0.9788510203361511),
    float2(0.9789119958877563, 0.8495740294456482), float2(0.5026400089263916, 0.06869719922542572),
    float2(0.1686760038137436, 0.05017239972949028), float2(0.8659319877624512, 0.5303509831428528),
    float2(0.9236429929733276, 0.1647389978170395), float2(0.04965360090136528, 0.2052370011806488),
    float2(0.8231760263442993, 0.0799890011548996), float2(0.024811500683426857, 0.6864219903945923),
    float2(0.8720660209655762, 0.871882975101471), float2(0.6138799786567688, 0.991362988948822),
    float2(0.09607230126857758, 0.0949430987238884), float2(0.8258919715881348, 0.2834559977054596),
    float2(0.18890999257564545, 0.4492020010948181), float2(0.6251720190048218, 0.9024930000305176),
    float2(0.5876340270042419, 0.5638599991798401), float2(0.012054800055921078, 0.4879299998283386),
    float2(0.3265480101108551, 0.8919640183448792), float2(0.9322789907455444, 0.8912010192871094),
    float2(0.49372801184654236, 0.6959750056266785), float2(0.6568499803543091, 0.780144989490509),
    float2(0.47056499123573303, 0.46147000789642334), float2(0.3726620078086853, 0.25141099095344543),
    float2(0.8736839890480042, 0.4529249966144562), float2(0.1747490018606186, 0.6543470025062561),
    float2(0.6952120065689087, 0.3021329939365387), float2(0.09561450034379959, 0.8131960034370422),
    float2(0.034150201827287674, 0.07660149782896042), float2(0.06680499762296677, 0.9160130023956299),
    float2(0.23401600122451782, 0.6280710101127625), float2(0.616595983505249, 0.376446008682251),
    float2(0.5639820098876953, 0.22901099920272827), float2(0.010223699733614922, 0.865444004535675),
    float2(0.41499099135398865, 0.6073489785194397), float2(0.631397008895874, 0.2421029955148697),
    float2(0.07110810279846191, 0.002563549904152751), float2(0.8778039813041687, 0.7384870052337646),
    float2(0.9971920251846313, 0.03616439923644066), float2(0.4361099898815155, 0.11423099786043167),
    float2(0.9591969847679138, 0.33796200156211853), float2(0.7051299810409546, 0.04675440117716789),
    float2(0.18008999526500702, 0.3244119882583618), float2(0.6102179884910583, 0.1621749997138977),
    float2(0.5471659898757935, 0.300942987203598), float2(0.18503400683403015, 0.9186990261077881),
    float2(0.4466080069541931, 0.9603869915008545), float2(0.954069972038269, 0.9948729872703552),
    float2(0.33497101068496704, 0.166935995221138), float2(0.3333840072154999, 0.21280600130558014),
    float2(0.4661700129508972, 0.5096290111541748), float2(0.36365899443626404, 0.34330299496650696),
    float2(0.17191100120544434, 0.17108699679374695), float2(0.759880006313324, 0.4608910083770752),
    float2(0.2912079989910126, 0.9834280014038086), float2(0.7586290240287781, 0.10745599865913391),
    float2(0.04507580026984215, 0.596759021282196), float2(0.9029819965362549, 0.5074009895324707),
    float2(0.5965149998664856, 0.7915890216827393), float2(0.17499299347400665, 0.24253100156784058),
    float2(0.4685809910297394, 0.5535449981689453), float2(0.8664510250091553, 0.6336860060691833),
    float2(0.6723840236663818, 0.06936860084533691), float2(0.23993700742721558, 0.18555299937725067),
    float2(0.6918849945068359, 0.7355570197105408), float2(0.6443979740142822, 0.7349770069122314),
    float2(0.41938498616218567, 0.558152973651886), float2(0.4960170090198517, 0.43458399176597595),
    float2(0.6652119755744934, 0.913815975189209), float2(0.2779630124568939, 0.765434980392456),
    float2(0.08548229932785034, 0.17151400446891785), float2(0.30582600831985474, 0.39497101306915283),
    float2(0.7080289721488953, 0.5744190216064453), float2(0.7287819981575012, 0.16055800020694733),
    float2(0.1861020028591156, 0.28904101252555847), float2(0.6779379844665527, 0.14361999928951263),
    float2(0.14032399654388428, 0.7077850103378296), float2(0.05908380076289177, 0.760308027267456),
    float2(0.6109809875488281, 0.4517959952354431), float2(0.03625600039958954, 0.007629630155861378),
    float2(0.9473249912261963, 0.4029659926891327), float2(0.19592900574207306, 0.6829130053520203),
    float2(0.057710498571395874, 0.46809300780296326), float2(0.5917540192604065, 0.2777490019798279),
    float2(0.7339699864387512, 0.6210520267486572), float2(0.9282199740409851, 0.7740709781646729),
    float2(0.8901939988136292, 0.18771900236606598), float2(0.740805983543396, 0.0805383026599884),
    float2(0.7708979845046997, 0.5667589902877808), float2(0.480087012052536, 0.13376300036907196),
    float2(0.33899998664855957, 0.016968300566077232), float2(0.8547930121421814, 0.8099920153617859),
    float2(0.5144199728965759, 0.2596510052680969), float2(0.1364479959011078, 0.6233410239219666),
    float2(0.3690600097179413, 0.5036159753799438), float2(0.33542901277542114, 0.6557819843292236),
    float2(0.5450000166893005, 0.6102790236473083), float2(0.9918820261955261, 0.7283239960670471),
    float2(0.27637600898742676, 0.8976100087165833), float2(0.6288340091705322, 0.9483630061149597),
    float2(0.5381019711494446, 0.7844169735908508), float2(0.06775110214948654, 0.057588398456573486),
    float2(0.09720149636268616, 0.035279400646686554), float2(0.4394359886646271, 0.6425979733467102),
    float2(0.2509230077266693, 0.3704639971256256), float2(0.45289498567581177, 0.17441299557685852),
    float2(0.20642699301242828, 0.7395550012588501), float2(0.4592120051383972, 0.3491320013999939),
    float2(0.8646810054779053, 0.020020099356770515), float2(0.6320689916610718, 0.18671199679374695),
    float2(0.792352020740509, 0.21115800738334656), float2(0.09134189784526825, 0.40156298875808716),
    float2(0.3082979917526245, 0.2425609976053238), float2(0.5730460286140442, 0.3318580090999603),
    float2(0.5030670166015625, 0.6413159966468811), float2(0.665058970451355, 0.8760030269622803),
    float2(0.2581869959831238, 0.5482649803161621), float2(0.873134970664978, 0.6994839906692505),
    float2(0.3422960042953491, 0.11078199744224548), float2(0.2691729962825775, 0.21857400238513947),
    float2(0.07446520030498505, 0.5483570098876953), float2(0.35157299041748047, 0.40360701084136963),
    float2(0.6602979898452759, 0.3413800001144409), float2(0.86285001039505, 0.09915459901094437),
    float2(0.20813600718975067, 0.952817976474762), float2(0.19370099902153015, 0.029023099690675735),
    float2(0.40882599353790283, 0.1530199944972992), float2(0.7104399800300598, 0.7962579727172852),
    float2(0.7826780080795288, 0.7365339994430542), float2(0.8307139873504639, 0.7723320126533508),
    float2(0.4948880076408386, 0.4970549941062927), float2(0.7148659825325012, 0.6980500221252441),
    float2(0.9317910075187683, 0.1991640031337738), float2(0.6378369927406311, 0.8089849948883057),
    float2(0.6657000184059143, 0.5995969772338867), float2(0.7531049847602844, 0.6726580262184143),
    float2(0.5937070250511169, 0.41236600279808044), float2(0.22888900339603424, 0.48976099491119385),
    float2(0.559099018573761, 0.10855399817228317), float2(0.6749169826507568, 0.3947260081768036),
    float2(0.7104399800300598, 0.46766600012779236), float2(0.10541100054979324, 0.22852300107479095),
    float2(0.5172579884529114, 0.10803599655628204), float2(0.9805899858474731, 0.546953022480011),
    float2(0.394665002822876, 0.8042240142822266), float2(0.8669999837875366, 0.16284699738025665),
    float2(0.8225650191307068, 0.6406139731407166), float2(0.8028500080108643, 0.25720998644828796),
    float2(0.44144999980926514, 0.2212589979171753), float2(0.959318995475769, 0.7075719833374023),
    float2(0.6282539963722229, 0.6852020025253296), float2(0.1322370022535324, 0.24506400525569916),
    float2(0.041200000792741776, 0.8709679841995239), float2(0.8260440230369568, 0.4974820017814636),
    float2(0.24625399708747864, 0.6735129952430725), float2(0.025544000789523125, 0.4378489851951599),
    float2(0.23813599348068237, 0.22241899371147156), float2(0.16843199729919434, 0.887050986289978),
    float2(0.2742390036582947, 0.33637499809265137), float2(0.35056599974632263, 0.8275700211524963),
    float2(0.20206299424171448, 0.5102999806404114), float2(0.3134250044822693, 0.9222080111503601),
    float2(0.05026400089263916, 0.7097989916801453), float2(0.36939600110054016, 0.5843070149421692),
    float2(0.07763910293579102, 0.4414199888706207), float2(0.4021419882774353, 0.7092499732971191),
    float2(0.2239139974117279, 0.7875909805297852), float2(0.9979860186576843, 0.9055449962615967),
    float2(0.4920800030231476, 0.7682120203971863), float2(0.4643999934196472, 0.04620499908924103),
    float2(0.5120700001716614, 0.9446390271186829), float2(0.6356390118598938, 0.6482740044593811),
    float2(0.8786889910697937, 0.25064900517463684), float2(0.27533799409866333, 0.1322370022535324),
    float2(0.8996549844741821, 0.7868279814720154), float2(0.30582600831985474, 0.7288129925727844),
    float2(0.9813230037689209, 0.8091070055961609), float2(0.32331299781799316, 0.7908869981765747),
    float2(0.5439310073852539, 0.3821530044078827), float2(0.2187259942293167, 0.6564840078353882),
    float2(0.5931580066680908, 0.6300240159034729), float2(0.14828899502754211, 0.2917569875717163),
    float2(0.9242839813232422, 0.038453299552202225), float2(0.5154579877853394, 0.3307900130748749),
    float2(0.16144299507141113, 0.0877406969666481), float2(0.9222999811172485, 0.5511339902877808),
    float2(0.07669299840927124, 0.32676199078559875), float2(0.1838739961385727, 0.5891600251197815),
    float2(0.4387040138244629, 0.387706995010376), float2(0.8076720237731934, 0.607990026473999),
    float2(0.9334390163421631, 0.9492779970169067), float2(0.7456279993057251, 0.28369998931884766),
    float2(0.8460950255393982, 0.9068880081176758), float2(0.8607749938964844, 0.06769009679555893),
    float2(0.5854060053825378, 0.1382180005311966), float2(0.8695639967918396, 0.9325540065765381),
    float2(0.7618640065193176, 0.4056209921836853), float2(0.3135170042514801, 0.6791890263557434),
    float2(0.5335549712181091, 0.05142369866371155), float2(0.20685400068759918, 0.11807599663734436),
    float2(0.38502201437950134, 0.7539600133895874), float2(0.4065069854259491, 0.9872130155563354),
    float2(0.7939079999923706, 0.09723199903964996), float2(0.5392929911613464, 0.993133008480072),
    float2(0.35935500264167786, 0.7271339893341064), float2(0.9175080060958862, 0.33622199296951294),
    float2(0.0383617989718914, 0.9344459772109985), float2(0.8032469749450684, 0.9429299831390381),
    float2(0.09891050308942795, 0.5130770206451416), float2(0.6351510286331177, 0.026551099494099617),
    float2(0.02212589979171753, 0.1808219999074936), float2(0.4687950015068054, 0.09451580047607422),
    float2(0.614031970500946, 0.5858640074729919), float2(0.47022899985313416, 0.31464600563049316),
    float2(0.707053005695343, 0.430525004863739), float2(0.03366189822554588, 0.7913450002670288),
    float2(0.44935500621795654, 0.8981900215148926), float2(0.8767359852790833, 0.2822049856185913),
    float2(0.17279599606990814, 0.5384079813957214), float2(0.25800299644470215, 0.4765770137310028),
    float2(0.5892509818077087, 0.8811910152435303), float2(0.34333300590515137, 0.5415509939193726),
    float2(0.25071001052856445, 0.4148379862308502), float2(0.27228599786758423, 0.8397780060768127),
    float2(0.5558339953422546, 0.9547110199928284), float2(0.013550199568271637, 0.6540729999542236),
    float2(0.3352150022983551, 0.6053959727287292), float2(0.10904300212860107, 0.5640429854393005),
    float2(0.12421000003814697, 0.16727200150489807), float2(0.3903929889202118, 0.9488199949264526),
    float2(0.8118230104446411, 0.03530989959836006), float2(0.665913999080658, 0.1921750009059906),
    float2(0.7866150140762329, 0.5288860201835632), float2(0.8380079865455627, 0.9941099882125854),
    float2(0.9034090042114258, 0.4408090114593506), float2(0.8189949989318848, 0.7336339950561523),
    float2(0.5269020199775696, 0.8884850144386292), float2(0.9153419733047485, 0.6300240159034729),
    float2(0.8071539998054504, 0.4390699863433838), float2(0.9585559964179993, 0.1530809998512268),
    float2(0.05508590117096901, 0.8280280232429504), float2(0.5516219735145569, 0.004120000172406435),
    float2(0.6189460158348083, 0.8395339846611023), float2(0.4622940123081207, 0.7839900255203247),
    float2(0.8262280225753784, 0.12442400306463242), float2(0.927702009677887, 0.09329509735107422),
    float2(0.5537580251693726, 0.26767799258232117), float2(0.03744620084762573, 0.352183997631073),
    float2(0.38016900420188904, 0.544327974319458), float2(0.8460339903831482, 0.6719570159912109),
    float2(0.7600020170211792, 0.2283090054988861), float2(0.4577470123767853, 0.9305700063705444),
    float2(0.839838981628418, 0.436598002910614), float2(0.2591629922389984, 0.8022710084915161),
    float2(0.9543439745903015, 0.9241920113563538), float2(0.7391279935836792, 0.03790400177240372),
    float2(0.195592999458313, 0.8325750231742859), float2(0.33365899324417114, 0.8562269806861877),
    float2(0.5727710127830505, 0.4569540023803711), float2(0.2591019868850708, 0.036316998302936554),
    float2(0.9794920086860657, 0.6032590270042419), float2(0.26157400012016296, 0.9602950215339661),
    float2(0.6637780070304871, 0.5244609713554382), float2(0.70100998878479, 0.882777988910675),
    float2(0.1817069947719574, 0.7600330114364624), float2(0.1148110032081604, 0.9175999760627747),
    float2(0.9108859896659851, 0.25141099095344543), float2(0.43717798590660095, 0.7649160027503967),
    float2(0.14874699711799622, 0.5772579908370972), float2(0.04138309881091118, 0.30802300572395325),
    float2(0.06421089917421341, 0.659017026424408), float2(0.06592000275850296, 0.9849849939346313),
    float2(0.9661549925804138, 0.022553199902176857), float2(0.2212589979171753, 0.4282050132751465),
    float2(0.9422590136528015, 0.29847100377082825), float2(0.5663629770278931, 0.526108980178833),
    float2(0.41761499643325806, 0.05960259959101677), float2(0.029572399333119392, 0.9823600053787231),
    float2(0.766319990158081, 0.9114660024642944), float2(0.00012207399413455278, 0.9023410081863403),
    float2(0.6978060007095337, 0.5262309908866882), float2(0.5988039970397949, 0.5335860252380371),
    float2(0.27808499336242676, 0.5724660158157349), float2(0.5938599705696106, 0.6913049817085266),
    float2(0.39493998885154724, 0.18863500654697418), float2(0.22229699790477753, 0.7057099938392639),
    float2(0.7985780239105225, 0.9938960075378418), float2(0.48066699504852295, 0.8187509775161743),
    float2(0.12372200191020966, 0.3670769929885864), float2(0.7100440263748169, 0.6486709713935852),
    float2(0.9430519938468933, 0.26413801312446594), float2(0.7608259916305542, 0.6033509969711304),
    float2(0.6530050039291382, 0.27854201197624207), float2(0.5258030295372009, 0.4099859893321991),
    float2(0.1334269940853119, 0.677174985408783), float2(0.7902460098266602, 0.2891629934310913),
    float2(0.256630003452301, 0.7044280171394348), float2(0.5891289710998535, 0.07733389735221863),
    float2(0.9811090230941772, 0.23142200708389282), float2(0.20831899344921112, 0.8974270224571228),
    float2(0.5684379935264587, 0.7473070025444031), float2(0.13040600717067719, 0.04840239882469177),
    float2(0.5238500237464905, 0.8460649847984314), float2(0.549485981464386, 0.6958829760551453),
    float2(0.3769649863243103, 0.42201000452041626), float2(0.4108709990978241, 0.6622520089149475),
    float2(0.2935880124568939, 0.055848900228738785), float2(0.4882049858570099, 0.965453028678894),
    float2(0.7264630198478699, 0.9537339806556702), float2(0.9837639927864075, 0.0706809014081955),
    float2(0.7990660071372986, 0.14807599782943726), float2(0.6926789879798889, 0.24274399876594543),
    float2(0.8107550144195557, 0.37058600783348083), float2(0.7613450288772583, 0.7735530138015747),
    float2(0.5843989849090576, 0.19711899757385254), float2(0.3822439908981323, 0.649586021900177),
    float2(0.5361189842224121, 0.5626090168952942), float2(0.1069369986653328, 0.7808769941329956),
    float2(0.04721210151910782, 0.5020599961280823), float2(0.7670519948005676, 0.25974300503730774),
    float2(0.21903100609779358, 0.269569993019104), float2(0.9999079704284668, 0.7610099911689758),
    float2(0.7401350140571594, 0.0054933298379182816), float2(0.9114959836006165, 0.0007019260083325207),
    float2(0.0054933298379182816, 0.11359000205993652), float2(0.746940016746521, 0.5364239811897278),
    float2(0.8309879899024963, 0.858119010925293), float2(0.9400010108947754, 0.46684199571609497),
    float2(0.46415600180625916, 0.7427589893341064), float2(0.3052160143852234, 0.11606200039386749),
    float2(0.2652060091495514, 0.6256899833679199), float2(0.3504140079021454, 0.7576829791069031),
    float2(0.5263530015945435, 0.22064900398254395), float2(0.43202000856399536, 0.835536003112793),
    float2(0.45939499139785767, 0.6797080039978027), float2(0.7512440085411072, 0.8253120183944702),
    float2(0.9889219999313354, 0.36979299783706665), float2(0.554764986038208, 0.9222080111503601),
    float2(0.13818800449371338, 0.33500200510025024), float2(0.06369210034608841, 0.6258739829063416),
    float2(0.7296370267868042, 0.9963380098342896), float2(0.8769490122795105, 0.5734729766845703),
    float2(0.30063799023628235, 0.15240900218486786), float2(0.41410601139068604, 0.27835899591445923),
    float2(0.4047060012817383, 0.4435249865055084), float2(0.5926390290260315, 0.9656059741973877),
    float2(0.16592900454998016, 0.39811399579048157), float2(0.40757501125335693, 0.3829460144042969),
    float2(0.14102600514888763, 0.9392679929733276), float2(0.31070899963378906, 0.44724899530410767),
    float2(0.9505910277366638, 0.8207039833068848), float2(0.185247004032135, 0.6207159757614136),
    float2(0.4216740131378174, 0.4699850082397461), float2(0.995727002620697, 0.4603720009326935),
    float2(0.6359750032424927, 0.06024349853396416), float2(0.6675620079040527, 0.7038180232048035),
    float2(0.9926760196685791, 0.50968998670578), float2(0.2269970029592514, 0.8265330195426941),
    float2(0.08923610299825668, 0.27405598759651184), float2(0.12225700169801712, 0.4274730086326599),
    float2(0.008758810348808765, 0.21646800637245178), float2(0.9691460132598877, 0.9648119807243347),
    float2(0.5273900032043457, 0.7440410256385803), float2(0.376446008682251, 0.46232500672340393),
    float2(0.2663959860801697, 0.27372100949287415), float2(0.7247229814529419, 0.5046539902687073),
    float2(0.33017998933792114, 0.0569474995136261), float2(0.10275600105524063, 0.7081519961357117),
    float2(0.2038940042257309, 0.07797479629516602), float2(0.27274399995803833, 0.09881889820098877),
    float2(0.8383129835128784, 0.5476850271224976), float2(0.13641799986362457, 0.0021057799458503723),
    float2(0.1447490006685257, 0.8574180006980896), float2(0.36631399393081665, 0.30069899559020996),
    float2(0.8690450191497803, 0.36930400133132935), float2(0.33710700273513794, 0.36228498816490173),
    float2(0.6480000019073486, 0.992247998714447), float2(0.15732300281524658, 0.4629659950733185),
    float2(0.2579120099544525, 0.5161899924278259), float2(0.756401002407074, 0.36332300305366516),
    float2(0.07925660163164139, 0.863277018070221), float2(0.49708500504493713, 0.872829020023346),
    float2(0.7334210276603699, 0.2482379972934723), float2(0.8385570049285889, 0.33674100041389465),
    float2(0.701865017414093, 0.6125680208206177), float2(0.8915070295333862, 0.1351660043001175),
    float2(0.9291669726371765, 0.8591570258140564), float2(0.16385400295257568, 0.8232979774475098),
    float2(0.29578500986099243, 0.8687090277671814), float2(0.14365099370479584, 0.1382489949464798),
    float2(0.6873679757118225, 0.00015259299834724516), float2(0.19272400438785553, 0.19836999475955963),
    float2(0.4761190116405487, 0.005981630180031061), float2(0.0029603000730276108, 0.05365150049328804),
    float2(0.37595799565315247, 0.8765529990196228), float2(0.48942500352859497, 0.17853300273418427),
    float2(0.44901901483535767, 0.2751240134239197), float2(0.05920590087771416, 0.10687600076198578),
    float2(0.21671199798583984, 0.33851099014282227), float2(0.4308910071849823, 0.3313089907169342),
    float2(0.3969849944114685, 0.018188999965786934), float2(0.7869200110435486, 0.34943699836730957),
    float2(0.4629659950733185, 0.5937380194664001), float2(0.32071900367736816, 0.48463401198387146),
    float2(0.5358740091323853, 0.18234799802303314), float2(0.5571150183677673, 0.07528919726610184),
    float2(0.3522450029850006, 0.9188820123672485), float2(0.5332189798355103, 0.5303199887275696),
    float2(0.004028439987450838, 0.5356910228729248), float2(0.10589899867773056, 0.14139199256896973),
    float2(0.3878290057182312, 0.83992999792099), float2(0.9102150201797485, 0.7370830178260803),
    float2(0.9266639947891235, 0.9795830249786377), float2(0.6080809831619263, 0.8122199773788452),
    float2(0.9649649858474731, 0.27631500363349915), float2(0.14288799464702606, 0.44889700412750244),
    float2(0.7782829999923706, 0.9297770261764526), float2(0.8665729761123657, 0.9796749949455261),
    float2(0.6211429834365845, 0.03671380132436752), float2(0.14835000038146973, 0.32108500599861145),
    float2(0.39762601256370544, 0.52531498670578), float2(0.09006009995937347, 0.532243013381958),
    float2(0.5182650089263916, 0.5578479766845703), float2(0.9172340035438538, 0.6475110054016113),
    float2(0.4321730136871338, 0.5689259767532349), float2(0.20355799794197083, 0.2852570116519928),
    float2(0.7240210175514221, 0.09759820252656937), float2(0.7398300170898438, 0.652974009513855),
    float2(0.026337500661611557, 0.46317899227142334), float2(0.3448899984359741, 0.5820180177688599),
    float2(0.6165649890899658, 0.2599869966506958), float2(0.8989840149879456, 0.29755499958992004),
    float2(0.6288949847221375, 0.8738669753074646), float2(0.2642289996147156, 0.7735530138015747),
    float2(0.27021101117134094, 0.05664239823818207), float2(0.25159499049186707, 0.26065900921821594),
    float2(0.4109010100364685, 0.3521530032157898), float2(0.2665790021419525, 0.8811910152435303),
    float2(0.8006229996681213, 0.5765249729156494), float2(0.04422130063176155, 0.9715870022773743),
    float2(0.7536550164222717, 0.7961670160293579), float2(0.9815670251846313, 0.6637780070304871),
    float2(0.6142150163650513, 0.19577600061893463), float2(0.9745169878005981, 0.46595698595046997),
    float2(0.7259439826011658, 0.7727289795875549), float2(0.10940899699926376, 0.18436199426651),
    float2(0.3935059905052185, 0.9672539830207825), float2(0.6308789849281311, 0.1587270051240921),
    float2(0.07803580164909363, 0.71144700050354), float2(0.2816550135612488, 0.35581499338150024),
    float2(0.6762599945068359, 0.12686499953269958), float2(0.5257419943809509, 0.029328299686312675),
    float2(0.6664940118789673, 0.3791919946670532), float2(0.09625539928674698, 0.3855710029602051),
    float2(0.9055150151252747, 0.9863280057907104), float2(0.794031023979187, 0.7696769833564758),
    float2(0.5939819812774658, 0.6646019816398621), float2(0.24237799644470215, 0.7307350039482117),
    float2(0.34241798520088196, 0.7878350019454956), float2(0.27280500531196594, 0.9724720120429993),
    float2(0.7841730117797852, 0.975737988948822), float2(0.08322399854660034, 0.7509689927101135),
    float2(0.1539350003004074, 0.9025539755821228), float2(0.5869929790496826, 0.2986850142478943),
    float2(0.6508380174636841, 0.46540701389312744), float2(0.9307839870452881, 0.41453298926353455),
    float2(0.04864649847149849, 0.1587270051240921), float2(0.2348400056362152, 0.9785450100898743),
    float2(0.9290140271186829, 0.01223789993673563), float2(0.8074589967727661, 0.3382669985294342),
    float2(0.279092013835907, 0.4622940123081207), float2(0.8978850245475769, 0.3205359876155853),
    float2(0.16229699552059174, 0.5076749920845032), float2(0.024140100926160812, 0.8540909886360168),
    float2(0.32322201132774353, 0.6404920220375061), float2(0.774528980255127, 0.44495999813079834),
    float2(0.583361029624939, 0.5878170132637024), float2(0.23020100593566895, 0.5282449722290039),
    float2(0.7177950143814087, 0.5284280180931091), float2(0.895779013633728, 0.9174169898033142),
    float2(0.05331579968333244, 0.08786279708147049), float2(0.6327099800109863, 0.2616350054740906),
    float2(0.7405930161476135, 0.10132099688053131), float2(0.7268900275230408, 0.81563800573349),
    float2(0.4935759902000427, 0.7398599982261658), float2(0.19110700488090515, 0.25623300671577454),
    float2(0.7097690105438232, 0.33460500836372375), float2(0.30463600158691406, 0.4738300144672394),
    float2(0.1699880063533783, 0.8445690274238586), float2(0.1622059941291809, 0.13837100565433502),
    float2(0.6375619769096375, 0.5273900032043457), float2(0.9686880111694336, 0.5222629904747009),
    float2(0.6437569856643677, 0.2951439917087555), float2(0.8762779831886292, 0.06378369778394699),
    float2(0.8490859866142273, 0.6306959986686707), float2(0.3152559995651245, 0.31974199414253235),
    float2(0.6399120092391968, 0.0016479999758303165), float2(0.7389140129089355, 0.8538159728050232),
    float2(0.834650993347168, 0.6953639984130859), float2(0.21842099726200104, 0.5859249830245972),
    float2(0.20313100516796112, 0.3199560046195984), float2(0.2605060040950775, 0.9350569844245911),
    float2(0.5032200217247009, 0.22409699857234955), float2(0.9806510210037231, 0.6869109869003296),
    float2(0.04492320120334625, 0.29142099618911743), float2(0.6318550109863281, 0.45756399631500244),
    float2(0.6268810033798218, 0.783715009689331), float2(0.4195989966392517, 0.6993619799613953),
    float2(0.0400707982480526, 0.055970899760723114), float2(0.03051850013434887, 0.9512010216712952),
    float2(0.9286779761314392, 0.929502010345459), float2(0.8272039890289307, 0.7477650046348572),
    float2(0.11105699837207794, 0.01834160089492798), float2(0.5337380170822144, 0.9626150131225586),
    float2(0.9068269729614258, 0.6102790236473083), float2(0.5822929739952087, 0.2507399916648865),
    float2(0.05746639892458916, 0.8055970072746277), float2(0.711355984210968, 0.3095490038394928),
    float2(0.3408310115337372, 0.9883419871330261), float2(0.8656880259513855, 0.1908629983663559),
    float2(0.28107500076293945, 0.8549759984016418), float2(0.673695981502533, 0.6842250227928162),
    float2(0.4851219952106476, 0.8502150177955627), float2(0.12683500349521637, 0.11697699874639511),
    float2(0.38239699602127075, 0.7724850177764893), float2(0.14078199863433838, 0.4656510055065155),
    float2(0.3996399939060211, 0.7349159717559814), float2(0.925961971282959, 0.6934720277786255),
    float2(0.003295999951660633, 0.4524979889392853), float2(0.018677299842238426, 0.05279700085520744),
    float2(0.31504300236701965, 0.6172059774398804), float2(0.9683520197868347, 0.2957240045070648),
    float2(0.1404460072517395, 0.21460600197315216), float2(0.8244580030441284, 0.9115880131721497),
    float2(0.6701560020446777, 0.2635580003261566), float2(0.9408860206604004, 0.07382430136203766),
    float2(0.5609909892082214, 0.855434000492096), float2(0.9088109731674194, 0.40531599521636963),
    float2(0.04318369925022125, 0.0306100994348526), float2(0.8865630030632019, 0.5467389822006226),
    float2(0.5602589845657349, 0.3935360014438629), float2(0.7865840196609497, 0.6956999897956848),
    float2(0.9813230037689209, 0.3108919858932495), float2(0.741690993309021, 0.807000994682312),
    float2(0.7927489876747131, 0.9555649757385254), float2(0.03796499967575073, 0.1091340035200119),
    float2(0.5427719950675964, 0.3266089856624603), float2(0.2568129897117615, 0.2299260050058365),
    float2(0.5290690064430237, 0.12536999583244324), float2(0.15723100304603577, 0.21924500167369843),
    float2(0.5113679766654968, 0.6803799867630005), float2(0.8819540143013, 0.08236949890851974),
    float2(0.7788019776344299, 0.3316139876842499), float2(0.8931549787521362, 0.23706799745559692),
    float2(0.9986569881439209, 0.2539139986038208), float2(0.5540030002593994, 0.3552660048007965),
    float2(0.3447679877281189, 0.32279399037361145), float2(0.06250189989805222, 0.5236979722976685),
    float2(0.48634299635887146, 0.7996150255203247), float2(0.09692680090665817, 0.8869900107383728),
    float2(0.10077200084924698, 0.3481859862804413), float2(0.30857300758361816, 0.3744010031223297),
    float2(0.11847300082445145, 0.6935030221939087), float2(0.27414798736572266, 0.17810599505901337),
    float2(0.10101599991321564, 0.6044800281524658), float2(0.44770699739456177, 0.8116400241851807),
    float2(0.6609389781951904, 0.8439589738845825), float2(0.8148750066757202, 0.7059850096702576),
    float2(0.19531799852848053, 0.06024349853396416), float2(0.17032399773597717, 0.3525800108909607),
    float2(0.14594000577926636, 0.2640460133552551), float2(0.6514790058135986, 0.8976100087165833),
    float2(0.5121009945869446, 0.04525899887084961), float2(0.9422889947891235, 0.6454660296440125),
    float2(0.8371229767799377, 0.566057026386261), float2(0.6933190226554871, 0.7099519968032837),
    float2(0.22318199276924133, 0.2987760007381439), float2(0.776665985584259, 0.991424024105072),
    float2(0.6462900042533875, 0.8787189722061157), float2(0.013794399797916412, 0.06796470284461975),
    float2(0.5755490064620972, 0.8907129764556885), float2(0.6829130053520203, 0.5961490273475647),
    float2(0.6779080033302307, 0.17645800113677979), float2(0.3838619887828827, 0.20413799583911896),
    float2(0.039155200123786926, 0.65385901927948), float2(0.7220979928970337, 0.9296240210533142),
    float2(0.4055599868297577, 0.6443679928779602), float2(0.646992027759552, 0.7561569809913635),
    float2(0.9165930151939392, 0.11093500256538391), float2(0.7798699736595154, 0.5862910151481628),
    float2(0.5788750052452087, 0.7039700150489807), float2(0.1622059941291809, 0.6864219903945923),
    float2(0.5453659892082214, 0.5935239791870117), float2(0.6008179783821106, 0.9816280007362366),
    float2(0.4918060004711151, 0.32804301381111145), float2(0.3344219923019409, 0.036591701209545135),
    float2(0.437483012676239, 0.4643389880657196), float2(0.11142300069332123, 0.2599259912967682),
    float2(0.8161569833755493, 0.5559859871864319), float2(0.5656300187110901, 0.5763729810714722),
    float2(0.4595780074596405, 0.6119269728660583), float2(0.7170630097389221, 0.06869719922542572),
    float2(0.8241519927978516, 0.5289160013198853), float2(0.09192179888486862, 0.2417680025100708),
    float2(0.07708980143070221, 0.3779410123825073), float2(0.8162180185317993, 0.9664300084114075),
    float2(0.07837150245904922, 0.5922729969024658), float2(0.10809700191020966, 0.06491290032863617),
    float2(0.664205014705658, 0.9528489708900452), float2(0.6889550089836121, 0.5806760191917419),
    float2(0.06735440343618393, 0.2725299894809723), float2(0.02386550046503544, 0.7331770062446594),
    float2(0.8414869904518127, 0.9492779970169067), float2(0.9583420157432556, 0.4991300106048584),
    float2(0.2952969968318939, 0.9075899720191956), float2(0.4260689914226532, 0.6551409959793091),
    float2(0.3744930028915405, 0.8037049770355225), float2(0.45243701338768005, 0.7263100147247314),
    float2(0.770684003829956, 0.6922510266304016), float2(0.2409130036830902, 0.5623040199279785),
    float2(0.8374890089035034, 0.1417890042066574), float2(0.9472950100898743, 0.6683549880981445),
    float2(0.6531879901885986, 0.5027620196342468), float2(0.7993410229682922, 0.4907679855823517),
    float2(0.044373899698257446, 0.9018830060958862), float2(0.23279500007629395, 0.08410900086164474),
    float2(0.039277300238609314, 0.24481900036334991), float2(0.6735739707946777, 0.050630200654268265),
    float2(0.42893800139427185, 0.6259040236473083), float2(0.8502150177955627, 0.02804649993777275),
    float2(0.13934800028800964, 0.9736009836196899), float2(0.70973801612854, 0.1386760026216507),
    float2(0.7105010151863098, 0.24466699361801147), float2(0.806207001209259, 0.4577470123767853),
    float2(0.3526720106601715, 0.48011699318885803), float2(0.019013000652194023, 0.5774409770965576),
    float2(0.30634498596191406, 0.7099519968032837), float2(0.6133000254631042, 0.8608049750328064),
    float2(0.870693027973175, 0.6712549924850464), float2(0.1938840001821518, 0.1082490012049675),
    float2(0.8637959957122803, 0.11880899965763092), float2(0.6128730177879333, 0.6519359946250916),
    float2(0.740776002407074, 0.4344309866428375), float2(0.7747730016708374, 0.8911100029945374),
    float2(0.9734179973602295, 0.8720660209655762), float2(0.29666998982429504, 0.07504499703645706),
    float2(0.6775410175323486, 0.7664719820022583), float2(0.7274090051651001, 0.737326979637146),
    float2(0.231330007314682, 0.1134679988026619), float2(0.8655660152435303, 0.5137490034103394),
    float2(0.02783289924263954, 0.39231500029563904), float2(0.676351010799408, 0.0865200012922287),
    float2(0.08197270333766937, 0.12448500096797943), float2(0.7660449743270874, 0.9399700164794922),
    float2(0.4135870039463043, 0.81392902135849), float2(0.38462498784065247, 0.9191870093345642),
    float2(0.9143959879875183, 0.8536940217018127), float2(0.8333079814910889, 0.8886989951133728),
    float2(0.5034639835357666, 0.5336470007896423), float2(0.9488199949264526, 0.04211549833416939),
    float2(0.8899199962615967, 0.51670902967453), float2(0.029145199805498123, 0.1991640031337738),
    float2(0.6961269974708557, 0.9537950158119202), float2(0.6809290051460266, 0.8413649797439575),
    float2(0.4759669899940491, 0.42362698912620544), float2(0.8825039863586426, 0.4031189978122711),
    float2(0.48719701170921326, 0.6223949790000916), float2(0.29154300689697266, 0.28327301144599915),
    float2(0.41770699620246887, 0.8735920190811157), float2(0.5697810053825378, 0.5599539875984192),
    float2(0.7090370059013367, 0.819940984249115), float2(0.9451889991760254, 0.7791069746017456),
    float2(0.20664100348949432, 0.559831976890564), float2(0.15451499819755554, 0.8005009889602661),
    float2(0.761559009552002, 0.583666980266571), float2(0.8436229825019836, 0.5150610208511353),
    float2(0.9064610004425049, 0.36847999691963196), float2(0.7637559771537781, 0.1731320023536682),
    float2(0.3397020101547241, 0.2404550015926361), float2(0.9735710024833679, 0.1291240006685257),
    float2(0.5914790034294128, 0.32502201199531555), float2(0.7144380211830139, 0.22290700674057007),
    float2(0.28156399726867676, 0.014984600245952606), float2(0.37586599588394165, 0.157383993268013),
    float2(0.8169500231742859, 0.06500440090894699), float2(0.5927299857139587, 0.043763499706983566),
    float2(0.33323198556900024, 0.9622179865837097), float2(0.8095030188560486, 0.11258299648761749),
    float2(0.8926969766616821, 0.8699300289154053), float2(0.27539899945259094, 0.6830959916114807),
    float2(0.6876429915428162, 0.7784050107002258), float2(0.04742579907178879, 0.6855679750442505),
    float2(0.8386790156364441, 0.7967159748077393), float2(0.003295999951660633, 0.960204005241394),
    float2(0.4747759997844696, 0.7012540102005005), float2(0.6989960074424744, 0.40763598680496216),
    float2(0.46705499291419983, 0.9523910284042358), float2(0.009064000099897385, 0.7844780087471008),
    float2(0.7901549935340881, 0.8426160216331482), float2(0.7426679730415344, 0.9260839819908142),
    float2(0.9539480209350586, 0.08786279708147049), float2(0.7841119766235352, 0.3865779936313629),
    float2(0.4679099917411804, 0.6360059976577759), float2(0.2775050103664398, 0.7463300228118896),
    float2(0.9962769746780396, 0.017731299623847008), float2(0.6936249732971191, 0.015411799773573875),
    float2(0.16180899739265442, 0.27713900804519653), float2(0.7480700016021729, 0.5717949867248535),
    float2(0.38032200932502747, 0.30930501222610474), float2(0.26319199800491333, 0.0722372978925705),
    float2(0.12341699749231339, 0.736503005027771), float2(0.29456499218940735, 0.09595020115375519),
    float2(0.40058600902557373, 0.26285600662231445), float2(0.7446519732475281, 0.020508399233222008),
    float2(0.2360610067844391, 0.4338510036468506), float2(0.5066990256309509, 0.3561820089817047),
    float2(0.7633900046348572, 0.7488020062446594), float2(0.18716999888420105, 0.5315709710121155),
    float2(0.5283359885215759, 0.6516619920730591), float2(0.875361979007721, 0.8960540294647217),
    float2(0.3034459948539734, 0.9632560014724731), float2(0.28626400232315063, 0.6138489842414856),
    float2(0.41810399293899536, 0.3053379952907562), float2(0.6449170112609863, 0.386821985244751),
    float2(0.47199898958206177, 0.2539750039577484), float2(0.9628890156745911, 0.5816829800605774),
    float2(0.4387950003147125, 0.18417899310588837), float2(0.6507459878921509, 0.6331980228424072),
    float2(0.8930330276489258, 0.038544900715351105), float2(0.8532059788703918, 0.003906370140612125),
    float2(0.39274299144744873, 0.16058799624443054), float2(0.20053699612617493, 0.4065679907798767),
    float2(0.38416698575019836, 0.6161990165710449), float2(0.676351010799408, 0.10593000054359436),
    float2(0.061189599335193634, 0.7376319766044617), float2(0.5238810181617737, 0.6342049837112427),
    float2(0.07486189901828766, 0.8802449703216553), float2(0.2795189917087555, 0.6369820237159729),
    float2(0.9775689840316772, 0.5641040205955505), float2(0.6579790115356445, 0.7241430282592773),
    float2(0.587602972984314, 0.43284401297569275), float2(0.1535390019416809, 0.42844900488853455),
    float2(0.5446940064430237, 0.4000059962272644), float2(0.7756890058517456, 0.48335200548171997),
    float2(0.22074000537395477, 0.04196299985051155), float2(0.08465830236673355, 0.2912989854812622),
    float2(0.23642699420452118, 0.7124850153923035), float2(0.00299080996774137, 0.349590003490448),
    float2(0.9074069857597351, 0.7080289721488953), float2(0.8657799959182739, 0.7618029713630676),
    float2(0.7648239731788635, 0.8086180090904236), float2(0.3283179998397827, 0.34629398584365845),
    float2(0.3868829905986786, 0.0961942970752716), float2(0.6354870200157166, 0.5113070011138916),
    float2(0.2108519971370697, 0.5334029793739319), float2(0.8951690196990967, 0.9441210031509399),
    float2(0.905239999294281, 0.47059500217437744), float2(0.2443619966506958, 0.9996340274810791),
    float2(0.6532179713249207, 0.06213570013642311), float2(0.18250100314617157, 0.14465799927711487),
    float2(0.9402449727058411, 0.113926000893116), float2(0.8352310061454773, 0.8336129784584045),
    float2(0.7318639755249023, 0.8900110125541687), float2(0.4682759940624237, 0.8510090112686157),
    float2(0.7259439826011658, 0.050416599959135056), float2(0.02999969944357872, 0.5138710141181946),
    float2(0.6426889896392822, 0.8466749787330627), float2(0.115665003657341, 0.6367689967155457),
    float2(0.8600119948387146, 0.31769800186157227), float2(0.651295006275177, 0.15012100338935852),
    float2(0.14667199552059174, 0.48329100012779236), float2(0.002594070043414831, 0.080690898001194),
    float2(0.2742699980735779, 0.9473559856414795), float2(0.8565629720687866, 0.884456992149353),
    float2(0.6153450012207031, 0.5258949995040894), float2(0.6161689758300781, 0.0650349035859108),
    float2(0.04464859887957573, 0.7540209889411926), float2(0.6215699911117554, 0.610614001750946),
    float2(0.9099090099334717, 0.12802499532699585), float2(0.38581499457359314, 0.7100440263748169),
    float2(0.9608139991760254, 0.8434709906578064), float2(0.2586140036582947, 0.17401699721813202),
    float2(0.3295390009880066, 0.0877406969666481), float2(0.3620719909667969, 0.010986699722707272),
    float2(0.5771049857139587, 0.7944579720497131), float2(0.9248939752578735, 0.6160770058631897),
    float2(0.35148200392723083, 0.871487021446228), float2(0.1452070027589798, 0.08713030070066452),
    float2(0.26370999217033386, 0.9180880188941956), float2(0.9304180145263672, 0.21790200471878052),
    float2(0.03839230164885521, 0.8409990072250366), float2(0.3638420104980469, 0.9659109711647034),
    float2(0.7118750214576721, 0.9094820022583008), float2(0.5462200045585632, 0.9036229848861694),
    float2(0.7722709774971008, 0.5484790205955505), float2(0.06863609701395035, 0.23969200253486633),
    float2(0.5107269883155823, 0.6626480221748352), float2(0.8228399753570557, 0.200873002409935),
    float2(0.8471019864082336, 0.7287819981575012), float2(0.8039489984512329, 0.3090910017490387),
    float2(0.16592900454998016, 0.9992070198059082), float2(0.017029300332069397, 0.258338987827301),
    float2(0.14404700696468353, 0.6911529898643494), float2(0.30634498596191406, 0.8825950026512146),
    float2(0.2857449948787689, 0.8237559795379639), float2(0.9004179835319519, 0.21585699915885925),
    float2(0.7908260226249695, 0.6521810293197632), float2(0.542618989944458, 0.09350869804620743),
    float2(0.9871519804000854, 0.40418699383735657), float2(0.586778998374939, 0.546159029006958),
    float2(0.0012207400286570191, 0.14838099479675293), float2(0.3886840045452118, 0.04126099869608879),
    float2(0.30332300066947937, 0.6541950106620789), float2(0.2299869954586029, 0.4130989909172058),
    float2(0.08926659822463989, 0.05551319941878319), float2(0.05078279972076416, 0.44187700748443604),
    float2(0.16595999896526337, 0.9257789850234985), float2(0.3691520094871521, 0.43934398889541626),
    float2(0.9942010045051575, 0.8421279788017273), float2(0.6016420125961304, 0.2378309965133667),
    float2(0.2598649859428406, 0.31934601068496704), float2(0.31193000078201294, 0.09430219978094101),
    float2(0.8503680229187012, 0.5842159986495972), float2(0.4444110095500946, 0.9432049989700317),
    float2(0.8410900235176086, 0.6138190031051636), float2(0.7526469826698303, 0.8688920140266418),
    float2(0.8423110246658325, 0.4768820106983185), float2(0.9987789988517761, 0.5566580295562744),
    float2(0.7288429737091064, 0.2999970018863678), float2(0.41907998919487, 0.51139897108078),
    float2(0.006530960090458393, 0.19009999930858612), float2(0.777184009552002, 0.8656880259513855),
    float2(0.5524160265922546, 0.8083130121231079), float2(0.9157080054283142, 0.2674950063228607),
    float2(0.2386849969625473, 0.9572740197181702), float2(0.17767900228500366, 0.6733300089836121),
    float2(0.4201180040836334, 0.7552419900894165), float2(0.6901149749755859, 0.3751029968261719),
    float2(0.6461989879608154, 0.48582398891448975), float2(0.14795400202274323, 0.8742030262947083),
    float2(0.45377999544143677, 0.5587630271911621), float2(0.8760949969291687, 0.7765129804611206),
    float2(0.09781180322170258, 0.4535660147666931), float2(0.288796991109848, 0.7829520106315613),
    float2(0.5181130170822144, 0.9272440075874329), float2(0.5347149968147278, 0.8046209812164307),
    float2(0.6116520166397095, 0.1325719952583313), float2(0.39588600397109985, 0.3677789866924286),
    float2(0.07458720356225967, 0.7817320227622986), float2(0.09790340065956116, 0.7313460111618042),
    float2(0.47846901416778564, 0.6591389775276184), float2(0.7460860013961792, 0.17899100482463837),
    float2(0.8828089833259583, 0.659932017326355), float2(0.5023350119590759, 0.5522930026054382),
    float2(0.6184269785881042, 0.745140016078949), float2(0.03329569846391678, 0.2152779996395111),
    float2(0.8204290270805359, 0.9392380118370056), float2(0.2187259942293167, 0.9186379909515381),
    float2(0.9653000235557556, 0.9029819965362549), float2(0.41251900792121887, 0.32721900939941406),
    float2(0.5757930278778076, 0.11654999852180481), float2(0.4983980059623718, 0.8133180141448975),
    float2(0.8005620241165161, 0.8046510219573975), float2(0.014648900367319584, 0.7161779999732971),
    float2(0.28116700053215027, 0.2526319921016693), float2(0.46018901467323303, 0.9919739961624146),
    float2(0.8584250211715698, 0.48796001076698303), float2(0.4055910110473633, 0.2269359976053238),
    float2(0.6199529767036438, 0.34458398818969727), float2(0.22595900297164917, 0.7304610013961792),
    float2(0.8765529990196228, 0.38453298807144165), float2(0.7404710054397583, 0.5567190051078796),
    float2(0.33588701486587524, 0.9378029704093933), float2(0.6446120142936707, 0.5851920247077942),
    float2(0.09021270275115967, 0.8339790105819702), float2(0.7720270156860352, 0.8380690217018127),
    float2(0.6318249702453613, 0.5486310124397278), float2(0.5929440259933472, 0.9261149764060974),
    float2(0.6869109869003296, 0.8998990058898926), float2(0.655538022518158, 0.8112429976463318),
    float2(0.5086519718170166, 0.12631599605083466), float2(0.732109010219574, 0.20297899842262268),
    float2(0.5872679948806763, 0.01049839984625578), float2(0.12387499958276749, 0.8373969793319702),
    float2(0.5549790263175964, 0.20761699974536896), float2(0.0631427988409996, 0.17551200091838837),
    float2(0.5942869782447815, 0.7750179767608643), float2(0.20181900262832642, 0.8092589974403381),
    float2(0.23673200607299805, 0.13409799337387085), float2(0.9876400232315063, 0.08883939683437347),
    float2(0.8188419938087463, 0.4270760118961334), float2(0.11001899838447571, 0.47312799096107483),
    float2(0.7257300019264221, 0.8386179804801941), float2(0.7529529929161072, 0.20108599960803986),
    float2(0.5498830080032349, 0.49897798895835876), float2(0.399152010679245, 0.7832580208778381),
    float2(0.44984298944473267, 0.3073819875717163), float2(0.3033849895000458, 0.014557300135493279),
    float2(0.4429759979248047, 0.3716540038585663), float2(0.4494459927082062, 0.8466749787330627),
};

#endif
// FO4RE_INLINE_INCLUDE_END shadow_poisson_kernel.hlsli
#endif

cbuffer PerFrame_CB12 : register(b12)
{

    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
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

    output.diffuse = result.zzzz;
    output.specular = float4(result.xyz, 1.0);
#else
    float shadowBlend = fadeFactor * (shadow - 1.0);
    float splitShadow = shadowBlend + 1.0;

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

    output.specular = float4(splitShadow.xxx, 1.0) + float4(ambientSpecular, 0.0);
    output.diffuse = float4(ambientDiffuse, 1.0) + float4(splitShadow.xxx, shadowBlend);
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

// FO4RE_INLINE_INCLUDE_BEGIN deferred_contracts.hlsli

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

#endif
// FO4RE_INLINE_INCLUDE_END deferred_contracts.hlsli

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

    PS_OUTPUT output;
    output.specular = float4(0.0, 0.0, 0.0, 1.0);
    output.specular += float4(ambientSpecular, 0.0);
    output.diffuse = float4(0.0, 0.0, 0.0, 0.0);
    output.diffuse += float4(ambientDiffuse / 3.0, 0.0);
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
