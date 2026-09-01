import io
import json
import os
import sys
import traceback


def _write_result(path, result):
    with open(path, "w") as stream:
        json.dump(result, stream, indent=2, sort_keys=True)
        stream.flush()


def _inside(path, root):
    candidate = os.path.normcase(os.path.realpath(path))
    parent = os.path.normcase(os.path.realpath(root))
    return candidate == parent or candidate.startswith(parent + os.sep)


def main():
    job_path = os.environ.get("RDOC_JOB")
    if not job_path:
        os._exit(2)
    result_path = None
    session = None
    try:
        with io.open(job_path, "r", encoding="utf-8") as stream:
            job = json.load(stream)
        out_dir = os.path.abspath(job["outDir"])
        repo_root = os.path.abspath(job["repoRoot"])
        if _inside(out_dir, repo_root):
            raise ValueError("Artifact directory must be outside the repository")
        if not os.path.isdir(out_dir):
            os.makedirs(out_dir)
        result_path = os.path.join(out_dir, "result.json")
        script_dir = os.path.abspath(job["scriptDir"])
        if script_dir not in sys.path:
            sys.path.insert(0, script_dir)
        from actions import ActionIndex
        from capture import CaptureSession
        from commands import run

        command = str(job.get("command", "overview")).lower()
        args = [str(value) for value in job.get("args", [])]
        if command == "dump" and "--outdir" in args:
            index = args.index("--outdir")
            if index + 1 < len(args) and _inside(args[index + 1], repo_root):
                raise ValueError("Dump directory must be outside the repository")
        capture_path = os.path.abspath(job["capture"])
        if not os.path.isfile(capture_path):
            raise ValueError("Capture not found: " + capture_path)
        session = CaptureSession(capture_path)
        session.open()
        actions = ActionIndex(session.controller)
        payload = run(command, session, actions, args, out_dir)
        result = {
            "ok": True,
            "command": command,
            "artifactDirectory": out_dir,
            "result": payload,
        }
    except BaseException as error:
        result = {
            "ok": False,
            "command": str(locals().get("command", "")),
            "artifactDirectory": locals().get("out_dir"),
            "error": str(error),
            "errorType": error.__class__.__name__,
            "traceback": traceback.format_exc(),
        }
    finally:
        if session is not None:
            try:
                session.close()
            except BaseException as error:
                if locals().get("result", {}).get("ok"):
                    result = {
                        "ok": False,
                        "command": locals().get("command", ""),
                        "artifactDirectory": locals().get("out_dir"),
                        "error": "Replay shutdown failed: " + str(error),
                        "errorType": error.__class__.__name__,
                        "traceback": traceback.format_exc(),
                    }
        if result_path is not None:
            try:
                _write_result(result_path, result)
            except BaseException:
                os._exit(2)
        os._exit(0)


main()
