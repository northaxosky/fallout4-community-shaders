import json
import os

from bindings import decode_cbuffer, stage_shader, state_record
from common import enum_name, resource_id, safe_get, sanitize_filename
from resources import dump_bound_textures, is_write_usage, texture_stats, usage_name, usage_records
from triage import run_triage


def _option(args, name, default=None, cast=str):
    flag = "--" + name
    for index, value in enumerate(args):
        if value == flag:
            if index + 1 >= len(args):
                raise ValueError(flag + " requires a value")
            return cast(args[index + 1])
    return default


def _positionals(args):
    values = []
    skip = False
    options = ("--eid", "--mip", "--slice", "--slot", "--outdir")
    for value in args:
        if skip:
            skip = False
            continue
        if value in options:
            skip = True
            continue
        if value.startswith("--"):
            raise ValueError("Unknown option: " + value)
        values.append(value)
    return values


def _require(args, count, usage):
    values = _positionals(args)
    if len(values) < count:
        raise ValueError("Usage: " + usage)
    return values


def _debug_message(item):
    return {
        "eventId": safe_get(item, "eventId", 0),
        "category": enum_name(safe_get(item, "category", "")),
        "severity": enum_name(safe_get(item, "severity", "")),
        "source": enum_name(safe_get(item, "source", "")),
        "messageId": safe_get(item, "messageID", 0),
        "description": safe_get(item, "description", ""),
    }


def overview(session, actions, args, out_dir):
    counts = {}
    for item in actions.actions:
        counts[item["kind"]] = counts.get(item["kind"], 0) + 1
    grouped = {}
    for item in session.app_named_resources():
        prefix = item.name.split("/", 1)[0]
        grouped.setdefault(prefix, []).append(session.resource_record(item))
    for values in grouped.values():
        values.sort(key=lambda item: item["name"])
    frame = session.controller.GetFrameInfo()
    api = session.controller.GetAPIProperties()
    return {
        "capture": session.path,
        "driver": session.capture.DriverName(),
        "api": enum_name(safe_get(api, "pipelineType", "")),
        "frame": {
            "frameNumber": safe_get(frame, "frameNumber", 0),
            "firstEvent": safe_get(frame, "firstEvent", 0),
            "fileOffset": safe_get(frame, "fileOffset", 0),
            "uncompressedFileSize": safe_get(frame, "uncompressedFileSize", 0),
            "compressedFileSize": safe_get(frame, "compressedFileSize", 0),
        },
        "counts": {
            "actions": len(actions.actions),
            "drawcalls": counts.get("drawcall", 0),
            "dispatches": counts.get("dispatch", 0),
            "markerRegions": len(actions.regions),
        },
        "markerRegions": actions.root_region_records(),
        "appNamedResourceCount": len(session.app_named_resources()),
        "appNamedResourcesByFeature": grouped,
        "debugMessages": [_debug_message(item)
                          for item in session.controller.GetDebugMessages()],
    }


def passes(session, actions, args, out_dir):
    writes_by_region = dict((id(region), []) for region in actions.regions)
    for resource in session.resources:
        rid = resource_id(resource.resourceId)
        for usage in session.usage(rid):
            name = usage_name(usage.usage)
            if not is_write_usage(name):
                continue
            containing = [region for region in actions.regions
                          if region["startEventId"] <= usage.eventId
                          <= region["endEventId"]]
            for region in containing:
                writes_by_region[id(region)].append({
                    "eventId": usage.eventId,
                    "usage": name,
                    "resourceId": rid,
                    "resourceName": session.name(rid),
                })
    records = []
    for region in actions.regions:
        contained = [actions.action_record(item) for item in actions.actions
                     if region["startEventId"] <= item["eventId"] <= region["endEventId"]
                     and item["kind"] not in ("push_marker", "pop_marker")]
        records.append({
            "name": region["name"],
            "path": "/".join(region["path"]),
            "startEventId": region["startEventId"],
            "endEventId": region["endEventId"],
            "actions": contained,
            "writes": writes_by_region[id(region)],
        })
    return {"passes": records}


