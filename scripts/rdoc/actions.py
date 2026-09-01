import renderdoc as rd

from common import enum_name, has_flag


def action_kind(action):
    checks = (
        ("dispatch", rd.ActionFlags.Dispatch),
        ("drawcall", rd.ActionFlags.Drawcall),
        ("clear", rd.ActionFlags.Clear),
        ("copy", rd.ActionFlags.Copy),
        ("present", rd.ActionFlags.Present),
        ("push_marker", rd.ActionFlags.PushMarker),
        ("pop_marker", rd.ActionFlags.PopMarker),
        ("set_marker", rd.ActionFlags.SetMarker),
    )
    for name, flag in checks:
        if has_flag(action.flags, flag):
            return name
    return "action"


class ActionIndex(object):
    def __init__(self, controller):
        self.actions = []
        self.by_event = {}
        self.regions = []
        structured = controller.GetStructuredFile()
        self._walk(list(controller.GetRootActions()), None, [], structured)
        self.regions.sort(key=lambda item: item["startEventId"])

    def _walk(self, actions, parent_region, region_path, structured):
        for action in actions:
            name = action.GetName(structured)
            record = {
                "eventId": action.eventId,
                "name": name,
                "kind": action_kind(action),
                "flags": enum_name(action.flags),
                "markerPath": list(region_path),
            }
            self.actions.append(record)
            self.by_event[action.eventId] = record
            next_parent = parent_region
            next_path = region_path
            if has_flag(action.flags, rd.ActionFlags.PushMarker):
                region = {
                    "name": name,
                    "startEventId": action.eventId,
                    "endEventId": self._max_event(action),
                    "path": list(region_path) + [name],
                    "parent": parent_region,
                    "children": [],
                }
                if parent_region is not None:
                    parent_region["children"].append(region)
                self.regions.append(region)
                next_parent = region
                next_path = region["path"]
            self._walk(list(action.children), next_parent, next_path, structured)

    def _max_event(self, action):
        maximum = action.eventId
        for child in action.children:
            maximum = max(maximum, self._max_event(child))
        return maximum

    def marker_for_event(self, event_id):
        matches = [region for region in self.regions
                   if region["startEventId"] <= event_id <= region["endEventId"]]
        if not matches:
            return None
        return max(matches, key=lambda item: len(item["path"]))

    def marker_name_for_event(self, event_id):
        region = self.marker_for_event(event_id)
        return region["name"] if region is not None else None

    def resolve_event(self, value):
        try:
            event_id = int(str(value), 0)
            if event_id not in self.by_event:
                raise ValueError("Event id is not present: {}".format(event_id))
            return event_id
        except ValueError as error:
            if str(value).strip().isdigit():
                raise error
        lowered = str(value).lower()
        exact = [item for item in self.regions if item["name"].lower() == lowered]
        matches = exact or [item for item in self.regions
                            if lowered in item["name"].lower()]
        if len(matches) != 1:
            if not matches:
                raise ValueError("Marker not found: " + str(value))
            raise ValueError("Marker name is ambiguous: " +
                             ", ".join(item["name"] for item in matches[:12]))
        region = matches[0]
        candidates = [item for item in self.actions
                      if region["startEventId"] <= item["eventId"] <= region["endEventId"]
                      and item["kind"] in ("dispatch", "drawcall")]
        return candidates[0]["eventId"] if candidates else region["endEventId"]

    def action_record(self, record):
        return {
            "eventId": record["eventId"],
            "name": record["name"],
            "kind": record["kind"],
            "marker": self.marker_name_for_event(record["eventId"]),
        }

    def region_record(self, region, nested=True):
        record = {
            "name": region["name"],
            "path": "/".join(region["path"]),
            "startEventId": region["startEventId"],
            "endEventId": region["endEventId"],
        }
        if nested:
            record["children"] = [self.region_record(child, True)
                                  for child in region["children"]]
        return record

    def root_region_records(self):
        return [self.region_record(region, True) for region in self.regions
                if region["parent"] is None]
