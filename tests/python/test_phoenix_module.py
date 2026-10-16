"""Tests for the phoenix Python module (python/phoenix), driving a built Phoenix.

    python tests/python/test_phoenix_module.py [--exe path/to/Phoenix.exe]

Phoenix is found as the module finds it: --exe, PHOENIX_EXE, or a build in
this source tree (build*/Release/Phoenix.exe...). Copy Qt's offscreen
platform plugin (platforms/qoffscreen.dll) beside it so no window shows.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "python"))

import phoenix  # noqa: E402

EXE = None


class SessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ph = phoenix.Session(EXE, echo=False)
        cls.folder = tempfile.mkdtemp(prefix="phoenix_module_test_")

    @classmethod
    def tearDownClass(cls):
        cls.ph.shutdown()
        shutil.rmtree(cls.folder, ignore_errors=True)

    def tearDown(self):
        self.ph.fl.closeAll(False)

    def path(self, name):
        return os.path.join(self.folder, name)

    def test_starts_and_says_what_it_is(self):
        self.assertIn("Phoenix", self.ph.version)
        self.assertTrue(self.ph.running)
        self.assertEqual(self.ph.python_version.split(".")[:2], [str(v) for v in sys.version_info[:2]])

    def test_documents_read_and_change(self):
        doc = self.ph.new(width=320, height=240, frame_rate=12)
        self.assertEqual((doc.width, doc.height, doc.frame_rate), (320, 240, 12))
        doc.width = 400
        self.assertEqual(doc.width, 400)
        self.assertEqual(self.ph.documents, [doc])
        self.assertEqual(self.ph.document, doc)

        timeline = doc.get_timeline()
        self.assertEqual(timeline, doc.timelines[0])
        self.assertEqual(len({timeline, doc.timelines[0]}), 1)
        timeline.add_new_layer(name="Ink", add_above=True)
        timeline.insert_frames(9, all_layers=True)
        timeline.insertFrames(numFrames=2, allLayers=False)
        self.assertEqual([layer.name for layer in timeline.layers], ["Ink", "Layer_1"])
        self.assertEqual([layer.frame_count for layer in timeline.layers], [12, 10])
        layer = timeline.layers[0]
        layer.name = "Top"
        self.assertEqual(repr(timeline.layers[0]), "<phoenix.Layer 'Top'>")
        self.assertIn("frame_count", dir(layer))
        self.assertIn("frameCount", dir(layer))
        self.assertEqual(layer.frames[0].start_frame, 0)

    def test_errors_come_back_as_python_errors(self):
        doc = self.ph.new()
        self.assertFalse(hasattr(doc, "widht"))
        with self.assertRaises(AttributeError):
            doc.widht = 3
        with self.assertRaises(phoenix.Error) as caught:
            doc.get_timeline().delete_layer(7)
        self.assertIn("no layer 7", str(caught.exception))
        self.assertIn("Traceback", caught.exception.remote_traceback)
        with self.assertRaises(TypeError):
            doc.get_timeline().insert_frames(5, every_layer=True)
        with self.assertRaises(ZeroDivisionError):
            self.ph.python("1 / 0")
        # The session goes on after them.
        self.assertEqual(doc.get_timeline().layer_count, 1)

    def test_python_and_jsfl_run_inside(self):
        doc = self.ph.new()
        width = doc.width
        self.assertEqual(self.ph.python("print('hi')\nfl.getDocumentDOM().width + extra", extra=5), width + 5)
        self.assertEqual(self.ph.output, "hi\n")
        # Objects pass in and come back out.
        self.assertEqual(self.ph.python("d.get_timeline().layers[0]", d=doc), doc.get_timeline().layers[0])
        self.assertEqual(self.ph.python("{'a': [1, 2.5, None], 'b': d}", d=doc), {"a": [1, 2.5, None], "b": doc})

        self.assertEqual(self.ph.jsfl("fl.trace('from jsfl'); fl.getDocumentDOM().width * 2"), width * 2)
        self.assertEqual(self.ph.output, "from jsfl\n")
        self.assertEqual(self.ph.jsfl("({list: [1, 'x'], doc: fl.getDocumentDOM()})"), {"list": [1, "x"], "doc": None})
        self.ph.fl.trace("traced")
        self.assertEqual(self.ph.output, "traced\n")

    def test_scripts_run_with_arguments(self):
        doc = self.ph.new()
        script = self.path("helper.jsfl")
        with open(script, "w") as out:
            out.write("function addLayer(name, above) {\n"
                      "  fl.getDocumentDOM().getTimeline().addNewLayer(name, 'normal', above);\n"
                      "  return fl.getDocumentDOM().getTimeline().layerCount;\n}\n")
        self.assertEqual(self.ph.run_script(script, "addLayer", "Ink", True), 2)
        self.assertEqual(doc.get_timeline().layers[0].name, "Ink")
        script = self.path("helper.py")
        with open(script, "w") as out:
            out.write("def names(doc):\n    return [layer.name for layer in doc.getTimeline().layers]\n")
        self.assertEqual(self.ph.run_script(script, "names", doc), ["Ink", "Layer_1"])

    def test_save_open_and_export(self):
        doc = self.ph.new(width=64, height=48)
        doc.get_timeline().add_new_layer("Saved")
        target = self.path("saved.fla")
        self.assertTrue(self.ph.save(doc, target))
        self.assertTrue(os.path.isfile(target))
        self.ph.close(doc)
        self.assertEqual(self.ph.documents, [])

        opened = self.ph.open(target)
        self.assertEqual(opened.name, "saved.fla")
        self.assertEqual([layer.name for layer in opened.get_timeline().layers], ["Saved", "Layer_1"])
        picture = self.path("frame.png")
        self.ph.export_png(opened, picture, frame=0)
        with open(picture, "rb") as image:
            self.assertEqual(image.read(8), b"\x89PNG\r\n\x1a\n")

    def test_phoenix_operations(self):
        self.ph.new(width=200, height=100)
        names = [operation["name"] for operation in self.ph.operations()]
        self.assertIn("get_document", names)
        info = self.ph.call("get_document")
        self.assertEqual((info["width"], info["height"]), (200, 100))
        self.ph.call("add_layer", name="Called")
        self.assertEqual(self.ph.document.get_timeline().layers[0].name, "Called")

    def test_many_objects_are_let_go(self):
        doc = self.ph.new()
        for _ in range(300):
            doc.get_timeline().layers[0].name
        self.assertEqual(doc.get_timeline().layers[0].name, "Layer_1")


class ModuleTests(unittest.TestCase):
    def test_module_functions_share_a_session(self):
        os.environ["PHOENIX_EXE"] = phoenix.find_phoenix(EXE)
        try:
            doc = phoenix.new(width=100, height=50)
            self.assertIn("Phoenix", phoenix.fl.version)
            self.assertEqual(phoenix.documents(), [doc])
            self.assertIs(phoenix.session(), doc._session)
            phoenix.close(doc)
            self.assertEqual(phoenix.documents(), [])
        finally:
            phoenix.shutdown()
        self.assertFalse(doc._session.running)
        with self.assertRaises(phoenix.Error):
            doc.width

    def test_a_missing_phoenix_says_so(self):
        with self.assertRaises(phoenix.PhoenixNotFound):
            phoenix.find_phoenix(os.path.join(tempfile.gettempdir(), "no", "Phoenix.exe"))


class BatchTests(unittest.TestCase):
    def test_batch_python_runs_in_phoenix(self):
        folder = tempfile.mkdtemp(prefix="phoenix_batch_test_")
        try:
            with phoenix.Session(EXE, echo=False) as ph:
                for name in ("a.fla", "b.fla"):
                    ph.save(ph.new(), os.path.join(folder, name))
            script = os.path.join(folder, "rename.py")
            with open(script, "w") as out:
                out.write("doc = fl.getDocumentDOM()\n"
                          "doc.get_timeline().layers[0].name = 'Renamed'\n"
                          "print('did', doc.name)\n")
            exe = phoenix.find_phoenix(EXE)
            result = subprocess.run([exe, "batch", "--python", script, folder, "--save"], capture_output=True,
                                    env=phoenix._environment(exe), timeout=120)
            output = result.stdout.decode("utf-8", "replace")
            self.assertEqual(result.returncode, 0, output + result.stderr.decode("utf-8", "replace"))
            self.assertIn("did a.fla", output)
            self.assertIn("did b.fla", output)
            self.assertIn("2 documents, 0 with problems", output)
            with phoenix.Session(EXE, echo=False) as ph:
                for name in ("a.fla", "b.fla"):
                    doc = ph.open(os.path.join(folder, name))
                    self.assertEqual(doc.get_timeline().layers[0].name, "Renamed")
        finally:
            shutil.rmtree(folder, ignore_errors=True)


if __name__ == "__main__":
    if "--exe" in sys.argv:
        at = sys.argv.index("--exe")
        EXE = sys.argv[at + 1]
        del sys.argv[at:at + 2]
    unittest.main()
