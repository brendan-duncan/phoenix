"""Phoenix's side of the phoenix module: run inside Phoenix with

    Phoenix run-script _server.py --timeout 0

it answers requests from the module, one JSON object a line on standard
input, with one JSON object a line on standard output. Everything runs in
this one script run, so the objects it hands out stay good from one request
to the next; the module holds them by number.

What scripts print, and what Phoenix traces, goes into a file rather than to
standard output, and comes back with each answer.
"""

import ast
import datetime
import json
import os
import sys
import tempfile
import traceback

import phoenix

PROTOCOL = 1


class Server:
    def __init__(self):
        # Answers go out on what was standard output; standard output
        # itself (Phoenix's trace, print) goes into a file from here on.
        sys.stdout.flush()
        self._answers = os.fdopen(os.dup(1), "wb", buffering=0)
        self._output = tempfile.TemporaryFile()
        os.dup2(self._output.fileno(), 1)
        stdin = sys.stdin.buffer if sys.stdin is not None else os.fdopen(0, "rb")
        self._requests = stdin
        self._objects = {}
        self._numbers = {}
        self._next = 1
        self._scripts = tempfile.mkdtemp(prefix="phoenix_jsfl_")

    # Values, both ways.

    def _number(self, value):
        """The number the module holds the value by, given out once."""
        try:
            number = self._numbers.get(value)
        except TypeError:
            number = None
        if number is not None and self._objects.get(number) is not None:
            return number
        number = self._next
        self._next += 1
        self._objects[number] = value
        try:
            self._numbers[value] = number
        except TypeError:
            pass
        return number

    def encode(self, value, depth=0):
        if value is None or isinstance(value, (bool, int, float, str)):
            return value
        if depth > 32:
            return repr(value)
        if isinstance(value, (list, tuple)):
            return [self.encode(item, depth + 1) for item in value]
        if isinstance(value, dict):
            return {"$dict": [[str(key), self.encode(item, depth + 1)] for key, item in value.items()]}
        if isinstance(value, datetime.datetime):
            return {"$date": value.isoformat()}
        if isinstance(value, bytes):
            return value.decode("utf-8", "replace")
        kind = type(value).__name__
        if callable(value) and not isinstance(value, phoenix.Object):
            return {"$method": self._number(value), "repr": repr(value)}
        return {"$object": self._number(value), "class": kind, "repr": repr(value)}

    def decode(self, value):
        if isinstance(value, list):
            return [self.decode(item) for item in value]
        if isinstance(value, dict):
            if "$object" in value or "$method" in value:
                return self._get(value.get("$object", value.get("$method")))
            if "$dict" in value:
                return {key: self.decode(item) for key, item in value["$dict"]}
            if "$date" in value:
                return datetime.datetime.fromisoformat(value["$date"])
            return {key: self.decode(item) for key, item in value.items()}
        return value

    def _get(self, number):
        if number == "fl":
            return phoenix.fl
        if number == "FLfile":
            return phoenix.FLfile
        try:
            return self._objects[number]
        except KeyError:
            raise phoenix.Error("Phoenix no longer has that object.")

    def _release(self, numbers):
        for number in numbers:
            value = self._objects.pop(number, None)
            if value is None:
                continue
            try:
                if self._numbers.get(value) == number:
                    del self._numbers[value]
            except (TypeError, KeyError):
                pass

    # What the module asks for.

    def op_hello(self, request):
        return {"protocol": PROTOCOL, "version": phoenix.fl.version, "python": sys.version.split()[0]}

    def op_get(self, request):
        return getattr(self._get(request["target"]), request["name"])

    def op_set(self, request):
        setattr(self._get(request["target"]), request["name"], self.decode(request.get("value")))

    def op_call(self, request):
        target = self._get(request["target"])
        if request.get("name"):
            target = getattr(target, request["name"])
        args = self.decode(request.get("args") or [])
        kwargs = self.decode(request.get("kwargs") or {})
        return target(*args, **kwargs)

    def op_dir(self, request):
        return sorted(name for name in dir(self._get(request["target"])) if not name.startswith("_"))

    def op_repr(self, request):
        return repr(self._get(request["target"]))

    def op_python(self, request):
        """Runs Python code; the value of its last line when that is an expression."""
        scope = {
            "__name__": "__phoenix__",
            "fl": phoenix.fl,
            "FLfile": phoenix.FLfile,
            "phoenix": phoenix,
            "alert": phoenix.alert,
            "confirm": phoenix.confirm,
            "prompt": phoenix.prompt,
        }
        scope.update(self.decode(request.get("variables") or {}))
        name = request.get("name") or "<phoenix>"
        tree = ast.parse(request["code"], name)
        last = None
        if tree.body and isinstance(tree.body[-1], ast.Expr):
            last = ast.Expression(tree.body.pop().value)
        exec(compile(tree, name, "exec"), scope)
        if last is not None:
            return eval(compile(last, name, "eval"), scope)
        return None

    def op_jsfl(self, request):
        """Runs JSFL code, or a function in a JSFL file; what it comes to."""
        path = request.get("path")
        if not path:
            path = os.path.join(self._scripts, "snippet%d.jsfl" % self._next)
            self._next += 1
            with open(path, "w", encoding="utf-8") as script:
                script.write(request["code"])
        try:
            uri = phoenix.FLfile.platformPathToURI(os.path.abspath(path))
            function = request.get("function")
            args = self.decode(request.get("args") or [])
            if function:
                return phoenix.fl.runScript(uri, function, *args)
            return phoenix.fl.runScript(uri)
        finally:
            if not request.get("path"):
                os.remove(path)

    def op_open(self, request):
        return phoenix.fl.openDocument(phoenix.FLfile.platformPathToURI(os.path.abspath(request["path"])))

    def op_new(self, request):
        document = phoenix.fl.createDocument()
        for name in ("width", "height", "frameRate", "backgroundColor"):
            if request.get(name) is not None:
                setattr(document, name, request[name])
        return document

    def op_save(self, request):
        document = self._get(request["target"])
        path = request.get("path")
        uri = phoenix.FLfile.platformPathToURI(os.path.abspath(path)) if path else None
        if request.get("copy"):
            return document.saveAsCopy(uri)
        return phoenix.fl.saveDocument(document, uri)

    def op_export_png(self, request):
        """Writes one frame of a document (frame given, from 0), or every frame."""
        document = self._get(request["target"])
        frame = request.get("frame")
        if frame is not None:
            document.getTimeline().currentFrame = frame
        uri = phoenix.FLfile.platformPathToURI(os.path.abspath(request["path"]))
        return document.exportPNG(uri, True, frame is not None)

    def op_operation(self, request):
        """One of Phoenix's own operations, as fl.phoenix.call gives it."""
        answer = phoenix.fl.phoenixCall(request.get("name", ""), json.dumps(request.get("arguments") or {}))
        return json.loads(answer)

    # The loop.

    def answer(self, message):
        self._answers.write(json.dumps(message, default=repr).encode("utf-8") + b"\n")

    def _take_output(self):
        sys.stdout.flush()
        self._output.seek(0)
        text = self._output.read().decode("utf-8", "replace").replace("\r\n", "\n")
        self._output.seek(0)
        self._output.truncate()
        return text

    def handle(self, request):
        self._release(request.get("release") or [])
        reply = {"id": request.get("id")}
        try:
            handler = getattr(self, "op_" + str(request.get("op")), None)
            if handler is None:
                raise ValueError("Phoenix does not know the request %r." % request.get("op"))
            reply["value"] = self.encode(handler(request))
            reply["ok"] = True
        except BaseException as error:
            # Also sys.exit() in the code sent: the server carries on.
            reply["ok"] = False
            reply["error"] = {
                "type": type(error).__name__,
                "message": str(error),
                "traceback": "".join(traceback.format_exception(type(error), error, error.__traceback__)),
            }
        reply["output"] = self._take_output()
        return reply

    def serve(self):
        self.answer({"ready": True, "protocol": PROTOCOL})
        for line in self._requests:
            line = line.strip()
            if not line:
                continue
            try:
                request = json.loads(line.decode("utf-8"))
            except ValueError as error:
                self.answer({"ok": False, "error": {"type": "ValueError", "message": str(error)}})
                continue
            if request.get("op") == "quit":
                self.answer({"id": request.get("id"), "ok": True, "output": self._take_output()})
                break
            self.answer(self.handle(request))
        try:
            os.rmdir(self._scripts)
        except OSError:
            pass


if __name__ == "__main__":
    Server().serve()
