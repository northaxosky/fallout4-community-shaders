import renderdoc as rd

from common import enum_name, format_name, resource_id, resource_ref, safe_get, vector


STAGES = (
    ("vertex", rd.ShaderStage.Vertex),
    ("hull", rd.ShaderStage.Hull),
    ("domain", rd.ShaderStage.Domain),
    ("geometry", rd.ShaderStage.Geometry),
    ("pixel", rd.ShaderStage.Pixel),
    ("compute", rd.ShaderStage.Compute),
    ("task", rd.ShaderStage.Task),
    ("mesh", rd.ShaderStage.Mesh),
)

STAGE_ALIASES = {
    "vs": "vertex",
    "hs": "hull",
    "ds": "domain",
    "gs": "geometry",
    "ps": "pixel",
    "cs": "compute",
    "as": "task",
    "amplification": "task",
    "ms": "mesh",
}

_STAGE_BY_NAME = dict(STAGES)
_STAGE_NAME_BY_ENUM = dict((enum_name(value).lower(), name)
                           for name, value in STAGES)


class ShaderState(object):
    def __init__(self, resource, reflection, stage, entry_point):
        self.resourceId = resource
        self.reflection = reflection
        self.stage = stage
        self.entryPoint = entry_point


class PipelineAdapter(object):
    def __init__(self, session):
        self.session = session
        self.controller = session.controller
        self.state = self.controller.GetPipelineState()
        api = self.controller.GetAPIProperties()
        self.api = enum_name(safe_get(api, "pipelineType", ""))
        self.d3d11 = (self.controller.GetD3D11PipelineState()
                      if self.state.IsCaptureD3D11() else None)
        self.d3d12 = (self.controller.GetD3D12PipelineState()
                      if self.state.IsCaptureD3D12() else None)
        self._descriptor_stores = None

    @classmethod
    def at_event(cls, session, event_id, force=True):
        session.controller.SetFrameEvent(event_id, force)
        return cls(session)

    def shader(self, name):
        stage_name = STAGE_ALIASES.get(name.lower(), name.lower())
        stage = _STAGE_BY_NAME.get(stage_name)
        if stage is None:
            raise ValueError("Unknown shader stage: " + name)
        return stage_name, ShaderState(
            self.state.GetShader(stage),
            self.state.GetShaderReflection(stage),
            stage,
            self.state.GetShaderEntryPoint(stage))

    def pipeline_object(self, stage):
        stage_name = STAGE_ALIASES.get(stage.lower(), stage.lower())
        if stage_name == "compute":
            return self.state.GetComputePipelineObject()
        return self.state.GetGraphicsPipelineObject()

    def descriptor_store(self, store):
        if self._descriptor_stores is None:
            try:
                stores = self.controller.GetDescriptorStores()
            except Exception:
                stores = []
            self._descriptor_stores = dict(
                (resource_id(item.resourceId), item) for item in stores)
        return self._descriptor_stores.get(resource_id(store))

    def rasterizer(self):
        native = self.d3d12 if self.d3d12 is not None else self.d3d11
        return safe_get(native, "rasterizer")


def pipeline_state(session, event_id=None, force=True):
    if event_id is not None:
        return PipelineAdapter.at_event(session, event_id, force)
    return PipelineAdapter(session)


def _stage_name(stage):
    return _STAGE_NAME_BY_ENUM.get(
        enum_name(stage).lower(), enum_name(stage).lower())


def _descriptor_category(descriptor_type):
    if rd.IsConstantBlockDescriptor(descriptor_type):
        return "ConstantBuffer"
    if rd.IsSamplerDescriptor(descriptor_type):
        return "Sampler"
    if rd.IsReadOnlyDescriptor(descriptor_type):
        return "Image"
    if rd.IsReadWriteDescriptor(descriptor_type):
        return "ReadWriteImage"
    return enum_name(descriptor_type)


def _reflection_collection(reflection, descriptor_type):
    if reflection is None:
        return None
    category = enum_name(rd.CategoryForDescriptorType(descriptor_type))
    collections = {
        "ConstantBlock": reflection.constantBlocks,
        "Sampler": reflection.samplers,
        "ReadOnlyResource": reflection.readOnlyResources,
        "ReadWriteResource": reflection.readWriteResources,
    }
    return collections.get(category)


