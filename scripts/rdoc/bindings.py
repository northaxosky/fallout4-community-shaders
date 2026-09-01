import renderdoc as rd

from common import enum_name, format_name, resource_id, resource_ref, safe_get, vector


STAGES = (
    ("vertex", "vertexShader"),
    ("hull", "hullShader"),
    ("domain", "domainShader"),
    ("geometry", "geometryShader"),
    ("pixel", "pixelShader"),
    ("compute", "computeShader"),
)

STAGE_ALIASES = {
    "vs": "vertex",
    "hs": "hull",
    "ds": "domain",
    "gs": "geometry",
    "ps": "pixel",
    "cs": "compute",
}


def _range_for(access, state):
    item = rd.DescriptorRange()
    item.descriptorSize = state.descriptorByteSize
    item.offset = access.byteOffset
    item.count = 1
    return item


def _fixed_bind_slot(state, access, kind):
    try:
        _, shader = stage_shader(state, enum_name(access.stage).lower())
        reflection = shader.reflection
        collections = {
            "ConstantBuffer": reflection.constantBlocks,
            "Sampler": reflection.samplers,
            "Image": reflection.readOnlyResources,
            "ReadWriteImage": reflection.readWriteResources,
        }
        items = collections.get(kind)
        if items is not None and access.index < len(items):
            fixed = safe_get(items[access.index], "fixedBindNumber", -1)
            if fixed >= 0:
                return fixed + access.arrayElement
    except Exception:
        pass
    return access.index


def _descriptor_record(session, state, access, descriptor):
    rid = resource_id(resource_ref(descriptor))
    kind = enum_name(access.type)
    return {
        "stage": enum_name(access.stage).lower(),
        "type": kind,
        "slot": _fixed_bind_slot(state, access, kind),
        "reflectionIndex": access.index,
        "arrayElement": access.arrayElement,
        "resourceId": rid,
        "resourceName": session.name(rid),
        "format": format_name(safe_get(descriptor, "format")),
        "byteOffset": safe_get(descriptor, "byteOffset", 0),
        "byteSize": safe_get(descriptor, "byteSize", 0),
        "firstMip": safe_get(descriptor, "firstMip", 0),
        "numMips": safe_get(descriptor, "numMips", 0),
        "firstSlice": safe_get(descriptor, "firstSlice", 0),
        "numSlices": safe_get(descriptor, "numSlices", 0),
        "staticallyUnused": bool(access.staticallyUnused),
    }


def descriptor_bindings(session, state):
    records = []
    for access in session.controller.GetDescriptorAccess():
        kind = enum_name(access.type)
        item_range = _range_for(access, state)
        if kind == "Sampler":
            samplers = session.controller.GetSamplerDescriptors(
                access.descriptorStore, [item_range])
            sampler = samplers[0] if samplers else None
            records.append({
                "stage": enum_name(access.stage).lower(),
                "type": kind,
                "slot": _fixed_bind_slot(state, access, kind),
                "reflectionIndex": access.index,
                "arrayElement": access.arrayElement,
                "resourceId": 0,
                "resourceName": "Sampler",
                "addressU": enum_name(safe_get(sampler, "addressU", "")),
                "addressV": enum_name(safe_get(sampler, "addressV", "")),
                "addressW": enum_name(safe_get(sampler, "addressW", "")),
                "filter": enum_name(safe_get(sampler, "filter", "")),
                "staticallyUnused": bool(access.staticallyUnused),
            })
            continue
        descriptors = session.controller.GetDescriptors(
            access.descriptorStore, [item_range])
        if descriptors:
            records.append(_descriptor_record(
                session, state, access, descriptors[0]))
    return records


def stage_shader(state, name):
    lowered = STAGE_ALIASES.get(name.lower(), name.lower())
    for stage_name, member in STAGES:
        if stage_name == lowered:
            return stage_name, getattr(state, member)
    raise ValueError("Unknown shader stage: " + name)


def _variable_record(variable):
    rows = int(safe_get(variable, "rows", 1) or 1)
    columns = int(safe_get(variable, "columns", 1) or 1)
    count = max(1, rows * columns)
    value_type = enum_name(safe_get(variable, "type", ""))
    value = safe_get(variable, "value")
    if value_type == "SInt":
        values = vector(safe_get(value, "s32v"), count)
    elif value_type in ("UInt", "Bool"):
        values = vector(safe_get(value, "u32v"), count)
    elif value_type == "SLong":
        values = vector(safe_get(value, "s64v"), count)
    elif value_type == "ULong":
        values = vector(safe_get(value, "u64v"), count)
    elif value_type == "Double":
        values = vector(safe_get(value, "f64v"), count)
    else:
        values = vector(safe_get(value, "f32v"), count)
    return {
        "name": variable.name,
        "type": value_type,
        "rows": rows,
        "columns": columns,
        "values": values,
        "members": [_variable_record(item)
                    for item in safe_get(variable, "members", [])],
    }


