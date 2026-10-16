"""Drive Phoenix from your own Python.

    import phoenix

    doc = phoenix.open("walk.fla")
    for layer in doc.timelines[0].layers:
        print(layer.name, layer.frame_count)
    doc.get_timeline().insert_frames(5, all_layers=True)
    phoenix.save(doc, "walk_long.fla")

Phoenix runs with no window, in a process of its own, for as long as the
session lasts. The objects you get (documents, timelines, layers, frames,
elements, library items) are Phoenix's own, the ones its Python scripts
use, with the same names and the same snake_case ones beside them; each use
of one is a request to that process.

The functions here use one session, started when first needed. Make one of
your own with phoenix.start() (or phoenix.Session()) to choose the Phoenix,
or to run several at once.

Phoenix is found by, in order: the exe argument, the PHOENIX_EXE environment
variable, a Phoenix this package was installed with, the usual place
Phoenix installs to (%LOCALAPPDATA%\\Phoenix\\current\\Phoenix.exe on
Windows, /Applications/Phoenix.app on macOS), Phoenix on your PATH, and a
build beside this package's source folder.

Phoenix runs Python inside it to answer, the same Python as this one unless
PHOENIX_PYTHON says otherwise, so it needs a Python with its shared library
(the python.org installers, Homebrew, most Linux packages and virtual
environments made from them).
"""

import atexit
import builtins
import datetime
import glob
import json
import os
import shutil
import subprocess
import sys
import threading

__all__ = [
    "Error",
    "PhoenixNotFound",
    "Session",
    "Object",
    "Method",
    "start",
    "session",
    "find_phoenix",
    "open",
    "new",
    "save",
    "close",
    "documents",
    "python",
    "jsfl",
    "run_script",
    "call",
    "export_png",
    "export_movie",
    "shutdown",
]

_builtin_open = open
_here = os.path.dirname(os.path.abspath(__file__))


class Error(Exception):
    """An error from Phoenix. .remote_traceback has Phoenix's side of it."""

    remote_traceback = ""


class PhoenixNotFound(Error):
    """No Phoenix to start: set PHOENIX_EXE, or pass exe=."""


# Finding Phoenix.


def _candidates():
    exe = "Phoenix.exe" if os.name == "nt" else "Phoenix"
    # Installed with Phoenix: <Phoenix>/python/phoenix, or
    # Phoenix.app/Contents/Resources/python/phoenix.
    install = os.path.dirname(os.path.dirname(_here))
    yield os.path.join(install, exe)
    if sys.platform == "darwin":
        yield os.path.join(install, "..", "MacOS", "Phoenix")
        for applications in ("/Applications", os.path.expanduser("~/Applications")):
            yield os.path.join(applications, "Phoenix.app", "Contents", "MacOS", "Phoenix")
    if os.name == "nt":
        local = os.environ.get("LOCALAPPDATA")
        if local:
            yield os.path.join(local, "Phoenix", "current", "Phoenix.exe")
    for name in ("Phoenix", "phoenix"):
        found = shutil.which(name)
        if found:
            yield found
    # A build in the source tree this package came from, newest first.
    builds = []
    for pattern in ("build*/Release/" + exe, "build*/RelWithDebInfo/" + exe, "build*/" + exe,
                    "build*/Debug/" + exe, "build*/Phoenix.app/Contents/MacOS/Phoenix"):
        builds += glob.glob(os.path.join(install, pattern))
    builds.sort(key=os.path.getmtime, reverse=True)
    for build in builds:
        yield build


def find_phoenix(exe=None):
    """The Phoenix program to start: exe, PHOENIX_EXE, or where Phoenix installs."""
    if exe:
        if not os.path.isfile(exe):
            raise PhoenixNotFound("There is no Phoenix at %s." % exe)
        return os.path.abspath(exe)
    named = os.environ.get("PHOENIX_EXE")
    if named:
        if not os.path.isfile(named):
            raise PhoenixNotFound("PHOENIX_EXE names %s, which is not there." % named)
        return os.path.abspath(named)
    for candidate in _candidates():
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)
    raise PhoenixNotFound("Could not find Phoenix. Set PHOENIX_EXE to Phoenix's program, or pass exe=.")


# No console window for Phoenix on Windows.
_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def _offscreen(exe):
    """Whether Phoenix can draw with no screen at all (Qt's offscreen platform)."""
    if sys.platform.startswith("linux"):
        return not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY")
    folder = os.path.dirname(exe)
    plugin = "qoffscreen.dll" if os.name == "nt" else "libqoffscreen.dylib"
    for place in (os.path.join(folder, "platforms"), os.path.join(folder, "..", "PlugIns", "platforms")):
        if os.path.isfile(os.path.join(place, plugin)):
            return True
    return False


