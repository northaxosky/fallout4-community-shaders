from bindings import decode_cbuffer, output_targets, rasterizer_record
from common import resource_id
from resources import texture_stats, usage_name, usage_records


def _finding(rank, severity, kind, message, evidence, event_id, resource=None,
             marker=None):
    return {
        "rank": rank,
        "severity": severity,
        "kind": kind,
        "message": message,
        "eventId": event_id,
        "marker": marker,
        "resourceId": resource_id(resource.resourceId) if resource else None,
        "resourceName": resource.name if resource else None,
        "evidence": evidence,
    }


def _texture_findings(session, actions):
    findings = []
    anomaly_outputs = []
    unused_resources = []
    unconsumed_uniform_resources = []
    unconsumed_non_finite_resources = []
    analysis_errors = []
    seen = set()
    for resource in session.app_named_resources():
        rid = resource_id(resource.resourceId)
        if rid in seen or session.texture_description(rid) is None:
            continue
        seen.add(rid)
        try:
            stats = texture_stats(session, actions, rid, include_histogram=False)
        except Exception as error:
            analysis_errors.append({
                "resourceId": rid,
                "resourceName": resource.name,
                "error": str(error),
            })
            continue
        timeline = usage_records(session, actions, rid)
        reads = [item for item in timeline if item["reads"]]
        next_event = timeline[-1]["eventId"] if timeline else None
        next_marker = timeline[-1]["marker"] if timeline else None
        output_usage = next((item for item in reversed(timeline)
                             if item["producerWrite"] or item["usage"] == "Clear"),
                            None)
        output_event = output_usage["eventId"] if output_usage else next_event
        output_marker = output_usage["marker"] if output_usage else next_marker
        if stats["has_inf"] and stats["lifetime_verdict_available"]:
            if reads:
                findings.append(_finding(
                    100, "error", "infinity",
                    "A read texture contains an infinite value.",
                    {"minimum": stats["minimum"], "maximum": stats["maximum"]},
                    reads[0]["eventId"], resource, reads[0]["marker"]))
                marker = reads[0]["marker"] or output_marker
                if marker is not None:
                    anomaly_outputs.append((reads[0]["eventId"], resource,
                                            stats, marker))
            else:
                unconsumed_non_finite_resources.append(resource.name)
        if stats["only_cleared_then_read"]:
            clear_event = stats["clear_then_read"]["clearEventId"]
            read_event = stats["clear_then_read"]["readEventId"]
            clear = next((item for item in timeline
                          if item["eventId"] == clear_event), None)
            read = next((item for item in timeline
                         if item["eventId"] == read_event), None)
            findings.append(_finding(
                98, "error", "cleared_then_read",
                "Texture was cleared and read without a producer write.",
                {"clearEventId": clear["eventId"] if clear else None,
                 "readEventId": read["eventId"] if read else None,
                 "uniformValue": stats["uniform_value"]},
                read["eventId"] if read else next_event, resource,
                output_marker))
            if output_marker is not None:
                anomaly_outputs.append((output_event, resource, stats,
                                        output_marker))
        elif stats["never_written"]:
            if reads:
                first_read = reads[0]
                findings.append(_finding(
                    80, "warning", "not_produced_this_frame",
                    "Texture was read without an in-frame producer; it may be persistent.",
                    {"firstReadEventId": first_read["eventId"],
                     "usages": timeline[:12], "uniform": stats["uniform"]},
                    first_read["eventId"], resource, first_read["marker"]))
                marker = first_read["marker"] or output_marker
                if marker is not None:
                    anomaly_outputs.append((first_read["eventId"], resource,
                                            stats, marker))
            else:
                unused_resources.append(resource.name)
        elif stats["uniform"] and stats["lifetime_verdict_available"]:
            write = next((item for item in reversed(timeline)
                          if item["producerWrite"]), None)
            later_reads = [item for item in reads
                           if write is not None
                           and item["eventId"] > write["eventId"]]
            if later_reads:
                first_read = later_reads[0]
                findings.append(_finding(
                    72, "warning", "uniform_after_write",
                    "Texture was written uniformly and then read; the value may be intentional.",
                    {"uniformValue": stats["uniform_value"],
                     "writeEventId": write["eventId"],
                     "firstReadEventId": first_read["eventId"]},
                    first_read["eventId"], resource, first_read["marker"]))
                marker = first_read["marker"] or output_marker
                if marker is not None:
                    anomaly_outputs.append((first_read["eventId"], resource,
                                            stats, marker))
            else:
                unconsumed_uniform_resources.append(resource.name)
    anomaly_outputs.sort(key=lambda item: item[0])
    if anomaly_outputs:
        event_id, resource, stats, marker = anomaly_outputs[0]
        findings.append(_finding(
            82, "warning", "first_bad_region_output",
            "This is the earliest marker-associated anomalous output.",
            {"uniform": stats["uniform"],
             "uniformValue": stats["uniform_value"],
             "neverWritten": stats["never_written"]},
            event_id, resource, marker))
    return (findings, sorted(unused_resources),
            sorted(unconsumed_uniform_resources),
            sorted(unconsumed_non_finite_resources), analysis_errors)


