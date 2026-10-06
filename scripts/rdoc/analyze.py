import io
import json
import os
import signal
import sys
import time
import traceback


def _write_result(path, result):
    # Rename so the wrapper never reads a half-written result.
    temporary = path + ".tmp"
    with open(temporary, "w") as stream:
        json.dump(result, stream, indent=2, sort_keys=True)
    os.replace(temporary, path)


def _progress(out_dir, message):
    if out_dir:
        with open(os.path.join(out_dir, "progress.log"), "a") as stream:
            stream.write(time.strftime("%H:%M:%S ") + message + "\n")


def _terminate():
    # os._exit runs driver DLL teardown, which can block forever after a GPU replay;
    # on Windows os.kill is TerminateProcess, and qrenderdoc's Python lacks ctypes.
    os.kill(os.getpid(), signal.SIGTERM)


def _inside(path, root):
    from common import extended_path, plain_path

    def resolve(value):
        return os.path.normcase(plain_path(os.path.realpath(extended_path(value))))

    candidate = resolve(path)
    parent = resolve(root)
    return candidate == parent or candidate.startswith(parent + os.sep)


def main():
    job_path = os.environ.get("RDOC_JOB")
    script_dir = os.environ.get("RDOC_SCRIPT_DIR")
    if not job_path or not script_dir:
        _terminate()
    out_dir = None
    result_path = None
    session = None
    command = ""
    try:
        script_dir = os.path.abspath(script_dir)
        if script_dir not in sys.path:
            sys.path.insert(0, script_dir)
        from actions import ActionIndex
        from capture import CaptureSession
        from commands import run
        from common import extended_path, plain_path

        with io.open(extended_path(job_path), "r", encoding="utf-8") as stream:
            job = json.load(stream)
        out_dir = extended_path(job["outDir"])
        repo_root = os.path.abspath(job["repoRoot"])
        if _inside(out_dir, repo_root):
            raise ValueError("Artifact directory must be outside the repository")
        if not os.path.isdir(out_dir):
            os.makedirs(out_dir)
        result_path = os.path.join(out_dir, "result.json")

        command = str(job.get("command", "overview")).lower()
        args = [str(value) for value in job.get("args", [])]
        if command == "dump" and "--outdir" in args:
            index = args.index("--outdir")
            if index + 1 < len(args) and _inside(args[index + 1], repo_root):
                raise ValueError("Dump directory must be outside the repository")
        capture_path = extended_path(job["capture"])
        if not os.path.isfile(capture_path):
            raise ValueError("Capture not found: " + plain_path(capture_path))
        _progress(out_dir, "opening " + plain_path(capture_path))
        session = CaptureSession(capture_path)
        session.open()
        _progress(out_dir, "indexing actions")
        actions = ActionIndex(session.controller)
        _progress(out_dir, "running " + command)
        payload = run(command, session, actions, args, out_dir)
        result = {
            "ok": True,
            "command": command,
            "artifactDirectory": plain_path(out_dir),
            "result": payload,
        }
    except BaseException as error:
        result = {
            "ok": False,
            "command": command,
            "artifactDirectory": out_dir and plain_path(out_dir),
            "error": str(error),
            "errorType": error.__class__.__name__,
            "traceback": traceback.format_exc(),
        }
    if result_path is None:
        _terminate()
    try:
        _write_result(result_path, result)
        _progress(out_dir, "result written")
    except BaseException:
        _terminate()
    if session is not None:
        try:
            session.close()
            _progress(out_dir, "replay shut down")
        except BaseException as error:
            _progress(out_dir, "replay shutdown failed: " + str(error))
    _terminate()


main()
