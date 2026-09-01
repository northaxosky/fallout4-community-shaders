import math
import os

import renderdoc as rd

from bindings import descriptor_bindings, output_targets
from common import enum_name, finite, format_name, resource_id, result_message, result_ok, sanitize_filename, safe_get, vector


WRITE_USAGES = (
    "CopyDst", "ResolveDst", "ColorTarget", "DepthStencil", "StreamOut",
    "RWResource", "GenMips", "CPUWrite",
)


def usage_name(value):
    return enum_name(value)


def is_clear_usage(name):
    return name == "Clear"


def is_write_usage(name):
    return is_clear_usage(name) or any(token in name for token in WRITE_USAGES)


def is_producer_usage(name):
    return not is_clear_usage(name) and is_write_usage(name)


def is_read_usage(name):
    return not is_write_usage(name) and name != "Discard"


def usage_records(session, actions, rid):
    records = []
    for item in session.usage(rid):
        name = usage_name(item.usage)
        records.append({
            "eventId": item.eventId,
            "usage": name,
            "writes": is_write_usage(name),
            "producerWrite": is_producer_usage(name),
            "reads": is_read_usage(name),
            "discards": name == "Discard",
            "marker": actions.marker_name_for_event(item.eventId),
        })
    return records


def component_count(texture):
    fmt = safe_get(texture, "format")
    return max(1, min(4, int(safe_get(fmt, "compCount", 4) or 4)))


def _value_components(value, texture):
    count = component_count(texture)
    comp_type = enum_name(safe_get(safe_get(texture, "format"), "compType", ""))
    if comp_type in ("UInt", "UNorm", "UNormSRGB", "Depth"):
        if comp_type == "UInt":
            return vector(value.uintValue, count), "uint"
        return vector(value.floatValue, count), "float"
    if comp_type in ("SInt", "SNorm"):
        if comp_type == "SInt":
            return vector(value.intValue, count), "int"
        return vector(value.floatValue, count), "float"
    return vector(value.floatValue, count), "float"


def texture_stats(session, actions, rid, event_id=None, mip=0, slice_index=0,
                  include_histogram=True):
    texture = session.texture_description(rid)
    if texture is None:
        raise ValueError("Resource is not a texture: " + session.name(rid))
    if event_id is not None:
        session.controller.SetFrameEvent(event_id, True)
    sub = rd.Subresource(mip, slice_index, 0)
    minimum, maximum = session.controller.GetMinMax(
        texture.resourceId, sub, rd.CompType.Typeless)
    mins, value_type = _value_components(minimum, texture)
    maxs, _ = _value_components(maximum, texture)
    values = mins + maxs
    has_inf = any(isinstance(value, float) and math.isinf(value)
                  for value in values)
    uniform = len(mins) == len(maxs) and all(
        mins[index] == maxs[index] for index in range(len(mins)))
    all_zero = bool(values) and all(value == 0 for value in values)
    timeline = usage_records(session, actions, rid)
    producer_writes = [item for item in timeline if item["producerWrite"]]
    clear_then_read = None
    active_clear = None
    for item in timeline:
        if item["usage"] == "Clear":
            active_clear = item
        elif item["producerWrite"]:
            active_clear = None
        elif active_clear is not None and item["reads"]:
            clear_then_read = {
                "clearEventId": active_clear["eventId"],
                "readEventId": item["eventId"],
            }
            break
    lifetime_available = (
        int(safe_get(texture, "mips", 1) or 1) == 1
        and int(safe_get(texture, "arraysize", 1) or 1) == 1
        and int(safe_get(texture, "depth", 1) or 1) == 1)
    record = {
        "resourceId": resource_id(texture.resourceId),
        "resourceName": session.name(rid),
        "eventId": event_id,
        "mip": mip,
        "slice": slice_index,
        "format": format_name(texture.format),
        "minimum": mins,
        "maximum": maxs,
        "valueType": value_type,
        "uniform": uniform,
        "uniform_value": mins if uniform else None,
        "all_zero": all_zero,
        "has_inf": has_inf,
        "nan_status": "unknown_minmax_does_not_preserve_nan",
        "content_note": (
            "Uniform and zero verdicts cover finite extrema; NaN presence is unknown."),
        "never_written": not producer_writes if lifetime_available else None,
        "only_cleared_then_read": (
            clear_then_read is not None if lifetime_available else None),
        "clear_then_read": clear_then_read if lifetime_available else None,
        "lifetime_verdict_available": lifetime_available,
        "lifetime_note": (
            None if lifetime_available
            else "Usage is resource-wide; lifetime verdicts are suppressed for subresources."),
        "usageCount": len(timeline),
    }
    if include_histogram:
        low_values = finite(mins)
        high_values = finite(maxs)
        low = float(min(low_values)) if low_values else 0.0
        high = float(max(high_values)) if high_values else 1.0
        if high <= low:
            high = low + 1.0
        channels = [index < component_count(texture) for index in range(4)]
        histogram = session.controller.GetHistogram(
            texture.resourceId, sub, rd.CompType.Typeless,
            low, high, channels)
        record["histogram"] = {
            "minimum": low,
            "maximum": high,
            "channels": channels,
            "buckets": list(histogram),
        }
    return record