def _logical_location(adapter, access):
    try:
        locations = adapter.controller.GetDescriptorLocations(
            access.descriptorStore, [rd.DescriptorRange(access)])
        return locations[0] if locations else None
    except Exception:
        return None


def _reflected_binding(adapter, access):
    stage_name = _stage_name(access.stage)
    try:
        _, shader = adapter.shader(stage_name)
    except ValueError:
        shader = None
    reflection = shader.reflection if shader is not None else None
    collection = _reflection_collection(reflection, access.type)
    no_binding = int(access.index) == int(rd.DescriptorAccess.NoShaderBinding)
    if not no_binding and collection is not None and access.index < len(collection):
        item = collection[access.index]
        register = int(safe_get(item, "fixedBindNumber", -1))
        if register >= 0:
            return {
                "available": True,
                "status": "reflected",
                "register": register + int(access.arrayElement),
                "registerSpace": int(
                    safe_get(item, "fixedBindSetOrSpace", 0) or 0),
                "name": safe_get(item, "name", ""),
            }
    if adapter.d3d11 is not None:
        location = _logical_location(adapter, access)
        register = int(safe_get(location, "fixedBindNumber", -1))
        if register >= 0:
            return {
                "available": True,
                "status": "api_fixed_slot",
                "register": register,
                "registerSpace": 0,
                "name": safe_get(location, "logicalBindName", ""),
            }
    reason = ("direct descriptor access has no shader binding"
              if no_binding else
              "shader reflection does not expose a fixed register")
    return {
        "available": False,
        "status": "unavailable",
        "register": None,
        "registerSpace": None,
        "name": "",
        "reason": reason,
    }


def _same_descriptor(left, right):
    return (
        resource_id(resource_ref(left)) == resource_id(resource_ref(right))
        and int(safe_get(left, "byteOffset", 0) or 0)
        == int(safe_get(right, "byteOffset", 0) or 0)
        and int(safe_get(left, "byteSize", 0) or 0)
        == int(safe_get(right, "byteSize", 0) or 0)
        and enum_name(safe_get(left, "type", ""))
        == enum_name(safe_get(right, "type", "")))


def _visibility_allows(visibility, stage):
    if visibility in (rd.ShaderStageMask.Unknown, rd.ShaderStageMask.All):
        return True
    try:
        return bool(visibility & rd.MaskForStage(stage))
    except Exception:
        return False