def _environment(exe, python=None, extra=None):
    """Phoenix's environment: the Python it runs inside, and no window."""
    environment = dict(os.environ)
    if extra:
        environment.update(extra)
    if python:
        environment["PHOENIX_PYTHON"] = python
    elif not environment.get("PHOENIX_PYTHON") and sys.executable:
        environment["PHOENIX_PYTHON"] = sys.executable
    if not environment.get("QT_QPA_PLATFORM") and _offscreen(exe):
        environment["QT_QPA_PLATFORM"] = "offscreen"
    return environment


# Phoenix's objects, held by number.


class Object:
    """One of Phoenix's objects: a document, timeline, layer, frame, element or item.

    Reading or setting an attribute, or calling a method, asks Phoenix. Lists
    and dicts come back as Python's own, holding objects like this one.
    """

    __slots__ = ("_session", "_number", "_class", "_repr", "__weakref__")

    def __init__(self, session, number, kind, text):
        object.__setattr__(self, "_session", session)
        object.__setattr__(self, "_number", number)
        object.__setattr__(self, "_class", kind)
        object.__setattr__(self, "_repr", text)

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        session = object.__getattribute__(self, "_session")
        kind = object.__getattribute__(self, "_class")
        if (kind, name) in session._methods:
            return _Call(self, name)
        value = session._request({"op": "get", "target": self._number, "name": name})
        if isinstance(value, Method):
            session._methods.add((kind, name))
        return value

    def __setattr__(self, name, value):
        if name in Object.__slots__:
            raise AttributeError("%s cannot be set" % name)
        self._session._request({"op": "set", "target": self._number, "name": name, "value": value})

    def __dir__(self):
        return self._session._request({"op": "dir", "target": self._number})

    def __repr__(self):
        return self._repr

    def __eq__(self, other):
        return isinstance(other, Object) and other._session is self._session and other._number == self._number

    def __ne__(self, other):
        return not self == other

    def __hash__(self):
        return hash((id(self._session), self._number))

    def __del__(self):
        try:
            self._session._forget(self._number)
        except Exception:
            pass


class Method(Object):
    """A method of one of Phoenix's objects, ready to call, with keywords where it takes them."""

    __slots__ = ()

    def __call__(self, *args, **kwargs):
        return self._session._request({"op": "call", "target": self._number, "args": list(args), "kwargs": kwargs})


class _Call:
    """obj.method, for a method already known: called without asking for it first."""

    __slots__ = ("_target", "_name")

    def __init__(self, target, name):
        self._target = target
        self._name = name

    def __call__(self, *args, **kwargs):
        target = self._target
        return target._session._request(
            {"op": "call", "target": target._number, "name": self._name, "args": list(args), "kwargs": kwargs})

    def __repr__(self):
        return "<phoenix method %s>" % self._name


class _Root(Object):
    """fl and FLfile, which are always there."""

    __slots__ = ()

    def __del__(self):
        pass


# Python's own errors from Phoenix's side are raised as themselves here
# (AttributeError, TypeError...), so hasattr and except work as usual.
_ERRORS = {name: kind for name, kind in vars(builtins).items()
           if isinstance(kind, type) and issubclass(kind, Exception) and not issubclass(kind, OSError)}