def _auto_range(session, rid, mip, slice_index):
    texture = session.texture_description(rid)
    sub = rd.Subresource(mip, slice_index, 0)
    minimum, maximum = session.controller.GetMinMax(
        texture.resourceId, sub, rd.CompType.Typeless)
    mins, _ = _value_components(minimum, texture)
    maxs, _ = _value_components(maximum, texture)
    lows = finite(mins[:3])
    highs = finite(maxs[:3])
    black = float(min(lows)) if lows else 0.0
    white = float(max(highs)) if highs else 1.0
    if white <= black:
        white = black + 1.0
    return black, white


def dump_bound_textures(session, event_id, out_dir):
    session.controller.SetFrameEvent(event_id, True)
    state = session.controller.GetD3D11PipelineState()
    bindings = output_targets(session, state)
    bindings.extend(item for item in descriptor_bindings(session, state)
                    if item["type"] in ("Image", "ReadWriteImage"))
    manifest = []
    occurrence = {}
    for binding in bindings:
        rid = binding["resourceId"]
        texture = session.texture_description(rid)
        if texture is None:
            continue
        binding_type = binding["type"]
        stage = binding.get("stage", "output_merger")
        slot = binding.get("slot", 0)
        base = "{}-{}-{}-{}".format(stage, binding_type, slot,
                                    sanitize_filename(session.name(rid)))
        occurrence[base] = occurrence.get(base, 0) + 1
        if occurrence[base] > 1:
            base += "-{}".format(occurrence[base])
        path = os.path.join(out_dir, base + ".png")
        mip = int(binding.get("firstMip", 0) or 0)
        slice_index = int(binding.get("firstSlice", 0) or 0)
        black, white = _auto_range(session, rid, mip, slice_index)
        save = rd.TextureSave()
        save.resourceId = texture.resourceId
        save.mip = mip
        save.slice.sliceIndex = slice_index
        save.destType = rd.FileType.PNG
        save.comp.blackPoint = black
        save.comp.whitePoint = white
        if hasattr(rd, "AlphaMapping"):
            save.alpha = rd.AlphaMapping.Discard
        if binding_type == "depth_target":
            save.channelExtract = 0
        status = session.controller.SaveTexture(save, path)
        entry = dict(binding)
        entry.update({
            "file": os.path.basename(path),
            "path": path,
            "blackPoint": black,
            "whitePoint": white,
            "exportedMip": mip,
            "exportedSlice": slice_index,
            "viewSpansMultipleSubresources": (
                binding.get("numMips", 1) != 1
                or binding.get("numSlices", 1) != 1),
            "saved": result_ok(status),
        })
        if not result_ok(status):
            entry["error"] = result_message(status)
        manifest.append(entry)
    return manifest