def state(session, actions, args, out_dir):
    values = _require(args, 1, "state <eid>")
    event_id = int(values[0], 0)
    result = state_record(session, event_id)
    result["action"] = actions.action_record(actions.by_event[event_id])
    return result


def resource(session, actions, args, out_dir):
    values = _require(args, 1, "resource <name-or-id>")
    rid = session.resolve_resource(values[0])
    record = session.resource_record(session.resource_by_id[rid])
    record["usage"] = usage_records(session, actions, rid)
    return record


def stats(session, actions, args, out_dir):
    values = _require(args, 1, "stats <name-or-id> [--eid N] [--mip N] [--slice N]")
    rid = session.resolve_resource(values[0])
    return texture_stats(
        session, actions, rid,
        event_id=_option(args, "eid", None, int),
        mip=_option(args, "mip", 0, int),
        slice_index=_option(args, "slice", 0, int))


def dump(session, actions, args, out_dir):
    values = _require(args, 1, "dump <eid-or-marker-name> [--outdir PATH]")
    event_id = actions.resolve_event(values[0])
    artifact_dir = os.path.abspath(_option(args, "outdir", out_dir, str))
    if not os.path.isdir(artifact_dir):
        os.makedirs(artifact_dir)
    manifest = dump_bound_textures(session, event_id, artifact_dir)
    manifest_path = os.path.join(artifact_dir, "manifest.json")
    with open(manifest_path, "w") as stream:
        json.dump({"eventId": event_id, "textures": manifest},
                  stream, indent=2, sort_keys=True)
    return {
        "eventId": event_id,
        "artifactDirectory": artifact_dir,
        "manifest": manifest_path,
        "textureCount": len(manifest),
        "textures": manifest,
    }


def cbuffer(session, actions, args, out_dir):
    values = _require(args, 2, "cbuffer <eid> <stage> [--slot N]")
    return decode_cbuffer(
        session, int(values[0], 0), values[1],
        _option(args, "slot", 0, int))


def disasm(session, actions, args, out_dir):
    values = _require(args, 2, "disasm <eid> <stage>")
    event_id = int(values[0], 0)
    session.controller.SetFrameEvent(event_id, True)
    state = session.controller.GetD3D11PipelineState()
    stage_name, shader = stage_shader(state, values[1])
    if resource_id(shader.resourceId) == 0 or shader.reflection is None:
        raise ValueError("No {} shader is bound at event {}".format(stage_name, event_id))
    pipeline = session.controller.GetPipelineState().GetGraphicsPipelineObject()
    targets = list(session.controller.GetDisassemblyTargets(True))
    target = targets[0] if targets else ""
    text = session.controller.DisassembleShader(pipeline, shader.reflection, target)
    filename = "{}-{}-{}.txt".format(
        event_id, stage_name, sanitize_filename(session.name(shader.resourceId)))
    path = os.path.join(out_dir, filename)
    with open(path, "w") as stream:
        stream.write(text)
    return {
        "eventId": event_id,
        "stage": stage_name,
        "shaderId": resource_id(shader.resourceId),
        "shaderName": session.name(shader.resourceId),
        "target": target,
        "artifact": path,
        "characters": len(text),
    }


def triage(session, actions, args, out_dir):
    return run_triage(session, actions)


COMMANDS = {
    "overview": overview,
    "passes": passes,
    "state": state,
    "resource": resource,
    "stats": stats,
    "dump": dump,
    "cbuffer": cbuffer,
    "disasm": disasm,
    "triage": triage,
}


def run(command, session, actions, args, out_dir):
    handler = COMMANDS.get(command)
    if handler is None:
        raise ValueError("Unknown command '{}'. Available commands: {}".format(
            command, ", ".join(sorted(COMMANDS))))
    return handler(session, actions, args, out_dir)