def _d3d12_root_binding(adapter, access, descriptor, reflected):
    if adapter.d3d12 is None:
        return None
    category = enum_name(rd.CategoryForDescriptorType(access.type))
    store_id = resource_id(access.descriptorStore)
    access_offset = int(access.byteOffset)
    signature = adapter.d3d12.rootSignature
    parameters = list(safe_get(signature, "parameters", []))

    store = adapter.descriptor_store(access.descriptorStore)
    stride = int(safe_get(store, "descriptorByteSize", 0) or 0)
    for parameter_index, parameter in enumerate(parameters):
        if not _visibility_allows(parameter.visibility, access.stage):
            continue
        if resource_id(safe_get(parameter, "heap")) != store_id or stride <= 0:
            continue
        table_start = int(safe_get(parameter, "heapByteOffset", 0) or 0)
        for range_index, table_range in enumerate(
                safe_get(parameter, "tableRanges", [])):
            if enum_name(safe_get(table_range, "category", "")) != category:
                continue
            range_start = table_start + int(
                safe_get(table_range, "tableByteOffset", 0) or 0)
            count = int(safe_get(table_range, "count", 0) or 0)
            delta = access_offset - range_start
            if delta < 0 or delta % stride != 0 or delta // stride >= count:
                continue
            descriptor_index = delta // stride
            register = int(table_range.baseRegister) + descriptor_index
            space = int(table_range.space)
            consistent = (
                not reflected["available"]
                or (reflected["register"] == register
                    and reflected["registerSpace"] == space))
            result = {
                "kind": "descriptor_table",
                "status": "resolved" if consistent else "register_mismatch",
                "rootSignatureId": resource_id(signature.resourceId),
                "parameterIndex": parameter_index,
                "visibility": enum_name(
                    safe_get(parameter, "visibility", "")),
                "heapId": store_id,
                "heapByteOffset": table_start,
                "rangeIndex": range_index,
                "rangeCategory": category,
                "rangeBaseRegister": int(table_range.baseRegister),
                "rangeRegisterSpace": space,
                "rangeCount": count,
                "rangeTableByteOffset": int(table_range.tableByteOffset),
                "descriptorIndex": descriptor_index,
                "register": register,
                "registerSpace": space,
                "reflectionConsistent": consistent,
            }
            if not consistent:
                result["reason"] = (
                    "root table resolves to register {}, space {} but shader "
                    "reflection resolves to register {}, space {}".format(
                        register, space, reflected["register"],
                        reflected["registerSpace"]))
            return result

    if reflected["available"]:
        register = reflected["register"]
        space = reflected["registerSpace"]
        for parameter_index, parameter in enumerate(parameters):
            if not _visibility_allows(parameter.visibility, access.stage):
                continue
            if int(safe_get(parameter, "reg", -1)) != register:
                continue
            if int(safe_get(parameter, "space", -1)) != space:
                continue
            constants = safe_get(parameter, "constants", b"")
            if category == "ConstantBlock" and constants:
                return {
                    "kind": "root_constants",
                    "status": "resolved",
                    "rootSignatureId": resource_id(signature.resourceId),
                    "parameterIndex": parameter_index,
                    "visibility": enum_name(
                        safe_get(parameter, "visibility", "")),
                    "register": register,
                    "registerSpace": space,
                    "inlineByteSize": len(constants),
                    "reflectionConsistent": True,
                }
            root_descriptor = safe_get(parameter, "descriptor")
            root_descriptor_type = safe_get(root_descriptor, "type")
            root_descriptor_category = (
                enum_name(rd.CategoryForDescriptorType(root_descriptor_type))
                if root_descriptor_type is not None else "Unknown")
            if (root_descriptor_category == category
                    and _same_descriptor(root_descriptor, descriptor)):
                return {
                    "kind": "root_descriptor",
                    "status": "resolved",
                    "rootSignatureId": resource_id(signature.resourceId),
                    "parameterIndex": parameter_index,
                    "visibility": enum_name(
                        safe_get(parameter, "visibility", "")),
                    "register": register,
                    "registerSpace": space,
                    "reflectionConsistent": True,
                }

        if category == "Sampler":
            for sampler_index, sampler in enumerate(
                    safe_get(signature, "staticSamplers", [])):
                if (_visibility_allows(sampler.visibility, access.stage)
                        and int(safe_get(sampler, "reg", -1)) == register
                        and int(safe_get(sampler, "space", -1)) == space):
                    return {
                        "kind": "static_sampler",
                        "status": "resolved",
                        "rootSignatureId": resource_id(signature.resourceId),
                        "staticSamplerIndex": sampler_index,
                        "visibility": enum_name(
                            safe_get(sampler, "visibility", "")),
                        "register": register,
                        "registerSpace": space,
                        "reflectionConsistent": True,
                    }

    return {
        "kind": ("direct_heap_access"
                 if int(access.index) == int(rd.DescriptorAccess.NoShaderBinding)
                 else "unavailable"),
        "status": "unavailable",
        "rootSignatureId": resource_id(signature.resourceId),
        "reason": (
            "descriptor access is not associated with a root-signature "
            "parameter or table range"),
        "reflectionConsistent": None,
    }


def _binding_details(adapter, access, descriptor):
    reflected = _reflected_binding(adapter, access)
    root = _d3d12_root_binding(adapter, access, descriptor, reflected)
    if root is not None and root.get("status") == "resolved":
        register = root.get("register", reflected["register"])
        space = root.get("registerSpace", reflected["registerSpace"])
        available = register is not None
        status = root["kind"]
    elif root is not None:
        register = reflected["register"]
        space = reflected["registerSpace"]
        available = False
        status = root["status"]
    else:
        register = reflected["register"]
        space = reflected["registerSpace"]
        available = reflected["available"]
        status = reflected["status"]
    result = {
        "slot": register,
        "register": register,
        "registerSpace": space,
        "bindingName": reflected["name"],
        "bindingAvailable": available,
        "bindingStatus": status,
        "rootBinding": root,
    }
    if not available:
        result["bindingError"] = (
            root.get("reason")
            if root is not None else reflected.get("reason"))
    return result


