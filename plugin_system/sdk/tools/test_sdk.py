from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
import sys
import zipfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from package_plugin import package_plugin
from validate_plugin import validate_package


class SdkPackagingTests(unittest.TestCase):
    def test_hello_and_scene_info_packages_validate(self) -> None:
        system = HERE.parent.parent
        projects = (
            system / "examples" / "hello-metasiberia",
            HERE.parent / "examples" / "scene-info",
        )
        with tempfile.TemporaryDirectory() as temporary:
            for project in projects:
                output = Path(temporary) / f"{project.name}.mspkg"
                package_plugin(project, output)
                result = validate_package(str(output))
                self.assertGreater(result["files"], 2)
                self.assertEqual(result["runtime"], "luau")

    def test_traversal_entrypoint_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "bad"
            project.mkdir()
            (project / "plugin.json").write_text(
                '{"schemaVersion":1,"id":"org.example.bad","name":"Bad","version":"1.0.0",'
                '"runtime":"luau","entry":"../outside.luau","api":{"major":1},"permissions":[]}',
                encoding="utf-8")
            (project / "main.luau").write_text("-- harmless", encoding="utf-8")
            output = Path(temporary) / "bad.mspkg"
            with self.assertRaises(ValueError):
                package_plugin(project, output)

    def test_duplicate_casefolded_paths_are_rejected(self) -> None:
        manifest = {
            "schemaVersion": 1, "id": "org.example.duplicate", "name": "Duplicate",
            "version": "1.0.0", "runtime": "luau", "entry": "main.luau",
            "api": {"major": 1, "minor": 0}, "permissions": []
        }
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "duplicate.mspkg"
            with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("plugin.json", json.dumps(manifest))
                archive.writestr("main.luau", "-- first")
                archive.writestr("MAIN.LUAU", "-- collision")
            with self.assertRaisesRegex(ValueError, "duplicate package path"):
                validate_package(str(package))

    def test_package_output_cannot_be_added_to_its_own_source(self) -> None:
        project = HERE.parent.parent / "examples" / "hello-metasiberia"
        with self.assertRaisesRegex(ValueError, "outside the plugin source directory"):
            package_plugin(project, project / "hello.mspkg")

    def test_scene_info_v12_package_validates_and_declares_real_capability(self) -> None:
        project = HERE.parent / "examples" / "scene-info"
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "scene-info.mspkg"
            package_plugin(project, output)
            result = validate_package(str(output))
            self.assertEqual(result["id"], "org.metasiberia.examples.scene-info")
            with zipfile.ZipFile(output) as archive:
                manifest = json.loads(archive.read("plugin.json"))
            self.assertEqual(manifest["api"], {"major": 1, "minor": 2})
            self.assertIn("scene.read.summary", manifest["permissions"])

    def test_machine_readable_api_and_worker_schemas_are_consistent(self) -> None:
        system = HERE.parent.parent
        api = json.loads((system / "schemas" / "plugin-api-v1.2.json").read_text(encoding="utf-8"))
        protocol = json.loads((system / "schemas" / "worker-protocol-v1.schema.json").read_text(encoding="utf-8"))
        self.assertEqual(api["api"], {"major": 1, "minor": 2})
        self.assertIn("scene.getSummary", api["methods"])
        self.assertEqual(api["methods"]["scene.getSummary"]["capability"], "scene.read.summary")
        self.assertEqual(protocol["properties"]["protocolMajor"]["const"], 1)
        self.assertIn("sceneSummaryResult", protocol["properties"]["type"]["enum"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