def _resolution_observations(session, actions, upscale_size, expected_scale):
    candidate_events = set()
    for texture in session.textures:
        if [texture.width, texture.height] != upscale_size[:2]:
            continue
        for usage in session.usage(texture.resourceId):
            name = usage_name(usage.usage)
            action = actions.by_event.get(usage.eventId)
            if (action is not None and action["kind"] == "drawcall"
                    and ("ColorTarget" in name or "DepthStencil" in name)):
                candidate_events.add(usage.eventId)
    groups = {}
    matching_samples = 0
    for event_id in sorted(candidate_events, reverse=True)[:24]:
        session.controller.SetFrameEvent(event_id, False)
        state = session.controller.GetD3D11PipelineState()
        targets = output_targets(session, state)
        rasterizer = rasterizer_record(state)
        if not targets or not rasterizer["viewports"]:
            continue
        target_binding = next(
            (item for item in targets
             if session.texture_description(item["resourceId"]) is not None
             and [session.texture_description(item["resourceId"]).width,
                  session.texture_description(item["resourceId"]).height]
             == upscale_size[:2]),
            None)
        if target_binding is None:
            continue
        target = session.texture_description(target_binding["resourceId"])
        viewport = rasterizer["viewports"][0]
        x_ratio = float(viewport["width"]) / float(target.width)
        y_ratio = float(viewport["height"]) / float(target.height)
        if x_ratio <= 0.1 or abs(x_ratio - y_ratio) > 0.02:
            continue
        key = round((x_ratio + y_ratio) * 0.5, 3)
        sample = {
            "eventId": event_id,
            "marker": actions.marker_name_for_event(event_id),
            "targetId": target_binding["resourceId"],
            "targetName": target_binding["resourceName"],
            "targetSize": [target.width, target.height],
            "viewport": viewport,
            "scissor": rasterizer["scissors"][0]
            if rasterizer["scissors"] else None,
        }
        group = groups.setdefault(key, {"count": 0, "samples": []})
        group["count"] += 1
        if len(group["samples"]) < 5:
            group["samples"].append(sample)
        if abs(key - expected_scale) < 0.01:
            matching_samples += 1
            if matching_samples >= 5:
                break
    observations = []
    for factor, group in groups.items():
        if group["count"] >= 2:
            observations.append({
                "scaleFactor": factor,
                "sampledEvents": group["count"],
                "samples": group["samples"],
            })
    observations.sort(key=lambda item: -item["sampledEvents"])
    return observations