def _descriptor_record(session, adapter, used):
    access = used.access
    descriptor = used.descriptor
    rid = resource_id(resource_ref(descriptor))
    record = {
        "stage": _stage_name(access.stage),
        "type": enum_name(access.type),
        "category": _descriptor_category(access.type),
        "reflectionIndex": int(access.index),
        "arrayElement": int(access.arrayElement),
        "resourceId": rid,
        "resourceName": session.name(rid),
        "format": format_name(safe_get(descriptor, "format")),
        "byteOffset": int(safe_get(descriptor, "byteOffset", 0) or 0),
        "byteSize": int(safe_get(descriptor, "byteSize", 0) or 0),
        "firstMip": int(safe_get(descriptor, "firstMip", 0) or 0),
        "numMips": int(safe_get(descriptor, "numMips", 0) or 0),
        "firstSlice": int(safe_get(descriptor, "firstSlice", 0) or 0),
        "numSlices": int(safe_get(descriptor, "numSlices", 0) or 0),
        "descriptorStoreId": resource_id(access.descriptorStore),
        "descriptorByteOffset": int(access.byteOffset),
        "descriptorByteSize": int(access.byteSize),
        "staticallyUnused": bool(access.staticallyUnused),
    }
    record.update(_binding_details(adapter, access, descriptor))
    return record


def _sampler_record(adapter, used):
    access = used.access
    sampler = used.sampler
    record = {
        "stage": _stage_name(access.stage),
        "type": "Sampler",
        "category": "Sampler",
        "reflectionIndex": int(access.index),
        "arrayElement": int(access.arrayElement),
        "resourceId": 0,
        "resourceName": "Sampler",
        "addressU": enum_name(safe_get(sampler, "addressU", "")),
        "addressV": enum_name(safe_get(sampler, "addressV", "")),
        "addressW": enum_name(safe_get(sampler, "addressW", "")),
        "filter": enum_name(safe_get(sampler, "filter", "")),
        "descriptorStoreId": resource_id(access.descriptorStore),
        "descriptorByteOffset": int(access.byteOffset),
        "descriptorByteSize": int(access.byteSize),
        "staticallyUnused": bool(access.staticallyUnused),
    }
    record.update(_binding_details(adapter, access, used.descriptor))
    return record


def _descriptor_records(session, adapter):
    records = []
    for used in adapter.state.GetAllUsedDescriptors(False):
        if rd.IsSamplerDescriptor(used.access.type):
            record = _sampler_record(adapter, used)
        else:
            record = _descriptor_record(session, adapter, used)
        records.append((record, used))
    return records


def descriptor_bindings(session, state):
    return [record for record, _ in _descriptor_records(session, state)]


def stage_shader(state, name):
    return state.shader(name)


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


