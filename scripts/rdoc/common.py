import math
import os
import re


def enum_name(value):
    return str(value).rsplit(".", 1)[-1]


def resource_id(value):
    try:
        return int(value)
    except Exception:
        match = re.search(r"(\d+)", str(value))
        return int(match.group(1)) if match else 0


def resource_ref(value):
    for name in ("resource", "resourceId"):
        if hasattr(value, name):
            return getattr(value, name)
    return None


def result_ok(result):
    return result is not None and result.OK()


def result_message(result):
    if result is None:
        return "No result returned"
    for name in ("message", "Message"):
        value = getattr(result, name, None)
        if value:
            return str(value)
    return str(result)


def safe_get(value, name, default=None):
    try:
        return getattr(value, name)
    except Exception:
        return default


def vector(value, count=None):
    if value is None:
        return []
    try:
        values = list(value)
    except Exception:
        values = []
        if count is not None:
            for index in range(count):
                try:
                    values.append(value[index])
                except Exception:
                    break
    if count is not None:
        values = values[:count]
    return [json_scalar(item) for item in values]


def json_scalar(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    try:
        return int(value)
    except Exception:
        return str(value)


def finite(values):
    return [value for value in values
            if isinstance(value, (int, float)) and math.isfinite(value)]


def sanitize_filename(value):
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._")
    return cleaned[:120] or "resource"


def has_flag(flags, flag):
    try:
        return bool(flags & flag)
    except Exception:
        return False


def format_name(fmt):
    if fmt is None:
        return ""
    name = safe_get(fmt, "Name")
    if callable(name):
        try:
            return str(name())
        except Exception:
            pass
    return str(fmt)