def decode_cbuffer(session, event_id, stage, slot):
    session.controller.SetFrameEvent(event_id, False)
    state = session.controller.GetD3D11PipelineState()
    stage_name, shader = stage_shader(state, stage)
    if resource_id(shader.resourceId) == 0 or shader.reflection is None:
        raise ValueError("No {} shader is bound at event {}".format(
            stage_name, event_id))
    matches = [item for item in descriptor_bindings(session, state)
               if item["type"] == "ConstantBuffer"
               and item["stage"] == stage_name and item["slot"] == slot]
    if not matches:
        raise ValueError("No constant buffer is bound to {} slot {}".format(
            stage_name, slot))
    binding = matches[0]
    buffer_id = binding["resourceId"]
    if buffer_id == 0:
        raise ValueError("Constant buffer slot {} is null".format(slot))
    blocks = list(shader.reflection.constantBlocks)
    block_matches = [
        (index, block) for index, block in enumerate(blocks)
        if safe_get(block, "fixedBindNumber",
                    safe_get(block, "bindPoint", -1)) <= slot
        < safe_get(block, "fixedBindNumber",
                   safe_get(block, "bindPoint", -1))
        + int(safe_get(block, "bindArraySize", 1) or 1)]
    if not block_matches:
        available = ["{}@{}".format(
            block.name,
            safe_get(block, "fixedBindNumber",
                     safe_get(block, "bindPoint", -1))) for block in blocks]
        raise ValueError(
            "No reflected constant block is bound at slot {}; available={}".format(
                slot, ",".join(available)))
    block_index, block = block_matches[0]
    if binding["reflectionIndex"] != block_index:
        raise ValueError("Descriptor and reflection indices disagree at slot {}".format(
            slot))
    length = binding["byteSize"] or block.byteSize
    pipeline = session.controller.GetPipelineState().GetGraphicsPipelineObject()
    variables = session.controller.GetCBufferVariableContents(
        pipeline, shader.resourceId, shader.stage, shader.reflection.entryPoint,
        block_index, session.resource_by_id[buffer_id].resourceId,
        binding["byteOffset"], length)
    return {
        "eventId": event_id,
        "stage": stage_name,
        "slot": slot,
        "reflectionIndex": block_index,
        "blockName": block.name,
        "shaderId": resource_id(shader.resourceId),
        "shaderName": session.name(shader.resourceId),
        "entryPoint": shader.reflection.entryPoint,
        "bufferId": buffer_id,
        "bufferName": session.name(buffer_id),
        "byteOffset": binding["byteOffset"],
        "byteSize": length,
        "variables": [_variable_record(item) for item in variables],
    }


def _bound_view(session, value, binding_type, slot):
    rid = resource_id(resource_ref(value))
    return {
        "type": binding_type,
        "slot": slot,
        "resourceId": rid,
        "resourceName": session.name(rid),
        "format": format_name(safe_get(value, "format")),
        "firstMip": safe_get(value, "firstMip", 0),
        "numMips": safe_get(value, "numMips", 0),
        "firstSlice": safe_get(value, "firstSlice", 0),
        "numSlices": safe_get(value, "numSlices", 0),
    }


def output_targets(session, state):
    records = []
    for slot, target in enumerate(state.outputMerger.renderTargets):
        rid = resource_id(resource_ref(target))
        if rid:
            records.append(_bound_view(session, target, "render_target", slot))
    depth = state.outputMerger.depthTarget
    rid = resource_id(resource_ref(depth))
    if rid:
        records.append(_bound_view(session, depth, "depth_target", 0))
    return records


def shader_records(session, state):
    records = []
    for stage_name, member in STAGES:
        shader = getattr(state, member)
        rid = resource_id(shader.resourceId)
        if rid == 0:
            continue
        reflection = shader.reflection
        records.append({
            "stage": stage_name,
            "resourceId": rid,
            "resourceName": session.name(rid),
            "entryPoint": safe_get(reflection, "entryPoint", "") if reflection else "",
        })
    return records


def rasterizer_record(state):
    viewports = []
    for item in state.rasterizer.viewports:
        viewports.append({
            "x": item.x,
            "y": item.y,
            "width": item.width,
            "height": item.height,
            "minDepth": item.minDepth,
            "maxDepth": item.maxDepth,
        })
    scissors = []
    for item in state.rasterizer.scissors:
        left = safe_get(item, "left", safe_get(item, "x", 0))
        top = safe_get(item, "top", safe_get(item, "y", 0))
        width = safe_get(item, "width")
        height = safe_get(item, "height")
        right = safe_get(item, "right",
                         left + width if width is not None else left)
        bottom = safe_get(item, "bottom",
                          top + height if height is not None else top)
        scissors.append({
            "left": left,
            "top": top,
            "right": right,
            "bottom": bottom,
            "width": width if width is not None else right - left,
            "height": height if height is not None else bottom - top,
        })
    return {"viewports": viewports, "scissors": scissors}


def state_record(session, event_id):
    session.controller.SetFrameEvent(event_id, True)
    state = session.controller.GetD3D11PipelineState()
    bindings = descriptor_bindings(session, state)
    return {
        "eventId": event_id,
        "shaders": shader_records(session, state),
        "bindings": bindings,
        "constantBuffers": [item for item in bindings
                            if item["type"] == "ConstantBuffer"],
        "srvs": [item for item in bindings if item["type"] == "Image"],
        "uavs": [item for item in bindings
                 if item["type"] == "ReadWriteImage"],
        "samplers": [item for item in bindings if item["type"] == "Sampler"],
        "outputTargets": output_targets(session, state),
        "rasterizer": rasterizer_record(state),
    }