def decode_cbuffer(session, event_id, stage, slot, space=0):
    state = pipeline_state(session, event_id, False)
    stage_name, shader = stage_shader(state, stage)
    if resource_id(shader.resourceId) == 0 or shader.reflection is None:
        raise ValueError("No {} shader is bound at event {}".format(
            stage_name, event_id))
    records = _descriptor_records(session, state)
    matches = [
        (item, used) for item, used in records
        if item["category"] == "ConstantBuffer"
        and item["stage"] == stage_name
        and item["register"] == slot
        and item["registerSpace"] == space
    ]
    if not matches:
        unavailable = [
            item for item, _ in records
            if item["category"] == "ConstantBuffer"
            and item["stage"] == stage_name
            and not item["bindingAvailable"]
        ]
        detail = ("; unavailable bindings={}".format(unavailable)
                  if unavailable else "")
        raise ValueError(
            "No constant buffer is bound to {} register b{}, space {}{}".format(
                stage_name, slot, space, detail))
    binding, used = matches[0]
    if not binding["bindingAvailable"]:
        raise ValueError(
            "Constant buffer binding b{}, space {} is unavailable: {}".format(
                slot, space, binding.get("bindingError", "unknown reason")))
    blocks = list(shader.reflection.constantBlocks)
    block_index = binding["reflectionIndex"]
    if block_index < 0 or block_index >= len(blocks):
        raise ValueError(
            "Constant buffer b{}, space {} has no reflected block".format(
                slot, space))
    block = blocks[block_index]
    expected_register = int(safe_get(block, "fixedBindNumber", -1))
    expected_space = int(safe_get(block, "fixedBindSetOrSpace", 0) or 0)
    expected_register += binding["arrayElement"]
    if expected_register != slot or expected_space != space:
        raise ValueError(
            "Descriptor and reflection bindings disagree for b{}, space {}"
            .format(slot, space))

    descriptor = used.descriptor
    buffer_resource = resource_ref(descriptor)
    buffer_id = resource_id(buffer_resource)
    buffer_backed = bool(safe_get(block, "bufferBacked", True))
    inline_data = bool(safe_get(block, "inlineDataBytes", False))
    if buffer_backed and not inline_data and buffer_id == 0:
        raise ValueError(
            "Constant buffer b{}, space {} is null or unavailable".format(
                slot, space))
    if buffer_resource is None:
        buffer_resource = rd.ResourceId.Null()
    length = binding["byteSize"] or int(safe_get(block, "byteSize", 0) or 0)
    pipeline = state.pipeline_object(stage_name)
    variables = session.controller.GetCBufferVariableContents(
        pipeline, shader.resourceId, shader.stage, shader.entryPoint,
        block_index, buffer_resource, binding["byteOffset"], length)
    return {
        "eventId": event_id,
        "api": state.api,
        "pipelineKind": (
            "compute" if stage_name == "compute" else "graphics"),
        "pipelineObjectId": resource_id(pipeline),
        "pipelineObjectName": session.name(pipeline),
        "stage": stage_name,
        "slot": slot,
        "register": slot,
        "registerSpace": space,
        "bindingStatus": binding["bindingStatus"],
        "rootBinding": binding["rootBinding"],
        "reflectionIndex": block_index,
        "blockName": block.name,
        "shaderId": resource_id(shader.resourceId),
        "shaderName": session.name(shader.resourceId),
        "entryPoint": shader.entryPoint,
        "bufferId": buffer_id,
        "bufferName": session.name(buffer_id),
        "byteOffset": binding["byteOffset"],
        "byteSize": length,
        "bufferBacked": buffer_backed,
        "inlineDataBytes": inline_data,
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
    for slot, target in enumerate(state.state.GetOutputTargets()):
        rid = resource_id(resource_ref(target))
        if rid:
            records.append(_bound_view(session, target, "render_target", slot))
    depth = state.state.GetDepthTarget()
    rid = resource_id(resource_ref(depth))
    if rid:
        records.append(_bound_view(session, depth, "depth_target", 0))
    return records


def root_signature_record(session, state):
    if state.d3d12 is None:
        return None
    signature = state.d3d12.rootSignature
    parameters = []
    for parameter_index, parameter in enumerate(signature.parameters):
        table_ranges = []
        for range_index, table_range in enumerate(parameter.tableRanges):
            table_ranges.append({
                "rangeIndex": range_index,
                "category": enum_name(table_range.category),
                "baseRegister": int(table_range.baseRegister),
                "registerSpace": int(table_range.space),
                "count": int(table_range.count),
                "tableByteOffset": int(table_range.tableByteOffset),
                "appended": bool(table_range.appended),
            })
        descriptor = parameter.descriptor
        descriptor_type = enum_name(safe_get(descriptor, "type", ""))
        constants = parameter.constants
        if table_ranges:
            kind = "descriptor_table"
        elif constants:
            kind = "root_constants"
        elif descriptor_type != "Unknown":
            kind = "root_descriptor"
        else:
            kind = "unavailable"
        record = {
            "parameterIndex": parameter_index,
            "kind": kind,
            "visibility": enum_name(parameter.visibility),
        }
        if kind == "descriptor_table":
            record.update({
                "heapId": resource_id(parameter.heap),
                "heapByteOffset": int(parameter.heapByteOffset),
                "tableRanges": table_ranges,
            })
        elif kind == "root_constants":
            record.update({
                "register": int(parameter.reg),
                "registerSpace": int(parameter.space),
                "inlineByteSize": len(constants),
            })
        elif kind == "root_descriptor":
            rid = resource_id(resource_ref(descriptor))
            record.update({
                "register": int(parameter.reg),
                "registerSpace": int(parameter.space),
                "descriptor": {
                    "type": descriptor_type,
                    "resourceId": rid,
                    "resourceName": session.name(rid),
                    "format": format_name(safe_get(descriptor, "format")),
                    "byteOffset": int(
                        safe_get(descriptor, "byteOffset", 0) or 0),
                    "byteSize": int(
                        safe_get(descriptor, "byteSize", 0) or 0),
                },
            })
        elif kind == "unavailable":
            record["reason"] = (
                "Root parameter has no descriptor table, inline constants, "
                "or typed root descriptor")
        parameters.append(record)
    static_samplers = []
    for sampler_index, sampler in enumerate(signature.staticSamplers):
        static_samplers.append({
            "staticSamplerIndex": sampler_index,
            "register": int(sampler.reg),
            "registerSpace": int(sampler.space),
            "visibility": enum_name(sampler.visibility),
        })
    return {
        "resourceId": resource_id(signature.resourceId),
        "descriptorHeapIds": [
            resource_id(item) for item in state.d3d12.descriptorHeaps],
        "parameters": parameters,
        "staticSamplers": static_samplers,
    }


def shader_records(session, state):
    records = []
    for stage_name, _ in STAGES:
        _, shader = stage_shader(state, stage_name)
        rid = resource_id(shader.resourceId)
        if rid == 0:
            continue
        records.append({
            "stage": stage_name,
            "resourceId": rid,
            "resourceName": session.name(rid),
            "entryPoint": shader.entryPoint,
        })
    return records


def rasterizer_record(state):
    rasterizer = state.rasterizer()
    if rasterizer is None:
        return {
            "available": False,
            "reason": "Rasterizer state is unavailable for " + state.api,
            "viewports": [],
            "scissors": [],
        }
    viewports = []
    for item in rasterizer.viewports:
        viewports.append({
            "x": item.x,
            "y": item.y,
            "width": item.width,
            "height": item.height,
            "minDepth": item.minDepth,
            "maxDepth": item.maxDepth,
        })
    scissors = []
    for item in rasterizer.scissors:
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
    return {
        "available": True,
        "viewports": viewports,
        "scissors": scissors,
    }


def state_record(session, event_id):
    state = pipeline_state(session, event_id, True)
    bindings = descriptor_bindings(session, state)
    graphics_pipeline = resource_id(state.state.GetGraphicsPipelineObject())
    compute_pipeline = resource_id(state.state.GetComputePipelineObject())
    shaders = shader_records(session, state)
    has_compute = any(item["stage"] == "compute" for item in shaders)
    has_graphics = any(item["stage"] != "compute" for item in shaders)
    if has_compute and not has_graphics:
        pipeline_kind = "compute"
    elif has_graphics and not has_compute:
        pipeline_kind = "graphics"
    elif compute_pipeline and not graphics_pipeline:
        pipeline_kind = "compute"
    elif graphics_pipeline and not compute_pipeline:
        pipeline_kind = "graphics"
    else:
        pipeline_kind = "mixed_or_unavailable"
    return {
        "eventId": event_id,
        "api": state.api,
        "pipelineKind": pipeline_kind,
        "graphicsPipelineObjectId": graphics_pipeline,
        "graphicsPipelineObjectName": session.name(graphics_pipeline),
        "computePipelineObjectId": compute_pipeline,
        "computePipelineObjectName": session.name(compute_pipeline),
        "rootSignature": root_signature_record(session, state),
        "shaders": shaders,
        "bindings": bindings,
        "unavailableBindings": [
            item for item in bindings if not item["bindingAvailable"]],
        "constantBuffers": [item for item in bindings
                            if item["category"] == "ConstantBuffer"],
        "srvs": [item for item in bindings
                 if item["category"] == "Image"],
        "uavs": [item for item in bindings
                 if item["category"] == "ReadWriteImage"],
        "samplers": [item for item in bindings
                     if item["category"] == "Sampler"],
        "outputTargets": output_targets(session, state),
        "rasterizer": rasterizer_record(state),
    }