class Session:
    """A Phoenix with no window, started for this Python and ended by close().

        with phoenix.Session() as ph:
            doc = ph.open("walk.fla")
            print(doc.name)

    exe picks the Phoenix (see find_phoenix); python the Python it runs
    inside (PHOENIX_PYTHON, else this one); echo=False keeps what Phoenix
    prints in .output rather than printing it here.
    """

    def __init__(self, exe=None, python=None, echo=True, env=None):
        self.exe = find_phoenix(exe)
        self.echo = echo
        #: What Phoenix printed for the last request.
        self.output = ""
        self._lock = threading.RLock()
        self._methods = set()
        # How many objects here hold each of Phoenix's numbers, and the
        # numbers none hold any more, let go of with the next request.
        self._held = {}
        self._released = []
        self._next = 1
        self._errors = []
        self._closed = False

        server = os.path.join(_here, "_server.py")
        self._process = subprocess.Popen(
            [self.exe, "run-script", server, "--timeout", "0"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=_environment(self.exe, python, env), creationflags=_NO_WINDOW)
        self._stderr = threading.Thread(target=self._read_errors, daemon=True)
        self._stderr.start()
        # Phoenix may print before the server starts; its first answer
        # says it is ready.
        while True:
            line = self._process.stdout.readline()
            if not line:
                self._died("Phoenix stopped before it was ready")
            line = line.strip()
            if line.startswith(b"{"):
                try:
                    message = json.loads(line.decode("utf-8"))
                except ValueError:
                    continue
                if message.get("ready"):
                    break
        hello = self._request({"op": "hello"})
        #: Phoenix's version, and the Python running inside it.
        self.version = hello.get("version", "")
        self.python_version = hello.get("python", "")
        self.fl = _Root(self, "fl", "Flash", "<phoenix.Flash>")
        self.FLfile = _Root(self, "FLfile", "FLfile", "<phoenix.FLfile>")

    def _read_errors(self):
        for line in self._process.stderr:
            self._errors.append(line.decode("utf-8", "replace").rstrip())
            del self._errors[:-50]

    def _died(self, what):
        self._closed = True
        try:
            self._process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        self._stderr.join(timeout=1)
        detail = "\n".join(self._errors[-20:])
        raise Error("%s (exit code %s)%s" % (what, self._process.returncode, ":\n" + detail if detail else "."))

    # Values, both ways.

    def _encode(self, value):
        if isinstance(value, Object):
            if value._session is not self:
                raise Error("That object belongs to another Phoenix session.")
            return {"$method" if isinstance(value, Method) else "$object": value._number}
        if isinstance(value, _Call):
            return self._encode(getattr(value._target, value._name))
        if isinstance(value, (list, tuple)):
            return [self._encode(item) for item in value]
        if isinstance(value, dict):
            return {"$dict": [[str(key), self._encode(item)] for key, item in value.items()]}
        if isinstance(value, datetime.datetime):
            return {"$date": value.isoformat()}
        if isinstance(value, os.PathLike):
            return os.fspath(value)
        return value

    def _hold(self, number):
        """One more object here holds the number; Phoenix keeps the object while any does."""
        self._held[number] = self._held.get(number, 0) + 1
        if number in self._released:
            self._released.remove(number)
        return number

    def _forget(self, number):
        count = self._held.get(number, 0) - 1
        if count > 0:
            self._held[number] = count
            return
        self._held.pop(number, None)
        if not self._closed:
            self._released.append(number)

    def _decode(self, value):
        if isinstance(value, list):
            return [self._decode(item) for item in value]
        if isinstance(value, dict):
            if "$object" in value:
                return Object(self, self._hold(value["$object"]), value.get("class", "Object"),
                              value.get("repr", "<phoenix.Object>"))
            if "$method" in value:
                return Method(self, self._hold(value["$method"]), "Method", value.get("repr", "<phoenix method>"))
            if "$dict" in value:
                return {key: self._decode(item) for key, item in value["$dict"]}
            if "$date" in value:
                return datetime.datetime.fromisoformat(value["$date"])
        return value

    def _request(self, request):
        with self._lock:
            if self._closed:
                raise Error("This Phoenix session has ended.")
            request = {key: self._encode(value) if key in ("value", "args", "kwargs", "variables") else value
                       for key, value in request.items()}
            request["id"] = self._next
            self._next += 1
            if self._released:
                request["release"], self._released = self._released, []
            try:
                self._process.stdin.write(json.dumps(request).encode("utf-8") + b"\n")
                self._process.stdin.flush()
            except OSError:
                self._died("Phoenix stopped")
            while True:
                line = self._process.stdout.readline()
                if not line:
                    self._died("Phoenix stopped")
                try:
                    reply = json.loads(line.decode("utf-8"))
                except ValueError:
                    continue
                if reply.get("id") == request["id"]:
                    break
        self.output = reply.get("output", "")
        if self.output and self.echo:
            sys.stdout.write(self.output)
            sys.stdout.flush()
        if not reply.get("ok"):
            error = reply.get("error") or {}
            kind = _ERRORS.get(error.get("type"), Error)
            if not issubclass(kind, Exception) or kind is Exception:
                kind = Error
            raised = kind(error.get("message", "Phoenix could not do that."))
            raised.remote_traceback = error.get("traceback", "")
            raise raised
        return self._decode(reply.get("value"))

    # Documents.

    @property
    def documents(self):
        """The documents open in this Phoenix."""
        return self.fl.documents

    @property
    def document(self):
        """The document in front (fl.getDocumentDOM()), or None."""
        return self.fl.getDocumentDOM()

    def open(self, path):
        """Opens a document (.fla, .phx, an XFL folder's .xfl) and gives it."""
        return self._request({"op": "open", "path": os.path.abspath(os.fspath(path))})

    def new(self, width=None, height=None, frame_rate=None, background_color=None):
        """A new, empty document."""
        return self._request({"op": "new", "width": width, "height": height, "frameRate": frame_rate,
                              "backgroundColor": background_color})

    def save(self, document, path=None, copy=False):
        """Saves a document, to path when given (its format from the name); copy leaves the document's own file as it was."""
        return self._request({"op": "save", "target": document._number,
                              "path": os.path.abspath(os.fspath(path)) if path else None, "copy": copy})

    def close(self, document=None):
        """Closes a document without saving it; with none given, ends the session."""
        if document is not None:
            return self.fl.closeDocument(document, False)
        self.shutdown()

    def export_png(self, document, path, frame=None):
        """Draws a frame (from 0) of a document to a PNG; with no frame, every frame, numbered."""
        return self._request({"op": "export_png", "target": document._number,
                              "path": os.path.abspath(os.fspath(path)), "frame": frame})

    # Scripts and operations.

    def python(self, code, **variables):
        """Runs Python inside Phoenix (fl, FLfile and the variables given are there); the value of its last line."""
        return self._request({"op": "python", "code": code, "variables": variables})

    def jsfl(self, code):
        """Runs JSFL; the value of its last statement (plain values only)."""
        return self._request({"op": "jsfl", "code": code})

    def run_script(self, path, function=None, *args):
        """Runs a .jsfl or .py script, and calls function in it with args when given; what it gives back."""
        return self._request({"op": "jsfl", "path": os.path.abspath(os.fspath(path)), "function": function,
                              "args": list(args)})

    def call(self, operation, **arguments):
        """One of Phoenix's own operations (fl.phoenix.call), on the document in front."""
        return self._request({"op": "operation", "name": operation, "arguments": arguments})

    def operations(self):
        """Phoenix's own operations, each with what it does and the arguments it takes."""
        return self._request({"op": "operation", "name": ""})

    # The session itself.

    def shutdown(self):
        """Ends this Phoenix; documents not saved are lost."""
        with self._lock:
            if self._closed:
                return
            try:
                self._process.stdin.write(b'{"op": "quit", "id": 0}\n')
                self._process.stdin.flush()
                self._process.stdin.close()
                self._process.wait(timeout=30)
            except (OSError, subprocess.TimeoutExpired):
                self._process.kill()
                self._process.wait()
            self._closed = True
            self._stderr.join(timeout=5)
            for pipe in (self._process.stdin, self._process.stdout, self._process.stderr):
                try:
                    pipe.close()
                except OSError:
                    pass

    @property
    def running(self):
        return not self._closed and self._process.poll() is None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.shutdown()

    def __del__(self):
        try:
            self.shutdown()
        except Exception:
            pass

    def __repr__(self):
        return "<phoenix.Session %s%s>" % (self.exe, "" if self.running else " (ended)")


# One session for the functions below, started when first needed.

_default = None
_default_lock = threading.Lock()


def start(exe=None, **options):
    """A new Phoenix session of your own (see Session)."""
    return Session(exe, **options)


def session():
    """The session the module's functions use, started the first time."""
    global _default
    with _default_lock:
        if _default is None or not _default.running:
            _default = Session()
        return _default


def shutdown():
    """Ends the module's session, if one was started."""
    global _default
    with _default_lock:
        if _default is not None:
            _default.shutdown()
            _default = None


atexit.register(shutdown)


def open(path):
    """Opens a document; see Session.open."""
    return session().open(path)


def new(**options):
    """A new document; see Session.new."""
    return session().new(**options)


def save(document, path=None, copy=False):
    """Saves a document; see Session.save."""
    return document._session.save(document, path, copy)


def close(document):
    """Closes a document without saving it."""
    return document._session.close(document)


def documents():
    """The open documents."""
    return session().documents


def python(code, **variables):
    """Runs Python inside Phoenix; see Session.python."""
    return session().python(code, **variables)


def jsfl(code):
    """Runs JSFL; see Session.jsfl."""
    return session().jsfl(code)


def run_script(path, function=None, *args):
    """Runs a .jsfl or .py script; see Session.run_script."""
    return session().run_script(path, function, *args)


def call(operation, **arguments):
    """One of Phoenix's own operations; see Session.call."""
    return session().call(operation, **arguments)


def export_png(document, path, frame=None):
    """Draws a frame to a PNG; see Session.export_png."""
    return document._session.export_png(document, path, frame)


def export_movie(document_path, out_path, exe=None):
    """Renders a document's movie (.mp4, .mov, .gif, an image sequence...) with a Phoenix of its own."""
    program = find_phoenix(exe)
    result = subprocess.run([program, "--export-movie", os.path.abspath(os.fspath(document_path)),
                             os.path.abspath(os.fspath(out_path))],
                            capture_output=True, env=_environment(program), creationflags=_NO_WINDOW)
    if result.returncode != 0:
        raise Error(result.stderr.decode("utf-8", "replace").strip() or "The movie could not be exported.")
    return os.path.abspath(os.fspath(out_path))


def __getattr__(name):
    # phoenix.fl and phoenix.FLfile: the module's session's.
    if name in ("fl", "FLfile"):
        return getattr(session(), name)
    raise AttributeError("module 'phoenix' has no attribute %r" % name)