def _find_resolution_constants(session, actions):
    candidates = [action for action in actions.actions
                  if action["kind"] == "dispatch"
                  and actions.marker_name_for_event(action["eventId"])
                  == "Upscaling/FSR"]
    for action in candidates[:8]:
        if (action["kind"] != "dispatch"
                or actions.marker_name_for_event(action["eventId"]) != "Upscaling/FSR"):
            continue
        session.controller.SetFrameEvent(action["eventId"], False)
        shader = session.controller.GetD3D11PipelineState().computeShader
        if session.name(shader.resourceId) == "FSR3-PREPARE-INPUTS":
            decoded = decode_cbuffer(session, action["eventId"], "compute", 0)
            return decoded
    return None


def _resolution_contract(session, actions):
    decoded = _find_resolution_constants(session, actions)
    result = {
        "status": "unavailable",
        "observedScales": [],
        "constantBuffer": None,
    }
    if decoded is None:
        return result, []
    variables = dict((item["name"], item["values"])
                     for item in decoded["variables"])
    render_size = variables.get("iRenderSize", [])
    upscale_size = variables.get("iUpscaleSize", [])
    downscale = variables.get("fDownscaleFactor", [])
    evidence = {
        "eventId": decoded["eventId"],
        "shaderName": decoded["shaderName"],
        "bufferName": decoded["bufferName"],
        "bufferByteOffset": decoded["byteOffset"],
        "bufferByteSize": decoded["byteSize"],
        "renderSize": render_size,
        "upscaleSize": upscale_size,
        "downscaleFactor": downscale,
    }
    result["constantBuffer"] = evidence
    if len(render_size) < 2 or len(upscale_size) < 2 or len(downscale) < 2:
        return result, []
    expected_x = float(render_size[0]) / float(upscale_size[0])
    expected_y = float(render_size[1]) / float(upscale_size[1])
    observations = _resolution_observations(
        session, actions, upscale_size, (expected_x + expected_y) * 0.5)
    result["observedScales"] = observations
    expected_matches_cbuffer = (
        abs(expected_x - downscale[0]) < 0.002
        and abs(expected_y - downscale[1]) < 0.002)
    dominant = min(
        observations,
        key=lambda item: abs(item["scaleFactor"] - expected_x)
    ) if observations else None
    allocation_matches = False
    if dominant is not None:
        sample = dominant["samples"][0]
        allocation_matches = (
            abs(dominant["scaleFactor"] - expected_x) < 0.01
            and sample["targetSize"] == upscale_size[:2]
            and abs(sample["viewport"]["width"] - render_size[0]) < 1.0
            and abs(sample["viewport"]["height"] - render_size[1]) < 1.0)
    elif expected_x >= 0.99 and expected_y >= 0.99:
        allocation_matches = True
    evidence["expectedScale"] = [expected_x, expected_y]
    evidence["dominantObservation"] = dominant
    if expected_matches_cbuffer and allocation_matches:
        result["status"] = "holding"
        return result, []
    result["status"] = "mismatch"
    finding = _finding(
        96, "error", "render_resolution_mismatch",
        "Render size, upscale allocation, viewport, and downscale factor disagree.",
        evidence, decoded["eventId"], None, "Upscaling/FSR")
    return result, [finding]


def run_triage(session, actions):
    (findings, unused_resources, unconsumed_uniform_resources,
     unconsumed_non_finite_resources, analysis_errors) = _texture_findings(
         session, actions)
    contract, contract_findings = _resolution_contract(session, actions)
    findings.extend(contract_findings)
    findings.sort(key=lambda item: (-item["rank"],
                                   item["eventId"] if item["eventId"] else 0))
    for index, item in enumerate(findings):
        item["order"] = index + 1
    return {
        "texturesChecked": len(set(
            resource_id(item.resourceId) for item in session.app_named_resources()
            if session.texture_description(item.resourceId) is not None)),
        "findings": findings,
        "nextEventId": findings[0]["eventId"] if findings else None,
        "renderResolutionContract": contract,
        "unusedResources": unused_resources,
        "unconsumedUniformResources": unconsumed_uniform_resources,
        "unconsumedNonFiniteResources": unconsumed_non_finite_resources,
        "analysisErrors": analysis_errors,
    }
