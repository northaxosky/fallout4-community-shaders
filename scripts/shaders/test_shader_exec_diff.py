#!/usr/bin/env python3

import json
import hashlib
import math
import os
import subprocess
import sys
import tempfile
import unittest
from collections import OrderedDict
from pathlib import Path

import shader_exec_diff as subject


class ShaderExecDiffUnitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contracts = json.loads(
            Path(subject.DEFAULT_CONTRACTS).read_text(encoding="utf-8")
        )
        cls.manifest = json.loads(
            Path(subject.DEFAULT_MANIFEST).read_text(encoding="utf-8")
        )
        cls.entries = {item["name"]: item for item in cls.manifest["shaders"]}
        cls.harness_source_sha256 = subject._hash_file(
            subject.HARNESS_SOURCE, include_sha1=False
        )["sha256"]
        cls.harness_executable = os.environ.get(
            "FO4CS_EXEC_HARNESS", subject.DEFAULT_EXECUTABLE
        )

    @classmethod
    def feature_measurement(cls, fixture):
        feature = cls.contracts["additive_features"]["wetness-effects"]
        wetness_format = next(
            item["wetness_format"]
            for item in feature["fixtures"]
            if item["id"] == fixture
        )
        requested_levels = feature["monotonic_levels"]
        uploaded_levels = [
            (
                int(value * 255.0 + 0.5) / 255.0
                if fixture == "native"
                else value
            )
            for value in requested_levels
        ]
        mask_payloads = subject._expected_wetness_mask_payloads(
            fixture, 16, 16, requested_levels
        )
        expected_buckets = subject._wetness_bucket_evidence_from_payload(
            fixture, mask_payloads["active-pattern"]
        )
        populations = {
            name: evidence["population"]
            for name, evidence in expected_buckets.items()
        }
        post_upload = {
            name: {
                "minimum": evidence["minimum"],
                "maximum": evidence["maximum"],
            }
            for name, evidence in expected_buckets.items()
        }
        cross_buckets = []
        for material in feature["material_buckets"]:
            for ibl in ("inactive", "active"):
                for mask in feature["mask_buckets"]:
                    population = populations[mask]
                    mrt = []
                    for name in feature["mrt_buckets"]:
                        zero_delta = mask == "mask.zero"
                        distribution = (
                            {
                                "min": 0.0,
                                "mean": 0.0,
                                "p50": 0.0,
                                "p95": 0.0,
                                "p99": 0.0,
                                "max": 0.0,
                            }
                            if zero_delta
                            else {
                                "min": 0.05,
                                "mean": 0.1,
                                "p50": 0.1,
                                "p95": 0.15,
                                "p99": 0.19,
                                "max": 0.2,
                            }
                        )
                        stock_energy = float(population * 3)
                        delta_energy = (
                            0.0 if zero_delta else population * 3 * 0.1
                        )
                        mrt.append(
                            {
                                "name": name,
                                "population": population,
                                "changed_pixels": 0 if zero_delta else population,
                                "changed_channels": (
                                    0 if zero_delta else population * 3
                                ),
                                "absolute_delta": distribution,
                                "stock_energy": stock_energy,
                                "delta_energy": delta_energy,
                                "denominator_threshold": max(
                                    1e-12, population * 3e-12
                                ),
                                "delta_fraction_of_stock": (
                                    delta_energy / stock_energy
                                ),
                                "energy_verdict": "PROVEN",
                            }
                        )
                    cross_buckets.append(
                        {
                            "fixture": fixture,
                            "mask": mask,
                            "material": material,
                            "ibl": ibl,
                            "population": population,
                            "post_upload": post_upload[mask],
                            "mrt": mrt,
                        }
                    )
        diffuse_series = []
        for material, ibl in (
            ("material.default", "inactive"),
            ("material.default", "active"),
            ("material.skin", "inactive"),
            ("material.skin", "active"),
        ):
            diffuse_series.append(
                {
                    "material": material,
                    "ibl": ibl,
                    "levels": [
                        {
                            "requested": requested,
                            "uploaded": uploaded,
                            "diffuse_delta_energy": float(index * 25),
                        }
                        for index, (requested, uploaded) in enumerate(
                            zip(requested_levels, uploaded_levels)
                        )
                    ],
                    "violations": 0,
                    "verdict": "PASS",
                }
            )
        peak_levels = []
        for angle in (0.0, 0.25, 0.5, 0.75, 1.0):
            theta = math.radians(angle)
            wet_ndotl = 1.0
            wet_ndotv = min(abs(math.cos(theta * 2.0)) + 1.0e-5, 1.0)
            wet_ndoth = math.cos(theta)
            wet_a = 0.05 * 0.05
            wet_a2 = wet_a * wet_a
            wet_denom = (
                wet_ndoth * wet_ndoth * (wet_a2 - 1.0) + 1.0
            )
            wet_d = wet_a2 / (
                3.141593 * wet_denom * wet_denom
            )
            wet_vis_v = wet_ndotl * (
                wet_ndotv * (1.0 + wet_a) + wet_a
            )
            wet_vis_l = wet_ndotv * (
                wet_ndotl * (1.0 + wet_a) + wet_a
            )
            wet_g = 0.5 / max(wet_vis_v + wet_vis_l, 1.0e-6)
            fresnel = 0.02 + 0.98 * (1.0 - wet_ndoth) ** 5
            core = wet_d * wet_g * fresnel * wet_ndotl
            correct = min(core, 15.0) * 0.95
            wrong = min(core * 0.95, 15.0)
            peak_levels.append(
                {
                    "half_angle_degrees": angle,
                    "ndotl": wet_ndotl,
                    "ndotv": wet_ndotv,
                    "ndoth": wet_ndoth,
                    "vdoth": wet_ndoth,
                    "expected_correct_magnitude": correct,
                    "expected_wrong_magnitude": wrong,
                    "measured_correct_magnitude": correct,
                    "measured_wrong_magnitude": wrong,
                    "proven_channels": 768,
                    "maximum_correct_residual": 0.0,
                    "maximum_wrong_residual": 0.0,
                }
            )
        directional_ambient_levels = []
        substrate_diffuse = [0.1, 0.2, 0.3]
        substrate_specular = [0.01, 0.02, 0.03]
        ambient_film = [0.04, 0.05, 0.06]
        for index, (requested, uploaded) in enumerate(
            zip(requested_levels, uploaded_levels)
        ):
            attenuation = 1.0 if index == 0 else 1.0 - index * 0.2
            directional_ambient_levels.append(
                {
                    "requested": requested,
                    "uploaded": uploaded,
                    "attenuation_mean": attenuation,
                    "representative_attenuation": attenuation,
                    "substrate_diffuse": substrate_diffuse,
                    "layered_diffuse": [
                        value * attenuation for value in substrate_diffuse
                    ],
                    "substrate_specular": substrate_specular,
                    "layered_specular": [
                        substrate_specular[channel] * attenuation
                        + ambient_film[channel] * (1.0 - attenuation)
                        for channel in range(3)
                    ],
                    "recovered_film": (
                        [0.0, 0.0, 0.0]
                        if index == 0
                        else ambient_film
                    ),
                    "maximum_diffuse_factor_spread": 0.0,
                    "maximum_film_residual": 0.0,
                    "maximum_untouched_mutant_residual": 0.0,
                }
            )
        mask_ids = [
            "active-pattern",
            "level-0",
            "level-1",
            "level-2",
            "level-3",
            "level-4",
            "neutral-zero",
        ]
        mask_hashes = [
            {
                "id": mask_id,
                "sha256": hashlib.sha256(
                    mask_payloads[mask_id]
                ).hexdigest(),
                "size": len(mask_payloads[mask_id]),
            }
            for mask_id in mask_ids
        ]
        return {
            "schema": subject.FEATURE_MEASUREMENT_SCHEMA,
            "schema_version": 1,
            "harness_version": subject.HARNESS_VERSION,
            "source_sha256": cls.harness_source_sha256,
            "evidence_class": "additive-feature",
            "comparison": "reconstructed-stock-vs-reconstructed-feature",
            "native_bytecode_used": False,
            "target": "directional",
            "profile": feature["profile"],
            "fixture": fixture,
            "width": 16,
            "height": 16,
            "execution_environment": {
                "driver_type": "WARP",
                "feature_level": "11_0",
                "native_bytecode_used": False,
            },
            "measurement_protocol": feature["measurement_protocol"],
            "wetness_format": wetness_format,
            "measurement_format": "R32G32B32A32_FLOAT",
            "formats": [
                {
                    "bind_point": 4,
                    "dimension": "texture2d",
                    "resource_format": wetness_format,
                    "srv_format": wetness_format,
                }
            ],
            "seeds": [1, 2],
            "generated_inputs_sha256": "0" * 64,
            "hashes": {
                "uploaded_masks": [dict(item) for item in mask_hashes],
                "readback_masks": [dict(item) for item in mask_hashes],
            },
            "contract_delta": {
                "stock_to_feature": "only-texture2d-t4-added",
                "mutation_t4_optimization_away_allowed": False,
                "verdict": "PASS",
            },
            "variants": feature["variants"],
            "properties": {
                "neutral_identity": {
                    "tolerance_absolute": 0,
                    "tolerance_relative": 0,
                    "comparisons": 12,
                    "violations": [],
                    "verdict": "PASS",
                },
                "active_locality": {
                    "zero_tolerance": True,
                    "bucket_basis": (
                        "t4 GPU readback after fixture quantization; "
                        "material from post-quantization upload"
                    ),
                    "pattern": {
                        "exact_zero": True,
                        "exact_one": True,
                        "smooth_strict_partial_ramp": True,
                        "isolated_full_patch_with_zero_moat": True,
                    },
                    "violations": [],
                    "verdict": "PASS",
                },
                "magnitude": {
                    "rgb_only": True,
                    "invalid_denominator_buckets": 0,
                    "verdict": "PASS",
                },
                "wet_lobe_scale": {
                    "stock_scale_symbol": "FO4_DIRECTIONAL_SPECULAR_SCALE",
                    "film_scale_symbol": "FO4_DIRECTIONAL_SPECULAR_SCALE",
                    "stock_scale": 3.141593,
                    "expected_derating_ratio": 0.65,
                    "expected_difference_ratio": 0.35,
                    "proven_channels": 128,
                    "corrected_lobe_mean": 0.01,
                    "corrected_lobe_max": 0.02,
                    "maximum_absolute_residual": 0.0,
                    "maximum_relative_residual": 0.0,
                    "violations": [],
                    "verdict": "PASS",
                },
                "wet_lobe_commensurability": {
                    "claim": (
                        "film and FO4 stock carry the same net NdotL order"
                    ),
                    "levels": [
                        {
                            "requested_ndotl": ndotl,
                            "ndotl": ndotl,
                            "missing_to_corrected_ratio": 1.0 / ndotl,
                            "corrected_to_fo4_reference_ratio": 1.0,
                            "proven_channels": 768,
                            "corrected_lobe_mean": 0.01 * ndotl,
                            "missing_cosine_lobe_mean": 0.01,
                            "maximum_ratio_residual": 0.0,
                        }
                        for ndotl in (1.0, 0.1, 0.01)
                    ],
                    "violations": [],
                    "verdict": "PASS",
                },
                "wet_lobe_peak_ordering": {
                    "correct_formula": (
                        "min(core*fresnel*NdotL,15)*strength"
                    ),
                    "wrong_formula": (
                        "min(core*fresnel*strength*NdotL,15)"
                    ),
                    "levels": peak_levels,
                    "violations": [],
                    "verdict": "PASS",
                },
                "ambient_ibl_layering": {
                    "diffuse_formula": (
                        "Dwet=Dstock*(1-ambientWetnessF)"
                    ),
                    "specular_formula": (
                        "Swet=Sstock*(1-ambientWetnessF)+"
                        "film*ambientWetnessF"
                    ),
                    "levels": directional_ambient_levels,
                    "violations": [],
                    "verdict": "PASS",
                },
                "monotonicity": {
                    "direct_specular_claim": "not-claimed",
                    "diffuse_series": diffuse_series,
                    "ibl_specular_probe": {
                        "scope": "paired directional-IBL activity",
                        "claim": "ambient gradient wetness delta is measured",
                        "levels": [
                            {
                                "requested": requested,
                                "uploaded": uploaded,
                                "delta_energy": float(index * 10),
                            }
                            for index, (requested, uploaded) in enumerate(
                                zip(requested_levels, uploaded_levels)
                            )
                        ],
                        "violations": 0,
                        "verdict": "PASS",
                    },
                    "violations": 0,
                    "verdict": "PASS",
                },
                "mutation_sensitivity": {
                    "mutants": [
                    {
                    "id": feature["mutations"][0]["id"],
                    "class": "historical-regression",
                    "expected_failed_property": "directional_layering",
                    "variants": ["directional", "directional-ibl"],
                    "observed_failed_properties": [
                        "directional_layering"
                    ],
                    "neutral_identity": "PASS",
                    "directional_layering": "FAIL",
                    "diffuse_probe": {
                        "name": "diffuse",
                        "population": 256,
                        "changed_pixels": 256,
                        "changed_channels": 768,
                        "absolute_delta": {
                            "min": 0.1,
                            "mean": 0.1,
                            "p50": 0.1,
                            "p95": 0.1,
                            "p99": 0.1,
                            "max": 0.1,
                        },
                        "signed_delta": {
                            "min": -0.1,
                            "mean": -0.1,
                            "p05": -0.1,
                            "p50": -0.1,
                            "p95": -0.1,
                            "max": -0.1,
                        },
                        "stock_energy": 768.0,
                        "delta_energy": 76.8,
                        "denominator_threshold": 768e-12,
                        "delta_fraction_of_stock": 0.1,
                        "energy_verdict": "PROVEN",
                    },
                    "expected_full_wet_scale": 0.5,
                    "maximum_scale_residual": 0.0,
                    "specular_probe": {
                        "name": "specular",
                        "population": 256,
                        "changed_pixels": 256,
                        "changed_channels": 768,
                        "absolute_delta": {
                            "min": 0.1,
                            "mean": 0.1,
                            "p50": 0.1,
                            "p95": 0.1,
                            "p99": 0.1,
                            "max": 0.1,
                        },
                        "signed_delta": {
                            "min": 0.1,
                            "mean": 0.1,
                            "p05": 0.1,
                            "p50": 0.1,
                            "p95": 0.1,
                            "max": 0.1,
                        },
                        "stock_energy": 768.0,
                        "delta_energy": 76.8,
                        "denominator_threshold": 768e-12,
                        "delta_fraction_of_stock": 0.1,
                        "energy_verdict": "PROVEN",
                    },
                    "verdict": "CAUGHT",
                    },
                    {
                        "id": feature["mutations"][1]["id"],
                        "class": "historical-regression",
                        "expected_failed_property": "wet_lobe_scale",
                        "variants": ["directional"],
                        "neutral_identity": "PASS",
                        "wet_lobe_scale": "FAIL",
                        "expected_ratio": 0.65,
                        "maximum_absolute_residual": 0.0,
                        "maximum_relative_residual": 0.0,
                        "verdict": "CAUGHT",
                    },
                    {
                        "id": feature["mutations"][2]["id"],
                        "class": "omission-regression",
                        "expected_failed_property": "ambient_ibl_layering",
                        "variants": ["directional-ibl"],
                        "neutral_identity": "PASS",
                        "ambient_ibl_layering": "FAIL",
                        "maximum_untouched_residual": 0.0,
                        "verdict": "CAUGHT",
                    },
                    {
                        "id": feature["mutations"][3]["id"],
                        "class": "historical-regression",
                        "expected_failed_property": (
                            "wet_lobe_commensurability"
                        ),
                        "variants": ["directional"],
                        "neutral_identity": "PASS",
                        "wet_lobe_commensurability": "FAIL",
                        "maximum_ratio_residual": 0.0,
                        "verdict": "CAUGHT",
                    },
                    {
                        "id": feature["mutations"][4]["id"],
                        "class": "historical-regression",
                        "expected_failed_property": (
                            "wet_lobe_peak_ordering"
                        ),
                        "variants": ["directional"],
                        "neutral_identity": "PASS",
                        "wet_lobe_peak_ordering": "FAIL",
                        "maximum_formula_residual": 0.0,
                        "verdict": "CAUGHT",
                    },
                    ],
                    "verdict": "CAUGHT",
                },
            },
            "cross_buckets": cross_buckets,
            "verdict": "PASS",
        }

    @classmethod
    def ambient_feature_measurement(cls, fixture):
        feature = cls.contracts["additive_features"]["wetness-effects"]
        ambient = feature["ambient_runtime"]
        wetness_format = next(
            item["wetness_format"]
            for item in feature["fixtures"]
            if item["id"] == fixture
        )
        requested_levels = feature["monotonic_levels"]
        uploaded_levels = [
            (
                int(value * 255.0 + 0.5) / 255.0
                if fixture == "native"
                else value
            )
            for value in requested_levels
        ]
        mask_payloads = subject._expected_wetness_mask_payloads(
            fixture, 16, 16, requested_levels
        )
        expected_buckets = subject._wetness_bucket_evidence_from_payload(
            fixture, mask_payloads["active-pattern"]
        )
        mask_ids = [
            "active-pattern",
            "level-0",
            "level-1",
            "level-2",
            "level-3",
            "level-4",
            "neutral-zero",
        ]
        mask_hashes = [
            {
                "id": mask_id,
                "sha256": hashlib.sha256(
                    mask_payloads[mask_id]
                ).hexdigest(),
                "size": len(mask_payloads[mask_id]),
            }
            for mask_id in mask_ids
        ]
        cross_buckets = []
        for scenario in ambient["scenario_buckets"]:
            for mask in feature["mask_buckets"]:
                evidence = expected_buckets[mask]
                population = evidence["population"]
                zero_delta = mask == "mask.zero"
                distribution = (
                    {
                        "min": 0.0,
                        "mean": 0.0,
                        "p50": 0.0,
                        "p95": 0.0,
                        "p99": 0.0,
                        "max": 0.0,
                    }
                    if zero_delta
                    else {
                        "min": 0.05,
                        "mean": 0.1,
                        "p50": 0.1,
                        "p95": 0.15,
                        "p99": 0.19,
                        "max": 0.2,
                    }
                )
                signed_distribution = (
                    {
                        "min": 0.0,
                        "mean": 0.0,
                        "p05": 0.0,
                        "p50": 0.0,
                        "p95": 0.0,
                        "max": 0.0,
                    }
                    if zero_delta
                    else (
                        {
                            "min": -0.2,
                            "mean": -0.1,
                            "p05": -0.19,
                            "p50": -0.1,
                            "p95": -0.05,
                            "max": -0.05,
                        }
                        if scenario == "diffuse"
                        else {
                            "min": 0.05,
                            "mean": 0.1,
                            "p05": 0.05,
                            "p50": 0.1,
                            "p95": 0.15,
                            "max": 0.2,
                        }
                    )
                )
                stock_energy = (
                    0.0
                    if scenario == "matte"
                    else float(population * 3)
                )
                delta_energy = (
                    0.0 if zero_delta else population * 3 * 0.1
                )
                cross_buckets.append(
                    {
                        "fixture": fixture,
                        "mask": mask,
                        "scenario": scenario,
                        "population": population,
                        "post_upload": {
                            "minimum": evidence["minimum"],
                            "maximum": evidence["maximum"],
                        },
                        "output": {
                            "name": "color",
                            "population": population,
                            "changed_pixels": (
                                0 if zero_delta else population
                            ),
                            "changed_channels": (
                                0 if zero_delta else population * 3
                            ),
                            "absolute_delta": distribution,
                            "signed_delta": signed_distribution,
                            "stock_energy": stock_energy,
                            "delta_energy": delta_energy,
                            "denominator_threshold": max(
                                1e-12, population * 3e-12
                            ),
                            "delta_fraction_of_stock": (
                                delta_energy / stock_energy
                                if stock_energy
                                else 0.0
                            ),
                            "energy_verdict": (
                                "UNPROVEN"
                                if scenario == "matte"
                                else "PROVEN"
                            ),
                        },
                    }
                )
        film_substrate = [0.5625, 0.375, 0.1875]
        film_source = [0.1875, 0.375, 0.5625]
        film_ndotv = 0.1
        one_minus_ndotv = 1.0 - film_ndotv
        film_fresnel = 0.02 + 0.98 * one_minus_ndotv**5
        film_levels = []
        for index, (requested, uploaded) in enumerate(
            zip(requested_levels, uploaded_levels)
        ):
            wetness_f = min(uploaded, 0.95) * film_fresnel
            expected_delta = [
                wetness_f * (film_source[channel] - film_substrate[channel])
                for channel in range(3)
            ]
            expected_film = [
                wetness_f * film_source[channel] for channel in range(3)
            ]
            film_levels.append(
                {
                    "requested": requested,
                    "uploaded": uploaded,
                    "wetness_f": wetness_f,
                    "expected_delta": expected_delta,
                    "observed_delta": list(expected_delta),
                    "expected_film": expected_film,
                    "observed_film": list(expected_film),
                    "maximum_residual": 0.0,
                    "expected_energy_delta": sum(expected_delta),
                    "observed_energy_delta": sum(expected_delta),
                    "film_nonzero_required": index != 0,
                    "film_nonzero": True,
                }
            )
        return {
            "schema": subject.FEATURE_MEASUREMENT_SCHEMA,
            "schema_version": 1,
            "harness_version": subject.HARNESS_VERSION,
            "source_sha256": cls.harness_source_sha256,
            "evidence_class": "additive-feature",
            "comparison": "reconstructed-stock-vs-reconstructed-feature",
            "native_bytecode_used": False,
            "target": "ambient-runtime",
            "profile": ambient["profile"],
            "fixture": fixture,
            "width": 16,
            "height": 16,
            "execution_environment": {
                "driver_type": "WARP",
                "feature_level": "11_0",
                "native_bytecode_used": False,
            },
            "measurement_protocol": feature["measurement_protocol"],
            "wetness_format": wetness_format,
            "measurement_format": "R32G32B32A32_FLOAT",
            "formats": [
                {
                    "bind_point": 13,
                    "dimension": "texture2d",
                    "resource_format": wetness_format,
                    "srv_format": wetness_format,
                }
            ],
            "seeds": [1, 2, 3, 4, 5, 6],
            "generated_inputs_sha256": "0" * 64,
            "hashes": {
                "uploaded_masks": [dict(item) for item in mask_hashes],
                "readback_masks": [dict(item) for item in mask_hashes],
            },
            "contract_delta": {
                "stock_to_feature": "only-texture2d-t13-added",
                "mutation_t13_optimization_away_allowed": False,
                "verdict": "PASS",
            },
            "variants": ambient["variants"],
            "properties": {
                "neutral_identity": {
                    "tolerance_absolute": 0,
                    "tolerance_relative": 0,
                    "comparisons": 6,
                    "violations": [],
                    "verdict": "PASS",
                },
                "active_locality": {
                    "zero_tolerance": True,
                    "bucket_basis": (
                        "t13 GPU readback after fixture quantization; "
                        "scenario from controlled ambient inputs"
                    ),
                    "pattern": {
                        "exact_zero": True,
                        "exact_one": True,
                        "smooth_strict_partial_ramp": True,
                        "isolated_full_patch_with_zero_moat": True,
                    },
                    "violations": [],
                    "verdict": "PASS",
                },
                "magnitude": {
                    "rgb_only": True,
                    "invalid_magnitude_buckets": 0,
                    "glossy_probe": {
                        "scenario": "reflection",
                        "mask": "mask.full",
                        "output": "color",
                        "denominator_policy": "stock-energy-proven",
                        "maximum_delta_fraction_of_stock": 2.5,
                        "maximum_p95_absolute_delta": 0.9,
                        "maximum_absolute_delta": 1.0,
                        "maximum_positive_delta": 1.0,
                        "inputs": {
                            "smoothness": 0.85,
                            "spec_magnitude": 0.35,
                            "decoded_material": 0.5,
                            "encoded_material": (0.5 * 0.02) ** 0.5,
                            "maximum_encoded_material": 0.02**0.5,
                        },
                    },
                    "verdict": "PASS",
                },
                "matte_sheen": {
                    "probe": {
                        "scenario": "matte",
                        "mask": "mask.full",
                        "output": "color",
                    },
                    "inputs": {
                        "smoothness": 0.25,
                        "smoothness_maximum": 0.3,
                        "spec_magnitude": 0.05,
                        "encoded_material": (0.01 * 0.02) ** 0.5,
                    },
                    "film_model": {
                        "substrate_independent": True,
                        "coverage_source": "t13",
                        "roughness_formula": (
                            "max(saturate(1-wetness),0.05)"
                        ),
                        "minimum_water_roughness": 0.05,
                        "smoothness_formula": "1-wetFilmRoughness",
                        "strength_formula": (
                            "saturate(1-wetFilmRoughness)"
                        ),
                        "full_wetness_roughness": 0.05,
                        "full_wetness_smoothness": 0.95,
                        "f0": 0.02,
                        "full_wetness_strength": 0.95,
                        "fresnel_model": "schlick",
                        "energy_attenuation": "substrate*(1-wetnessF)",
                        "lobe_path": "ambient-ibl",
                        "normal_incidence_reflection_scalar": 0.95 * 0.02,
                    },
                    "signed_rule": "feature-stock RGB strictly positive",
                    "stock_denominator": "expected-zero",
                    "violations": [],
                    "verdict": "PASS",
                },
                "no_ibl_film": {
                    "probe": {
                        "scenario": "no-ibl",
                        "mask": "mask.full",
                        "output": "color",
                    },
                    "inputs": {
                        "substrate_ibl_available": False,
                        "material_probe_selector": 0,
                        "smoothness": 0.25,
                        "spec_magnitude": 0.05,
                        "encoded_material": (0.01 * 0.02) ** 0.5,
                        "ambient_base": [0.2, 0.2, 0.2],
                        "lit_scene": [0.6, 0.45, 0.3, 1],
                        "lit_scene_weight": 0.75,
                        "lit_scene_alpha": 1,
                    },
                    "signed_rule": "feature-stock RGB strictly positive",
                    "stock_denominator": "proven",
                    "violations": [],
                    "verdict": "PASS",
                },
                "ambient_film_blend": {
                    "probe": {
                        "scenario": "layered-matte-grazing",
                        "mask": "levels",
                        "output": "color",
                    },
                    "ndotv": film_ndotv,
                    "ndotv_range": {
                        "exclusive_minimum": 0,
                        "inclusive_maximum": 0.125,
                    },
                    "substrate": film_substrate,
                    "film_source": film_source,
                    "formula": (
                        "substrate*(1-wetnessF)+film*wetnessF"
                    ),
                    "absolute_tolerance": 2e-5,
                    "relative_tolerance": 2e-4,
                    "maximum_energy_residual": 6e-5,
                    "levels": film_levels,
                    "violations": [],
                    "verdict": "PASS",
                },
                "monotonicity": {
                    "claim": (
                        "isolated diffuse absolute RGB delta is nondecreasing"
                    ),
                    "series": [
                        {
                            "scenario": scenario,
                            "claim": (
                                "absolute RGB delta is nondecreasing"
                                if scenario == "diffuse"
                                else (
                                    "net layered delta measured; "
                                    "monotonicity not claimed"
                                )
                            ),
                            "levels": [
                                {
                                    "requested": requested,
                                    "uploaded": uploaded,
                                    "delta_energy": float(index * 25),
                                }
                                for index, (requested, uploaded) in enumerate(
                                    zip(requested_levels, uploaded_levels)
                                )
                            ],
                            "violations": 0,
                            "verdict": "PASS",
                        }
                        for scenario in ambient["scenario_buckets"]
                    ],
                    "violations": 0,
                    "verdict": "PASS",
                },
                "mutation_sensitivity": {
                    "id": "ambient-wetness-old-output-multiply",
                    "expected_failed_property": "matte_sheen",
                    "observed_failed_properties": [
                        "matte_sheen",
                        "no_ibl_film",
                        "ambient_film_blend",
                    ],
                    "neutral_identity": "PASS",
                    "matte_sheen": "FAIL",
                    "no_ibl_film": "FAIL",
                    "ambient_film_blend": "FAIL",
                    "glossy_reflection": "PASS",
                    "matte_probe": {
                        "name": "color",
                        "population": expected_buckets["mask.full"][
                            "population"
                        ],
                        "changed_pixels": 0,
                        "changed_channels": 0,
                        "absolute_delta": {
                            name: 0.0
                            for name in (
                                "min",
                                "mean",
                                "p50",
                                "p95",
                                "p99",
                                "max",
                            )
                        },
                        "signed_delta": {
                            name: 0.0
                            for name in (
                                "min",
                                "mean",
                                "p05",
                                "p50",
                                "p95",
                                "max",
                            )
                        },
                        "stock_energy": 0.0,
                        "delta_energy": 0.0,
                        "denominator_threshold": max(
                            1e-12,
                            expected_buckets["mask.full"]["population"]
                            * 3e-12,
                        ),
                        "delta_fraction_of_stock": 0.0,
                        "energy_verdict": "UNPROVEN",
                    },
                    "no_ibl_probe": {
                        "name": "color",
                        "population": expected_buckets["mask.full"][
                            "population"
                        ],
                        "changed_pixels": expected_buckets["mask.full"][
                            "population"
                        ],
                        "changed_channels": expected_buckets["mask.full"][
                            "population"
                        ]
                        * 3,
                        "absolute_delta": {
                            "min": 0.05,
                            "mean": 0.1,
                            "p50": 0.1,
                            "p95": 0.15,
                            "p99": 0.19,
                            "max": 0.2,
                        },
                        "signed_delta": {
                            "min": -0.2,
                            "mean": -0.1,
                            "p05": -0.19,
                            "p50": -0.1,
                            "p95": -0.05,
                            "max": -0.05,
                        },
                        "stock_energy": float(
                            expected_buckets["mask.full"]["population"] * 3
                        ),
                        "delta_energy": float(
                            expected_buckets["mask.full"]["population"]
                            * 3
                            * 0.1
                        ),
                        "denominator_threshold": max(
                            1e-12,
                            expected_buckets["mask.full"]["population"]
                            * 3e-12,
                        ),
                        "delta_fraction_of_stock": 0.1,
                        "energy_verdict": "PROVEN",
                    },
                    "layered_probe": {
                        "name": "color",
                        "population": 256,
                        "changed_pixels": 256,
                        "changed_channels": 768,
                        "absolute_delta": {
                            "min": 0.05,
                            "mean": 0.1,
                            "p50": 0.1,
                            "p95": 0.15,
                            "p99": 0.19,
                            "max": 0.2,
                        },
                        "signed_delta": {
                            "min": -0.2,
                            "mean": -0.1,
                            "p05": -0.19,
                            "p50": -0.1,
                            "p95": -0.05,
                            "max": -0.05,
                        },
                        "stock_energy": 288.0,
                        "delta_energy": 76.8,
                        "denominator_threshold": 768e-12,
                        "delta_fraction_of_stock": 76.8 / 288.0,
                        "energy_verdict": "PROVEN",
                    },
                    "glossy_probe": {
                        "name": "color",
                        "population": expected_buckets["mask.full"][
                            "population"
                        ],
                        "changed_pixels": expected_buckets["mask.full"][
                            "population"
                        ],
                        "changed_channels": expected_buckets["mask.full"][
                            "population"
                        ]
                        * 3,
                        "absolute_delta": {
                            "min": 0.05,
                            "mean": 0.1,
                            "p50": 0.1,
                            "p95": 0.15,
                            "p99": 0.19,
                            "max": 0.2,
                        },
                        "signed_delta": {
                            "min": 0.05,
                            "mean": 0.1,
                            "p05": 0.05,
                            "p50": 0.1,
                            "p95": 0.15,
                            "max": 0.2,
                        },
                        "stock_energy": float(
                            expected_buckets["mask.full"]["population"] * 3
                        ),
                        "delta_energy": float(
                            expected_buckets["mask.full"]["population"]
                            * 3
                            * 0.1
                        ),
                        "denominator_threshold": max(
                            1e-12,
                            expected_buckets["mask.full"]["population"]
                            * 3e-12,
                        ),
                        "delta_fraction_of_stock": 0.1,
                        "energy_verdict": "PROVEN",
                    },
                    "verdict": "CAUGHT",
                },
            },
            "cross_buckets": cross_buckets,
            "verdict": "PASS",
        }

    def test_contract_schema_and_predicates(self):
        subject.validate_contracts(self.contracts)
        self.assertEqual(7, subject.FEATURE_PROTOCOL_VERSION)
        self.assertEqual(
            "wetness-warp-v7",
            self.contracts["additive_features"]["wetness-effects"][
                "measurement_protocol"
            ],
        )
        self.assertEqual(["adversarial", "native"], self.contracts["fixtures"])
        for profile in self.contracts["profiles"].values():
            for fixture in subject.FIXTURE_ORDER:
                required = profile["required_buckets"][fixture]
                self.assertEqual(len(required), len(set(required)))
                self.assertTrue(
                    all(profile["predicates"].get(name) for name in required)
                )

    def test_both_fixtures_and_required_mutations(self):
        self.assertEqual(
            {"adversarial", "native"}, set(self.contracts["fixtures"])
        )
        mutations = self.contracts["mutations"]
        self.assertEqual(len(subject.REQUIRED_MUTATION_IDS), len(mutations))
        self.assertEqual(
            subject.REQUIRED_MUTATION_IDS,
            {item["id"] for item in mutations},
        )
        self.assertTrue(all(item["class"] for item in mutations))
        directional = self.contracts["profiles"]["directional-lighting"]
        brdf_buckets = {
            "brdf.peak",
            "brdf.fallback",
            "brdf.unity-ratio",
            "brdf.nonunity-ratio",
        }
        for fixture in subject.FIXTURE_ORDER:
            self.assertTrue(
                brdf_buckets.issubset(
                    directional["required_buckets"][fixture]
                )
            )
        contract_profiles = {
            item["name"]: item["profile"]
            for item in self.contracts["contracts"]
        }
        self.assertEqual(
            "point-lighting-live",
            contract_profiles["bsdf_light_deferred_point"],
        )
        self.assertEqual(
            "ambient-ibl-runtime",
            contract_profiles["ambient_ibl_pass_runtime"],
        )
        self.assertEqual(
            "vls-slice-scatter",
            contract_profiles["vls_slice_scatter"],
        )
        for profile_name in (
            "ambient-ibl-runtime",
            "point-lighting-live",
            "vls-slice-scatter",
        ):
            required = self.contracts["profiles"][profile_name]["required_buckets"]
            self.assertIn("depth.equal", required["adversarial"])
            self.assertNotIn("depth.equal", required["native"])

    def test_additive_feature_contract_is_separate_and_exact(self):
        subject.validate_additive_features(
            self.contracts["additive_features"]
        )
        feature = self.contracts["additive_features"]["wetness-effects"]
        self.assertEqual(
            "reconstructed-stock-vs-reconstructed-feature",
            feature["comparison"],
        )
        self.assertFalse(feature["native_bytecode_used"])
        self.assertEqual(
            ["directional", "directional-ibl"],
            [item["id"] for item in feature["variants"]],
        )
        ambient = feature["ambient_runtime"]
        self.assertEqual("ambient-runtime", ambient["id"])
        self.assertEqual(
            "shaders/lighting/ambient_ibl_pass_runtime.hlsl",
            ambient["source"],
        )
        self.assertEqual(
            [{"id": "ambient-runtime", "defines": []}],
            ambient["variants"],
        )
        self.assertEqual(
            [
                "combined",
                "diffuse",
                "reflection",
                "matte",
                "no-ibl",
                "layered-matte-grazing",
            ],
            ambient["scenario_buckets"],
        )
        self.assertEqual(
            "ambient-wetness-old-output-multiply",
            ambient["mutation"]["id"],
        )
        self.assertEqual(
            "matte_sheen",
            ambient["mutation"]["expected_failed_property"],
        )
        self.assertEqual(1, len(ambient["mutation"]["replacements"]))
        self.assertNotIn(
            ambient["mutation"]["id"], subject.REQUIRED_MUTATION_IDS
        )
        self.assertEqual(
            [
                "directional-wetness-old-substrate-floors-output-multiply",
                "directional-wetness-historical-pi-065-derating",
                "directional-wetness-ambient-ibl-untouched",
                "directional-wetness-missing-fo4-cosine",
                "directional-wetness-strength-inside-clamp",
            ],
            [item["id"] for item in feature["mutations"]],
        )
        source = Path(
            subject.REPO_ROOT,
            "shaders",
            "lighting",
            "bsdf_light_deferred.hlsl",
        ).read_text(encoding="utf-8")
        self.assertEqual(6, source.count("#ifdef WETNESS_EFFECTS"))
        self.assertEqual(
            3, source.count("FO4_DIRECTIONAL_SPECULAR_SCALE")
        )
        self.assertNotIn("FO4_DIRECTIONAL_SPECULAR_SCALE * 0.65", source)
        self.assertIn(
            "wetD * wetG * wetFresnel * wetNdotL", source
        )
        self.assertEqual(
            ["adversarial", "native"],
            [item["id"] for item in feature["fixtures"]],
        )
        self.assertEqual(10, len(self.contracts["mutations"]))
        self.assertEqual(
            subject.REQUIRED_MUTATION_IDS,
            {item["id"] for item in self.contracts["mutations"]},
        )

    def test_additive_feature_mutation_is_exact_and_nonmutating(self):
        feature = self.contracts["additive_features"]["wetness-effects"]
        for target in ("directional", "ambient-runtime"):
            contract = subject._feature_target_contract(feature, target)
            source = os.path.join(subject.REPO_ROOT, contract["source"])
            before = Path(source).read_bytes()
            original = before.decode("utf-8-sig").replace("\r\n", "\n")
            for mutation in subject._feature_mutations(contract):
                mutant = subject.build_additive_feature_mutant(
                    original, contract, mutation
                )
                replacements = mutation["replacements"]
                for replacement in replacements:
                    if replacement["new"]:
                        self.assertEqual(
                            original.count(replacement["new"]) + 1,
                            mutant.count(replacement["new"]),
                        )
                    if replacement["old"] not in replacement["new"]:
                        self.assertNotIn(replacement["old"], mutant)
                self.assertEqual(before, Path(source).read_bytes())
                with self.assertRaises(subject.StableFailure):
                    first = replacements[0]
                    subject.build_additive_feature_mutant(
                        original.replace(
                            first["old"],
                            first["old"] * 2,
                        ),
                        contract,
                        mutation,
                    )

    def test_additive_feature_measurement_schema_and_matrix(self):
        feature = self.contracts["additive_features"]["wetness-effects"]
        for fixture in ("adversarial", "native"):
            measurement = self.feature_measurement(fixture)
            self.assertEqual(
                [],
                subject.validate_feature_measurement(
                    measurement,
                    fixture,
                    feature,
                    16,
                    16,
                    self.harness_source_sha256,
                ),
            )
            keys = {
                (item["mask"], item["material"], item["ibl"])
                for item in measurement["cross_buckets"]
            }
            self.assertEqual(12, len(keys))
            self.assertTrue(
                all(
                    [mrt["name"] for mrt in item["mrt"]]
                    == ["diffuse", "specular"]
                    and item["post_upload"]
                    for item in measurement["cross_buckets"]
                )
            )
            ambient_measurement = self.ambient_feature_measurement(fixture)
            self.assertEqual(
                [],
                subject.validate_feature_measurement(
                    ambient_measurement,
                    fixture,
                    feature,
                    16,
                    16,
                    self.harness_source_sha256,
                ),
            )
            ambient_keys = {
                (item["mask"], item["scenario"])
                for item in ambient_measurement["cross_buckets"]
            }
            self.assertEqual(18, len(ambient_keys))
            self.assertTrue(
                all(
                    item["output"]["name"] == "color"
                    and item["post_upload"]
                    for item in ambient_measurement["cross_buckets"]
                )
            )
        ambient_mutation_missed = self.ambient_feature_measurement(
            "adversarial"
        )
        ambient_mutation_missed["properties"]["mutation_sensitivity"][
            "verdict"
        ] = "MISSED"
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_mutation_missed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_inactive = self.ambient_feature_measurement("adversarial")
        ambient_bucket = next(
            item
            for item in ambient_inactive["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "combined"
        )
        ambient_bucket["output"]["changed_pixels"] = 0
        ambient_bucket["output"]["changed_channels"] = 0
        ambient_bucket["output"]["delta_energy"] = 0.0
        ambient_bucket["output"]["delta_fraction_of_stock"] = 0.0
        ambient_bucket["output"]["absolute_delta"] = {
            name: 0.0
            for name in ("min", "mean", "p50", "p95", "p99", "max")
        }
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_inactive,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_inverted = self.ambient_feature_measurement("adversarial")
        diffuse_bucket = next(
            item
            for item in ambient_inverted["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "diffuse"
        )
        diffuse_bucket["output"]["signed_delta"] = {
            "min": 0.05,
            "mean": 0.1,
            "p05": 0.05,
            "p50": 0.1,
            "p95": 0.15,
            "max": 0.2,
        }
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_inverted,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        reflection_inverted = self.ambient_feature_measurement(
            "adversarial"
        )
        reflection_bucket = next(
            item
            for item in reflection_inverted["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "reflection"
        )
        reflection_bucket["output"]["signed_delta"] = {
            "min": -0.2,
            "mean": -0.1,
            "p05": -0.19,
            "p50": -0.1,
            "p95": -0.05,
            "max": -0.05,
        }
        self.assertEqual(
            [],
            subject.validate_feature_measurement(
                reflection_inverted,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        matte_inactive = self.ambient_feature_measurement("adversarial")
        matte_bucket = next(
            item
            for item in matte_inactive["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "matte"
        )
        matte_bucket["output"]["changed_pixels"] = 0
        matte_bucket["output"]["changed_channels"] = 0
        matte_bucket["output"]["delta_energy"] = 0.0
        matte_bucket["output"]["absolute_delta"] = {
            name: 0.0
            for name in ("min", "mean", "p50", "p95", "p99", "max")
        }
        matte_bucket["output"]["signed_delta"] = {
            name: 0.0
            for name in ("min", "mean", "p05", "p50", "p95", "max")
        }
        self.assertTrue(
            subject.validate_feature_measurement(
                matte_inactive,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        no_ibl_inactive = self.ambient_feature_measurement("adversarial")
        no_ibl_bucket = next(
            item
            for item in no_ibl_inactive["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "no-ibl"
        )
        no_ibl_bucket["output"]["changed_pixels"] = 0
        no_ibl_bucket["output"]["changed_channels"] = 0
        no_ibl_bucket["output"]["delta_energy"] = 0.0
        no_ibl_bucket["output"]["delta_fraction_of_stock"] = 0.0
        no_ibl_bucket["output"]["absolute_delta"] = {
            name: 0.0
            for name in ("min", "mean", "p50", "p95", "p99", "max")
        }
        no_ibl_bucket["output"]["signed_delta"] = {
            name: 0.0
            for name in ("min", "mean", "p05", "p50", "p95", "max")
        }
        self.assertTrue(
            subject.validate_feature_measurement(
                no_ibl_inactive,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        no_ibl_mutation_missed = self.ambient_feature_measurement(
            "adversarial"
        )
        mutation = no_ibl_mutation_missed["properties"][
            "mutation_sensitivity"
        ]
        mutation["no_ibl_film"] = "PASS"
        mutation["observed_failed_properties"] = ["matte_sheen"]
        self.assertTrue(
            subject.validate_feature_measurement(
                no_ibl_mutation_missed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_gate_regression = self.ambient_feature_measurement("adversarial")
        gated_level = film_gate_regression["properties"][
            "ambient_film_blend"
        ]["levels"][1]
        gated_level["observed_delta"] = [
            -0.08,
            -0.05,
            -0.02,
        ]
        gated_level["observed_film"] = [0.0, 0.0, 0.0]
        gated_level["maximum_residual"] = 0.08
        gated_level["observed_energy_delta"] = -0.15
        gated_level["film_nonzero"] = False
        self.assertTrue(
            subject.validate_feature_measurement(
                film_gate_regression,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_wrong_magnitude = self.ambient_feature_measurement("adversarial")
        wrong_level = film_wrong_magnitude["properties"][
            "ambient_film_blend"
        ]["levels"][1]
        wrong_level["observed_delta"][0] *= 0.5
        wrong_level["observed_film"][0] *= 0.5
        wrong_level["maximum_residual"] = 0.01
        self.assertTrue(
            subject.validate_feature_measurement(
                film_wrong_magnitude,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_energy_leak = self.ambient_feature_measurement("adversarial")
        film_energy_leak["properties"]["ambient_film_blend"]["levels"][1][
            "observed_energy_delta"
        ] = -0.01
        self.assertTrue(
            subject.validate_feature_measurement(
                film_energy_leak,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_residual_tampered = self.ambient_feature_measurement("adversarial")
        film_residual_tampered["properties"]["ambient_film_blend"]["levels"][
            1
        ]["maximum_residual"] = 1.0
        self.assertTrue(
            subject.validate_feature_measurement(
                film_residual_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_energy_summary_tampered = self.ambient_feature_measurement(
            "adversarial"
        )
        level = film_energy_summary_tampered["properties"][
            "ambient_film_blend"
        ]["levels"][1]
        level["observed_delta"] = [
            value + 5.0e-5 for value in level["observed_delta"]
        ]
        level["observed_film"] = [
            value + 5.0e-5 for value in level["observed_film"]
        ]
        level["maximum_residual"] = 5.0e-5
        level["observed_energy_delta"] = 0.0
        self.assertTrue(
            subject.validate_feature_measurement(
                film_energy_summary_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_bad_angle = self.ambient_feature_measurement("adversarial")
        film_bad_angle["properties"]["ambient_film_blend"]["ndotv"] = 0.2
        self.assertTrue(
            subject.validate_feature_measurement(
                film_bad_angle,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_missing_level = self.ambient_feature_measurement("adversarial")
        film_missing_level["properties"]["ambient_film_blend"]["levels"].pop()
        self.assertTrue(
            subject.validate_feature_measurement(
                film_missing_level,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_fixture_tampered = self.ambient_feature_measurement("adversarial")
        film_fixture_tampered["properties"]["ambient_film_blend"][
            "substrate"
        ] = [1.0, 1.0, 1.0]
        self.assertTrue(
            subject.validate_feature_measurement(
                film_fixture_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        film_factor_tampered = self.ambient_feature_measurement("adversarial")
        blend = film_factor_tampered["properties"]["ambient_film_blend"]
        level = blend["levels"][1]
        level["wetness_f"] = 0.001
        level["expected_delta"] = [
            0.001 * (blend["film_source"][channel] - blend["substrate"][channel])
            for channel in range(3)
        ]
        level["observed_delta"] = list(level["expected_delta"])
        level["expected_film"] = [
            0.001 * value for value in blend["film_source"]
        ]
        level["observed_film"] = list(level["expected_film"])
        level["expected_energy_delta"] = sum(level["expected_delta"])
        level["observed_energy_delta"] = sum(level["observed_delta"])
        self.assertTrue(
            subject.validate_feature_measurement(
                film_factor_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        directional_mutation_missed = self.feature_measurement("adversarial")
        old_mutant = directional_mutation_missed["properties"][
            "mutation_sensitivity"
        ]["mutants"][0]
        old_mutant["specular_probe"]["changed_pixels"] = 0
        old_mutant["specular_probe"]["delta_energy"] = 0.0
        self.assertTrue(
            subject.validate_feature_measurement(
                directional_mutation_missed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        scale_zero = self.feature_measurement("adversarial")
        scale_zero["properties"]["wet_lobe_scale"][
            "corrected_lobe_mean"
        ] = 0.0
        self.assertTrue(
            subject.validate_feature_measurement(
                scale_zero,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        cosine_ratio_tampered = self.feature_measurement("adversarial")
        cosine_ratio_tampered["properties"]["wet_lobe_commensurability"][
            "levels"
        ][1]["corrected_to_fo4_reference_ratio"] = 10.0
        self.assertTrue(
            subject.validate_feature_measurement(
                cosine_ratio_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        cosine_coverage_tampered = self.feature_measurement("adversarial")
        cosine_coverage_tampered["properties"][
            "wet_lobe_commensurability"
        ]["levels"][1]["proven_channels"] = 1
        self.assertTrue(
            subject.validate_feature_measurement(
                cosine_coverage_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        cosine_mutant_missed = self.feature_measurement("adversarial")
        cosine_mutant_missed["properties"]["mutation_sensitivity"]["mutants"][
            3
        ]["wet_lobe_commensurability"] = "PASS"
        self.assertTrue(
            subject.validate_feature_measurement(
                cosine_mutant_missed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        peak_formula_tampered = self.feature_measurement("adversarial")
        peak_formula_tampered["properties"]["wet_lobe_peak_ordering"][
            "levels"
        ][0]["measured_correct_magnitude"] = 15.0
        self.assertTrue(
            subject.validate_feature_measurement(
                peak_formula_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        peak_mutant_missed = self.feature_measurement("adversarial")
        peak_mutant_missed["properties"]["mutation_sensitivity"]["mutants"][
            4
        ]["wet_lobe_peak_ordering"] = "PASS"
        self.assertTrue(
            subject.validate_feature_measurement(
                peak_mutant_missed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        pi_ratio_tampered = self.feature_measurement("adversarial")
        pi_ratio_tampered["properties"]["mutation_sensitivity"]["mutants"][1][
            "expected_ratio"
        ] = 0.66
        self.assertTrue(
            subject.validate_feature_measurement(
                pi_ratio_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_layering_missing = self.feature_measurement("adversarial")
        ambient_level = ambient_layering_missing["properties"][
            "ambient_ibl_layering"
        ]["levels"][1]
        ambient_level["layered_diffuse"] = list(
            ambient_level["substrate_diffuse"]
        )
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_layering_missing,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_specular_tampered = self.feature_measurement("adversarial")
        ambient_specular_tampered["properties"]["ambient_ibl_layering"][
            "levels"
        ][1]["layered_specular"] = [99.0, 98.0, 97.0]
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_specular_tampered,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_attenuation_missing = self.feature_measurement("adversarial")
        ambient_attenuation_missing["properties"]["ambient_ibl_layering"][
            "levels"
        ][1].pop("representative_attenuation")
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_attenuation_missing,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_attenuation_invalid = self.feature_measurement("adversarial")
        ambient_attenuation_invalid["properties"]["ambient_ibl_layering"][
            "levels"
        ][1]["representative_attenuation"] = -0.5
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_attenuation_invalid,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_film_zero = self.feature_measurement("adversarial")
        ambient_film_zero["properties"]["ambient_ibl_layering"]["levels"][1][
            "recovered_film"
        ] = [0.0, 0.0, 0.0]
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_film_zero,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_mutant_missed = self.feature_measurement("adversarial")
        ambient_mutant_missed["properties"]["mutation_sensitivity"]["mutants"][
            2
        ]["ambient_ibl_layering"] = "PASS"
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_mutant_missed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        mutant_dry_identity_failed = self.feature_measurement("adversarial")
        mutant_dry_identity_failed["properties"]["mutation_sensitivity"][
            "mutants"
        ][1]["neutral_identity"] = "FAIL"
        self.assertTrue(
            subject.validate_feature_measurement(
                mutant_dry_identity_failed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        glossy_amplified = self.ambient_feature_measurement("adversarial")
        glossy_bucket = next(
            item
            for item in glossy_amplified["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "reflection"
        )
        glossy_output = glossy_bucket["output"]
        glossy_output["stock_energy"] = glossy_output["delta_energy"] / 2.51
        glossy_output["delta_fraction_of_stock"] = 2.51
        self.assertTrue(
            subject.validate_feature_measurement(
                glossy_amplified,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        glossy_p95_outlier = self.ambient_feature_measurement("adversarial")
        glossy_p95_bucket = next(
            item
            for item in glossy_p95_outlier["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "reflection"
        )
        glossy_p95_bucket["output"]["absolute_delta"]["p95"] = 0.91
        glossy_p95_bucket["output"]["absolute_delta"]["p99"] = 0.91
        glossy_p95_bucket["output"]["absolute_delta"]["max"] = 0.91
        self.assertTrue(
            subject.validate_feature_measurement(
                glossy_p95_outlier,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        glossy_max_outlier = self.ambient_feature_measurement("adversarial")
        glossy_max_bucket = next(
            item
            for item in glossy_max_outlier["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "reflection"
        )
        glossy_max_bucket["output"]["absolute_delta"]["max"] = 1.01
        self.assertTrue(
            subject.validate_feature_measurement(
                glossy_max_outlier,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        glossy_positive_outlier = self.ambient_feature_measurement(
            "adversarial"
        )
        glossy_positive_bucket = next(
            item
            for item in glossy_positive_outlier["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "reflection"
        )
        glossy_positive_bucket["output"]["signed_delta"]["max"] = 1.01
        self.assertTrue(
            subject.validate_feature_measurement(
                glossy_positive_outlier,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        glossy_invalid_encoding = self.ambient_feature_measurement(
            "adversarial"
        )
        glossy_invalid_encoding["properties"]["magnitude"]["glossy_probe"][
            "inputs"
        ]["encoded_material"] = 0.15
        self.assertTrue(
            subject.validate_feature_measurement(
                glossy_invalid_encoding,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        glossy_unproven = self.ambient_feature_measurement("adversarial")
        glossy_unproven_bucket = next(
            item
            for item in glossy_unproven["cross_buckets"]
            if item["mask"] == "mask.full"
            and item["scenario"] == "reflection"
        )
        glossy_unproven_output = glossy_unproven_bucket["output"]
        glossy_unproven_output["stock_energy"] = glossy_unproven_output[
            "denominator_threshold"
        ]
        glossy_unproven_output["delta_fraction_of_stock"] = 0.0
        glossy_unproven_output["energy_verdict"] = "UNPROVEN"
        self.assertTrue(
            subject.validate_feature_measurement(
                glossy_unproven,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        ambient_missing_level = self.ambient_feature_measurement(
            "adversarial"
        )
        ambient_missing_level["properties"]["monotonicity"]["series"][1][
            "levels"
        ].pop()
        self.assertTrue(
            subject.validate_feature_measurement(
                ambient_missing_level,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        malformed = self.feature_measurement("adversarial")
        malformed["properties"]["mutation_sensitivity"]["verdict"] = "MISSED"
        self.assertTrue(
            subject.validate_feature_measurement(
                malformed,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        malformed_mrt = self.feature_measurement("adversarial")
        malformed_mrt["cross_buckets"][0]["mrt"].append(None)
        self.assertTrue(
            subject.validate_feature_measurement(
                malformed_mrt,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        zeroed_active = self.feature_measurement("adversarial")
        active_bucket = next(
            item
            for item in zeroed_active["cross_buckets"]
            if item["mask"] == "mask.partial"
            and item["material"] == "material.default"
            and item["ibl"] == "inactive"
        )
        for stats in active_bucket["mrt"]:
            stats["changed_pixels"] = 0
            stats["changed_channels"] = 0
            stats["delta_energy"] = 0.0
            stats["delta_fraction_of_stock"] = 0.0
            stats["absolute_delta"] = {
                name: 0.0
                for name in ("min", "mean", "p50", "p95", "p99", "max")
            }
        self.assertTrue(
            subject.validate_feature_measurement(
                zeroed_active,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        missing_series = self.feature_measurement("adversarial")
        missing_series["properties"]["monotonicity"]["diffuse_series"].pop()
        self.assertTrue(
            subject.validate_feature_measurement(
                missing_series,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        missing_hash_id = self.feature_measurement("native")
        missing_hash_id["hashes"]["uploaded_masks"].pop(2)
        missing_hash_id["hashes"]["readback_masks"].pop(2)
        self.assertTrue(
            subject.validate_feature_measurement(
                missing_hash_id,
                "native",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        mismatched_hash = self.feature_measurement("native")
        mismatched_hash["hashes"]["readback_masks"][0]["sha256"] = "f" * 64
        self.assertTrue(
            subject.validate_feature_measurement(
                mismatched_hash,
                "native",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        repeated_hash = self.feature_measurement("native")
        repeated = repeated_hash["hashes"]["uploaded_masks"][0]["sha256"]
        for name in ("uploaded_masks", "readback_masks"):
            for item in repeated_hash["hashes"][name]:
                item["sha256"] = repeated
        self.assertTrue(
            subject.validate_feature_measurement(
                repeated_hash,
                "native",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        different_split = self.feature_measurement("adversarial")
        original_populations = {
            item["mask"]: item["population"]
            for item in different_split["cross_buckets"]
            if item["material"] == "material.default"
            and item["ibl"] == "inactive"
        }
        replacement_populations = {
            "mask.zero": original_populations["mask.zero"] - 1,
            "mask.partial": original_populations["mask.partial"] + 1,
            "mask.full": original_populations["mask.full"],
        }
        partial_record = next(
            item
            for item in different_split["cross_buckets"]
            if item["mask"] == "mask.partial"
        )
        replacement_partial_range = {
            "minimum": partial_record["post_upload"]["minimum"] / 2.0,
            "maximum": (
                1.0 + partial_record["post_upload"]["maximum"]
            )
            / 2.0,
        }
        for bucket in different_split["cross_buckets"]:
            population = replacement_populations[bucket["mask"]]
            bucket["population"] = population
            if bucket["mask"] == "mask.partial":
                bucket["post_upload"] = replacement_partial_range
            for stats in bucket["mrt"]:
                zero_delta = bucket["mask"] == "mask.zero" or (
                    bucket["material"] == "material.skin"
                    and stats["name"] == "specular"
                )
                stats["population"] = population
                stats["stock_energy"] = float(population * 3)
                stats["denominator_threshold"] = max(
                    1e-12, population * 3e-12
                )
                if zero_delta:
                    stats["changed_pixels"] = 0
                    stats["changed_channels"] = 0
                    stats["delta_energy"] = 0.0
                    stats["delta_fraction_of_stock"] = 0.0
                else:
                    stats["changed_pixels"] = population
                    stats["changed_channels"] = population * 3
                    stats["delta_energy"] = population * 3 * 0.1
                    stats["delta_fraction_of_stock"] = 0.1
        self.assertTrue(
            subject.validate_feature_measurement(
                different_split,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        zero_monotonic = self.feature_measurement("adversarial")
        for series in zero_monotonic["properties"]["monotonicity"][
            "diffuse_series"
        ]:
            for level in series["levels"]:
                level["diffuse_delta_energy"] = 0.0
        self.assertTrue(
            subject.validate_feature_measurement(
                zero_monotonic,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        contradictory_channels = self.feature_measurement("adversarial")
        diffuse = next(
            item
            for item in contradictory_channels["cross_buckets"]
            if item["mask"] == "mask.partial"
            and item["material"] == "material.default"
            and item["ibl"] == "inactive"
        )["mrt"][0]
        diffuse["changed_channels"] = diffuse["population"]
        self.assertTrue(
            subject.validate_feature_measurement(
                contradictory_channels,
                "adversarial",
                feature,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        summary = subject._feature_property_summary(
            [{"fixture": "native"}], "neutral_identity"
        )
        self.assertEqual("UNPROVEN", summary["verdict"])

    def test_additive_feature_manifest_is_canonical_and_path_safe(self):
        report = OrderedDict(
            (
                ("schema", subject.FEATURE_RUN_SCHEMA),
                ("schema_version", 1),
                ("evidence_class", "additive-feature"),
                (
                    "comparison",
                    "reconstructed-stock-vs-reconstructed-feature",
                ),
                ("native_bytecode_used", False),
                (
                    "fixtures",
                    [
                        self.feature_measurement("adversarial"),
                        self.feature_measurement("native"),
                        self.ambient_feature_measurement("adversarial"),
                        self.ambient_feature_measurement("native"),
                    ],
                ),
                ("verdict", "PASS"),
            )
        )
        subject._assert_receipt_safe(report)
        first = subject._canonical_bytes(report)
        second = subject._canonical_bytes(json.loads(first))
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertNotIn(b"corpus_sha", first)
        self.assertNotIn(b"native_bytecode_hash", first)
        self.assertNotIn(b"DXBC", first)
        with self.assertRaises(subject.StableFailure):
            subject._assert_receipt_safe(
                {"tool": r"C:\absolute\fxc.exe"}
            )
        with self.assertRaises(subject.StableFailure):
            subject._assert_receipt_safe({"tool": "/tmp"})

    def test_feature_source_snapshot_and_staleness_detection(self):
        feature = self.contracts["additive_features"]["wetness-effects"]
        for target in ("directional", "ambient-runtime"):
            contract = subject._feature_target_contract(feature, target)
            source = os.path.join(subject.REPO_ROOT, contract["source"])
            report, captures = subject._capture_hlsl_closure(source)
            self.assertGreaterEqual(len(captures), 1)
            self.assertRegex(
                report["combined_sha256"], r"^[0-9a-f]{64}$"
            )
            with tempfile.TemporaryDirectory() as temporary:
                snapshot = subject._write_hlsl_snapshot(
                    temporary, captures, contract["source"]
                )
                self.assertEqual(
                    next(
                        item["data"]
                        for item in captures
                        if item["label"] == contract["source"]
                    ),
                    Path(snapshot).read_bytes(),
                )
                label = f"snapshot/{target}/{contract['source']}"
                snapshot_capture = subject._capture_file(
                    snapshot, label
                )
                self.assertEqual(
                    [], subject._verify_captured_files([snapshot_capture])
                )
                Path(snapshot).write_bytes(
                    Path(snapshot).read_bytes() + b"\n"
                )
                self.assertEqual(
                    [label],
                    subject._verify_captured_files([snapshot_capture]),
                )
        with tempfile.TemporaryDirectory() as temporary:
            probe = os.path.join(temporary, "probe.bin")
            Path(probe).write_bytes(b"before")
            capture = subject._capture_file(probe, "probe.bin")
            self.assertEqual([], subject._verify_captured_files([capture]))
            Path(probe).write_bytes(b"after")
            self.assertEqual(
                ["probe.bin"], subject._verify_captured_files([capture])
            )

    def test_native_cli_defaults_are_unchanged(self):
        arguments = subject._parse_arguments([])
        self.assertIsNone(arguments.additive_feature)
        self.assertEqual("all", arguments.fixture)
        self.assertEqual("required", arguments.mutation_suite)
        self.assertEqual(subject.REPORT_SCHEMA, "fo4cs.shader-exec-diff-report")
        self.assertEqual(
            subject.MEASUREMENT_SCHEMA,
            "fo4cs.shader-exec-measurement",
        )
        native_only_contracts = json.loads(json.dumps(self.contracts))
        native_only_contracts.pop("additive_features")
        subject.validate_contracts(native_only_contracts)

    def test_source_transforms_are_exact_and_nonmutating(self):
        before = {}
        for mutation in self.contracts["mutations"]:
            entry = self.entries[mutation["target"]]
            path = os.path.join(subject.REPO_ROOT, entry["src"])
            original_bytes = Path(path).read_bytes()
            before[path] = original_bytes
            control, mutant = subject.build_mutation_sources(
                original_bytes.decode("utf-8-sig"),
                mutation["target"],
                mutation["id"],
            )
            self.assertNotEqual(control, mutant)
            self.assertEqual(original_bytes, Path(path).read_bytes())
        for path, original_bytes in before.items():
            self.assertEqual(original_bytes, Path(path).read_bytes())

    def test_replace_exact_rejects_ambiguous_transform(self):
        with self.assertRaises(subject.StableFailure):
            subject._replace_exact("x x", "x", "y", "ambiguous")

    def test_canonical_order_and_final_lf(self):
        value = OrderedDict(
            (
                ("schema", subject.REPORT_SCHEMA),
                ("schema_version", 1),
                ("producer", OrderedDict((("name", "test"),))),
                ("verdict", "PASS"),
            )
        )
        encoded = subject._canonical_bytes(value)
        self.assertTrue(encoded.endswith(b"\n"))
        self.assertFalse(encoded.endswith(b"\n\n"))
        self.assertTrue(encoded.startswith(b'{"schema":'))
        self.assertEqual(encoded, subject._canonical_bytes(json.loads(encoded)))

    def test_report_rejects_paths_and_contains_no_native_content(self):
        safe = OrderedDict(
            (
                ("schema", subject.REPORT_SCHEMA),
                ("schema_version", 1),
                ("source", "shaders/lighting/ambient_ibl_pass.hlsl"),
                (
                    "native",
                    OrderedDict(
                        (
                            ("sha1", "0" * 40),
                            ("sha256", "0" * 64),
                            ("size", 12),
                        )
                    ),
                ),
            )
        )
        subject._assert_receipt_safe(safe)
        encoded = subject._canonical_bytes(safe)
        self.assertNotIn(b"DXBC", encoded)
        with self.assertRaises(subject.StableFailure):
            subject._assert_receipt_safe({"path": r"C:\game\shader.dxbc"})

    def test_staleness_is_byte_exact(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = os.path.join(temporary, "report.json")
            current = subject._canonical_bytes(
                OrderedDict((("schema", subject.REPORT_SCHEMA), ("verdict", "PASS")))
            )
            Path(path).write_bytes(current)
            self.assertTrue(subject.check_report_bytes(path, current))
            changed = subject._canonical_bytes(
                OrderedDict((("schema", subject.REPORT_SCHEMA), ("verdict", "FAIL")))
            )
            self.assertFalse(subject.check_report_bytes(path, changed))
            Path(path).write_bytes(current.rstrip())
            self.assertFalse(subject.check_report_bytes(path, current))

    def test_structured_result_classification(self):
        zero = subject._measurement_metrics_zero()
        mutations = [
            {"verdict": "CAUGHT"}
            for _ in subject.REQUIRED_MUTATION_IDS
        ]
        self.assertEqual(
            ("PASS", 0),
            subject.classify_result(True, True, [], zero, mutations),
        )
        divergent = dict(zero)
        divergent["divergent_pixels"] = 1
        self.assertEqual(
            ("FAIL", 1),
            subject.classify_result(True, True, [], divergent, mutations),
        )
        tolerance = subject._regular_tolerance_failure(
            {"aggregate": {"divergent_pixels": 7}},
            [],
            "target",
            "native",
        )
        self.assertEqual(
            {"code": "tolerance_breach", "detail": "target:native"},
            tolerance,
        )
        self.assertEqual(
            ("FAIL", 1),
            subject.classify_result(
                True, True, [tolerance], divergent, mutations
            ),
        )
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(
                True,
                True,
                [
                    tolerance,
                    {"code": "measurement_protocol", "detail": "x"},
                ],
                divergent,
                mutations,
            ),
        )
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(False, True, [], zero, mutations),
        )
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(
                True,
                True,
                [{"code": "missing_bucket", "detail": "depth.equal"}],
                zero,
                mutations,
            ),
        )
        missed = mutations[:-1] + [{"verdict": "MISSED"}]
        self.assertEqual(
            ("FAIL", 1),
            subject.classify_result(True, True, [], zero, missed),
        )
        unproven = mutations[:-1] + [{"verdict": "UNPROVEN"}]
        self.assertEqual(
            ("UNPROVEN", 2),
            subject.classify_result(True, True, [], zero, unproven),
        )
        for state in (
            "mutation_compile_failed",
            "mutation_transform",
            "mutation_protocol",
            "mutation_control_dirty",
        ):
            infrastructure = subject._mutation_result(
                self.contracts["mutations"][0],
                None,
                None,
                64,
                state,
            )
            self.assertEqual("UNPROVEN", infrastructure["verdict"])
            self.assertEqual(
                ("UNPROVEN", 2),
                subject.classify_result(
                    True,
                    True,
                    [{"code": state, "detail": "x"}],
                    zero,
                    [infrastructure],
                ),
            )
        clean_control = {"aggregate": {"divergent_pixels": 0}}
        clean_mutant = {
            "buckets": [
                {
                    "name": self.contracts["mutations"][0]["expected_bucket"],
                    "population": 64,
                    "divergent_pixels": 0,
                }
            ]
        }
        self.assertEqual(
            "MISSED",
            subject._mutation_result(
                self.contracts["mutations"][0],
                clean_control,
                clean_mutant,
                64,
            )["verdict"],
        )

    def test_focused_scope_uses_only_relevant_mutations(self):
        all_targets = {item["name"] for item in self.contracts["contracts"]}
        selected = {"ambient_ibl_pass"}
        self.assertEqual("focused", subject._report_scope(selected, all_targets))
        relevant = subject._relevant_mutations(
            self.contracts["mutations"], selected
        )
        self.assertTrue(relevant)
        self.assertEqual(selected, {item["target"] for item in relevant})
        caught = [{"verdict": "CAUGHT"} for _ in relevant]
        self.assertEqual(
            ("PASS", 0),
            subject.classify_result(
                True, True, [], subject._measurement_metrics_zero(), caught
            ),
        )
        with self.assertRaises(subject.StableFailure):
            subject._validate_scope_artifacts("focused", "out", None)
        subject._validate_scope_artifacts("focused", None, None)

    def test_harness_self_test_when_built(self):
        if not os.path.isfile(self.harness_executable):
            if os.environ.get("FO4CS_REQUIRE_EXEC_HARNESS") == "1":
                self.fail("required shader execution harness is not built")
            self.skipTest("standalone harness is not built")
        result = subject._run_harness_self_test(
            self.harness_executable, self.harness_source_sha256
        )
        self.assertEqual("PASS", result["tests"]["binary16"])
        self.assertEqual("PASS", result["tests"]["dimension_hash"])
        self.assertEqual("PASS", result["tests"]["front_face_probe"])
        self.assertNotEqual(
            result["front_face_probe"]["clockwise_state_front"],
            result["front_face_probe"]["counter_clockwise_state_front"],
        )
        with self.assertRaises(subject.StableFailure) as context:
            subject._run_harness_self_test(
                self.harness_executable, "0" * 64
            )
        self.assertEqual("stale_harness", context.exception.code)

    def test_feature_failure_receipt_when_built(self):
        if not os.path.isfile(self.harness_executable):
            if os.environ.get("FO4CS_REQUIRE_EXEC_HARNESS") == "1":
                self.fail("required shader execution harness is not built")
            self.skipTest("standalone harness is not built")
        with tempfile.TemporaryDirectory() as temporary:
            missing = os.path.join(temporary, "missing.dxbc")
            receipt_path = os.path.join(temporary, "failure.json")
            process = subprocess.run(
                [
                    self.harness_executable,
                    missing,
                    missing,
                    "--wetness-feature",
                    "--stock-ibl",
                    missing,
                    "--feature-ibl",
                    missing,
                    "--mutant",
                    missing,
                    "--mutant-ibl",
                    missing,
                    "--measurement-json",
                    receipt_path,
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, process.returncode)
            receipt = json.loads(Path(receipt_path).read_text(encoding="utf-8"))
            self.assertEqual(
                subject.FEATURE_MEASUREMENT_SCHEMA, receipt["schema"]
            )
            self.assertEqual("additive-feature", receipt["evidence_class"])
            self.assertEqual(
                "reconstructed-stock-vs-reconstructed-feature",
                receipt["comparison"],
            )
            self.assertFalse(receipt["native_bytecode_used"])
            self.assertEqual("UNPROVEN", receipt["verdict"])
            self.assertNotIn("aggregate", receipt)
            feature = self.contracts["additive_features"]["wetness-effects"]
            validation = subject.validate_feature_measurement(
                receipt,
                "adversarial",
                feature,
                256,
                256,
                self.harness_source_sha256,
            )
            self.assertTrue(validation)
            self.assertFalse(
                any(
                    item["code"] == "feature_measurement_protocol"
                    for item in validation
                )
            )

    def test_feature_driver_failure_writes_additive_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = os.path.join(temporary, "failure.json")
            report, exit_code = subject.run(
                [
                    "--additive-feature",
                    "wetness-effects",
                    "--exe",
                    os.path.join(temporary, "missing.exe"),
                    "--json",
                    output,
                ]
            )
            self.assertEqual(2, exit_code)
            self.assertEqual("UNPROVEN", report["verdict"])
            self.assertEqual(subject.FEATURE_RUN_SCHEMA, report["schema"])
            self.assertEqual(
                subject._canonical_bytes(report), Path(output).read_bytes()
            )
            self.assertEqual(
                [
                    ("directional", "adversarial"),
                    ("directional", "native"),
                    ("ambient-runtime", "adversarial"),
                    ("ambient-runtime", "native"),
                ],
                [
                    (item["target"], item["fixture"])
                    for item in report["fixtures"]
                ],
            )
            for receipt in report["fixtures"]:
                self.assertEqual(
                    subject.FEATURE_MEASUREMENT_SCHEMA, receipt["schema"]
                )
                self.assertEqual(
                    "additive-feature", receipt["evidence_class"]
                )
                self.assertEqual(
                    "reconstructed-stock-vs-reconstructed-feature",
                    receipt["comparison"],
                )
                self.assertFalse(receipt["native_bytecode_used"])
                self.assertEqual("UNPROVEN", receipt["verdict"])
                self.assertNotIn("aggregate", receipt)
            for failure in report["failures"]:
                self.assertEqual(
                    subject.FEATURE_MEASUREMENT_SCHEMA, failure["schema"]
                )
                self.assertEqual("additive-feature", failure["evidence_class"])
                self.assertFalse(failure["native_bytecode_used"])

    def test_runtime_component_provenance_is_safe_and_sorted(self):
        if os.path.isfile(self.harness_executable):
            components = subject._run_harness_self_test(
                self.harness_executable, self.harness_source_sha256
            )["runtime_components"]
        else:
            components = [
                {
                    "name": "d3d10warp.dll",
                    "state": "available",
                    "version": "1.0.0.0",
                    "sha256": "0" * 64,
                    "size": 1,
                },
                {
                    "name": "d3d11.dll",
                    "state": "available",
                    "version": "1.0.0.0",
                    "sha256": "1" * 64,
                    "size": 1,
                },
            ]
        subject._validate_runtime_components(components)
        self.assertEqual(
            ["d3d10warp.dll", "d3d11.dll"],
            [item["name"] for item in components],
        )
        for component in components:
            self.assertIn(component["state"], {"available", "unavailable"})
            self.assertNotIn("path", component)
            if component["state"] == "available":
                self.assertRegex(component["sha256"], r"^[0-9a-f]{64}$")
                self.assertGreater(component["size"], 0)
                self.assertTrue(component["version"])
        subject._assert_receipt_safe({"components": components})
        unavailable = [dict(item) for item in components]
        unavailable[0] = {
            "name": "d3d10warp.dll",
            "state": "unavailable",
        }
        with self.assertRaises(subject.StableFailure) as unavailable_context:
            subject._validate_runtime_components(unavailable)
        self.assertEqual("runtime_identity", unavailable_context.exception.code)
        malformed = [dict(item) for item in components]
        malformed[1]["sha256"] = "not-a-hash"
        with self.assertRaises(subject.StableFailure) as malformed_context:
            subject._validate_runtime_components(malformed)
        self.assertEqual("runtime_identity", malformed_context.exception.code)
        unknown_version = [dict(item) for item in components]
        unknown_version[1]["version"] = "unknown"
        with self.assertRaises(subject.StableFailure) as version_context:
            subject._validate_runtime_components(unknown_version)
        self.assertEqual("runtime_identity", version_context.exception.code)

    def test_measurement_validation_is_structured(self):
        metrics = subject._measurement_metrics_zero()
        metrics["total_pixels"] = 64
        metrics["total_channels"] = 256
        measurement = {
            "schema": subject.MEASUREMENT_SCHEMA,
            "schema_version": 1,
            "harness_version": subject.HARNESS_VERSION,
            "source_sha256": self.harness_source_sha256,
            "profile": "ambient-ibl",
            "fixture": "adversarial",
            "width": 16,
            "height": 16,
            "execution_environment": {
                "driver_type": "WARP",
                "feature_level": "11_0",
                "runtime_fingerprint": "system-d3d11-runtime",
                "limitation": "runtime identity external",
            },
            "measurement_format": "R32G32B32A32_FLOAT",
            "generated_inputs_sha256": "0" * 64,
            "seed_base": 0,
            "seeds": [0],
            "scenario_seeds": [0, 1],
            "formats": [{"bind_point": 1}],
            "matrix_assertions": {"verdict": "PASS"},
            "aggregate": metrics,
            "buckets": [
                {
                    "name": "depth.equal",
                    "population": 64,
                    "required_minimum": 64,
                    **metrics,
                },
            ],
            "failures": [],
            "verdict": "PASS",
        }
        self.assertEqual(
            [],
            subject.validate_measurement(
                measurement,
                "ambient-ibl",
                "adversarial",
                ["depth.equal"],
                64,
                16,
                16,
                self.harness_source_sha256,
            ),
        )
        failures = subject.validate_measurement(
            measurement,
            "ambient-ibl",
            "adversarial",
            ["depth.equal", "ibl.on"],
            64,
            16,
            16,
            self.harness_source_sha256,
        )
        self.assertEqual("missing_bucket", failures[0]["code"])
        sparse = {
            "schema": subject.MEASUREMENT_SCHEMA,
            "schema_version": 1,
            "profile": "ambient-ibl",
            "fixture": "adversarial",
            "buckets": [],
        }
        self.assertTrue(
            subject.validate_measurement(
                sparse,
                "ambient-ibl",
                "adversarial",
                [],
                64,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        malformed = dict(measurement)
        malformed["failures"] = None
        result = subject.validate_harness_result(
            malformed,
            0,
            "ambient-ibl",
            "adversarial",
            ["depth.equal"],
            64,
            "x",
            16,
            16,
            self.harness_source_sha256,
        )
        self.assertTrue(
            any(item["code"] == "measurement_protocol" for item in result)
        )
        empty = dict(measurement)
        empty["aggregate"] = subject._measurement_metrics_zero()
        self.assertTrue(
            subject.validate_measurement(
                empty,
                "ambient-ibl",
                "adversarial",
                ["depth.equal"],
                64,
                16,
                16,
                self.harness_source_sha256,
            )
        )
        stale = dict(measurement)
        stale["source_sha256"] = "f" * 64
        self.assertTrue(
            any(
                item["code"] == "stale_harness"
                for item in subject.validate_measurement(
                    stale,
                    "ambient-ibl",
                    "adversarial",
                    ["depth.equal"],
                    64,
                    16,
                    16,
                    self.harness_source_sha256,
                )
            )
        )
        wrong_dimensions = dict(measurement)
        self.assertTrue(
            subject.validate_measurement(
                wrong_dimensions,
                "ambient-ibl",
                "adversarial",
                ["depth.equal"],
                64,
                17,
                16,
                self.harness_source_sha256,
            )
        )


def run_self_smoke():
    executable = os.environ.get(
        "FO4CS_EXEC_HARNESS", subject.DEFAULT_EXECUTABLE
    )
    source_sha256 = subject._hash_file(
        subject.HARNESS_SOURCE, include_sha1=False
    )["sha256"]
    subject._run_harness_self_test(executable, source_sha256)
    fxc = subject._find_fxc(None)
    contracts = json.loads(
        Path(subject.DEFAULT_CONTRACTS).read_text(encoding="utf-8")
    )
    manifest = json.loads(
        Path(subject.DEFAULT_MANIFEST).read_text(encoding="utf-8")
    )
    entries = {item["name"]: item for item in manifest["shaders"]}
    contract_by_name = {
        item["name"]: item for item in contracts["contracts"]
    }
    with tempfile.TemporaryDirectory() as temporary:
        for contract in contracts["contracts"]:
            entry = entries[contract["name"]]
            source = os.path.join(subject.REPO_ROOT, entry["src"])
            bytecode = os.path.join(
                temporary, f"{contract['name']}.dxbc"
            )
            compiled, log = subject._compile_shader(
                fxc,
                source,
                os.path.dirname(source),
                entry.get("defines", []),
                bytecode,
            )
            if not compiled:
                raise AssertionError(log)
            for fixture in subject.FIXTURE_ORDER:
                required = contracts["profiles"][contract["profile"]][
                    "required_buckets"
                ][fixture]
                measurement, return_code, human = subject._run_harness(
                    executable,
                    bytecode,
                    bytecode,
                    fixture,
                    contract["profile"],
                    required,
                    contracts["minimum_bucket_population"],
                    1,
                    31,
                    16,
                    16,
                    2e-3,
                    1e-2,
                    temporary,
                    f"{contract['name']}-{fixture}",
                    False,
                )
                failures = subject.validate_harness_result(
                    measurement,
                    return_code,
                    contract["profile"],
                    fixture,
                    required,
                    contracts["minimum_bucket_population"],
                    contract["name"],
                    16,
                    16,
                    source_sha256,
                )
                if return_code != 0 or failures:
                    raise AssertionError((failures, human))
                print(f"{contract['name']} {fixture}: PASS")

        for mutation in contracts["mutations"]:
            contract = contract_by_name[mutation["target"]]
            entry = entries[mutation["target"]]
            source = os.path.join(subject.REPO_ROOT, entry["src"])
            original = Path(source).read_text(encoding="utf-8-sig")
            control, mutant = subject.build_mutation_sources(
                original, mutation["target"], mutation["id"]
            )
            bytecodes = []
            for kind, text in (("control", control), ("mutant", mutant)):
                hlsl = os.path.join(
                    temporary, f"{mutation['id']}-{kind}.hlsl"
                )
                bytecode = os.path.join(
                    temporary, f"{mutation['id']}-{kind}.dxbc"
                )
                Path(hlsl).write_text(text, encoding="utf-8", newline="\n")
                compiled, log = subject._compile_shader(
                    fxc,
                    hlsl,
                    os.path.dirname(source),
                    entry.get("defines", []),
                    bytecode,
                )
                if not compiled:
                    raise AssertionError(log)
                bytecodes.append(bytecode)
            required = contracts["profiles"][contract["profile"]][
                "required_buckets"
            ][mutation["fixture"]]
            measurement, return_code, human = subject._run_harness(
                executable,
                bytecodes[0],
                bytecodes[1],
                mutation["fixture"],
                contract["profile"],
                required,
                contracts["minimum_bucket_population"],
                1,
                31,
                16,
                16,
                2e-3,
                1e-2,
                temporary,
                mutation["id"],
                False,
            )
            failures = subject.validate_harness_result(
                measurement,
                return_code,
                contract["profile"],
                mutation["fixture"],
                required,
                contracts["minimum_bucket_population"],
                mutation["id"],
                16,
                16,
                source_sha256,
            )
            bucket = next(
                item
                for item in measurement["buckets"]
                if item["name"] == mutation["expected_bucket"]
            )
            if (
                return_code != 1
                or failures
                or bucket["divergent_pixels"] == 0
                or (
                    mutation["class"] == "boundary-operator"
                    and any(
                        item["divergent_pixels"] != 0
                        for item in measurement["buckets"]
                        if item["name"] in {"depth.near", "depth.far"}
                    )
                )
            ):
                raise AssertionError((failures, bucket, human))
            print(f"{mutation['id']}: CAUGHT")


def run_wetness_smoke():
    executable = os.environ.get(
        "FO4CS_EXEC_HARNESS", subject.DEFAULT_EXECUTABLE
    )
    if not os.path.isfile(executable):
        raise AssertionError("required shader execution harness is not built")
    fxc = subject._find_fxc(None)
    with tempfile.TemporaryDirectory() as first_directory:
        with tempfile.TemporaryDirectory() as second_directory:
            for seed in (31, 32749, 65521):
                first_seed_directory = os.path.join(first_directory, str(seed))
                second_seed_directory = os.path.join(second_directory, str(seed))
                os.makedirs(first_seed_directory)
                os.makedirs(second_seed_directory)
                arguments = [
                    "--additive-feature",
                    "wetness-effects",
                    "--exe",
                    executable,
                    "--fxc",
                    fxc,
                    "--width",
                    "16",
                    "--height",
                    "16",
                    "--seeds",
                    "1",
                    "--seed-base",
                    str(seed),
                    "--manifest-dir",
                    first_seed_directory,
                ]
                first_report, first_code = subject.run(arguments)
                arguments[-1] = second_seed_directory
                second_report, second_code = subject.run(arguments)
                if first_code != 0 or second_code != 0:
                    raise AssertionError((first_report, second_report))
                first_path = os.path.join(
                    first_seed_directory, subject.FEATURE_MANIFEST_FILENAME
                )
                second_path = os.path.join(
                    second_seed_directory, subject.FEATURE_MANIFEST_FILENAME
                )
                if Path(first_path).read_bytes() != Path(second_path).read_bytes():
                    raise AssertionError(
                        "WetnessEffects manifest is not deterministic"
                    )
                if first_report != second_report:
                    raise AssertionError(
                        "WetnessEffects report is not deterministic"
                    )
                if (
                    first_report["schema"] != subject.FEATURE_RUN_SCHEMA
                    or first_report["evidence_class"] != "additive-feature"
                    or first_report["comparison"]
                    != "reconstructed-stock-vs-reconstructed-feature"
                    or first_report["native_bytecode_used"] is not False
                    or first_report["verdict"] != "PASS"
                ):
                    raise AssertionError(first_report)
                fixtures = first_report["fixtures"]
                if [
                    (item["target"], item["fixture"]) for item in fixtures
                ] != [
                    ("directional", "adversarial"),
                    ("directional", "native"),
                    ("ambient-runtime", "adversarial"),
                    ("ambient-runtime", "native"),
                ]:
                    raise AssertionError(fixtures)
                for fixture in fixtures:
                    expected_buckets = (
                        18
                        if fixture["target"] == "ambient-runtime"
                        else (12 if fixture["target"] == "directional" else 0)
                    )
                    if (
                        len(fixture["cross_buckets"]) != expected_buckets
                        or fixture["properties"]["neutral_identity"]["verdict"]
                        != "PASS"
                        or fixture["properties"]["active_locality"]["verdict"]
                        != "PASS"
                        or fixture["properties"]["magnitude"]["verdict"]
                        != "PASS"
                        or (
                            fixture["target"] == "ambient-runtime"
                            and fixture["properties"]["matte_sheen"]["verdict"]
                            != "PASS"
                        )
                        or (
                            fixture["target"] == "ambient-runtime"
                            and fixture["properties"]["no_ibl_film"]["verdict"]
                            != "PASS"
                        )
                        or (
                            fixture["target"] == "ambient-runtime"
                            and fixture["properties"]["ambient_film_blend"][
                                "verdict"
                            ]
                            != "PASS"
                        )
                        or fixture["properties"]["monotonicity"]["verdict"]
                        != "PASS"
                        or fixture["properties"]["mutation_sensitivity"]["verdict"]
                        != "CAUGHT"
                    ):
                        raise AssertionError(fixture)
            print("WetnessEffects additive-feature WARP multi-seed: PASS")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-smoke"]:
        run_self_smoke()
    elif sys.argv[1:] == ["--wetness-smoke"]:
        run_wetness_smoke()
    else:
        unittest.main()
