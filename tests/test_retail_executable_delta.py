"""Synthetic tests for the retail-disc -> verified 1.0 US executable delta builder.

No proprietary GTA San Andreas executables are included or needed for this test.
"""
import importlib.util
from pathlib import Path
import tempfile
import unittest


BUILDER_PATH = Path(__file__).resolve().parents[1] / "installer" / "portable" / "Build-PortablePackage.py"
spec = importlib.util.spec_from_file_location("sa_portable_builder", BUILDER_PATH)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class RetailDiscDeltaTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="retail-delta-fixture-")
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.source = root / "gta_sa.exe"
        self.target = root / "verified" / "gta_sa.exe"
        self.target.parent.mkdir()
        self.source.write_bytes(b"fixture retail source - never a game binary")
        self.target.write_bytes(b"fixture verified target - never a game binary")
        self.input_hash = builder.sha(self.source)
        self.recipes = [{
            "path": "gta_sa.exe",
            "kind": "steam_delta",
            "sha256": builder.sha(self.target),
            "bytes": self.target.stat().st_size,
            "source_sha256": "0" * 64,
            "source": "gta-sa.exe",
            "alternatives": [],
        }]
        builder.VERIFIED_CLASSIC_EXECUTABLES[self.input_hash] = "Synthetic fixture"
        builder.CLASSIC_EXECUTABLE_SIZES[self.input_hash] = self.source.stat().st_size
        self.addCleanup(builder.VERIFIED_CLASSIC_EXECUTABLES.pop, self.input_hash)
        self.addCleanup(builder.CLASSIC_EXECUTABLE_SIZES.pop, self.input_hash)

    def test_local_verified_delta_recipe(self):
        patch, source, target = builder.classic_executable_delta(self.recipes, self.source, self.target)
        self.assertTrue(patch.endswith(".pa30"))
        self.assertEqual(source, self.source)
        self.assertEqual(target, self.target)
        alternative = self.recipes[0]["alternatives"][0]
        self.assertEqual(alternative["source_sha256"], self.input_hash)
        self.assertEqual(alternative["source_bytes"], len(self.source.read_bytes()))
        self.assertEqual(alternative["patch"], patch)
        self.assertEqual(alternative["source"], "gta_sa.exe")

    def test_unknown_source_is_rejected(self):
        builder.VERIFIED_CLASSIC_EXECUTABLES.pop(self.input_hash)
        with self.assertRaisesRegex(ValueError, "no verified compatibility"):
            builder.classic_executable_delta(self.recipes, self.source, self.target)

    def test_changed_source_size_is_rejected(self):
        builder.CLASSIC_EXECUTABLE_SIZES[self.input_hash] += 1
        with self.assertRaisesRegex(ValueError, "size differs"):
            builder.classic_executable_delta(self.recipes, self.source, self.target)

    def test_target_has_to_be_verified(self):
        self.recipes[0]["sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "target differs"):
            builder.classic_executable_delta(self.recipes, self.source, self.target)

    def test_missing_source_is_rejected(self):
        self.source.unlink()
        with self.assertRaisesRegex(ValueError, "audited local executable"):
            builder.classic_executable_delta(self.recipes, self.source, self.target)

    def test_public_retail_digest_is_listed_but_not_embedded(self):
        public_sha = "8c609f108ad737deffbd0d17c702f5974d290c4379de742277b809f80350da1c"
        self.assertIn(public_sha, builder.VERIFIED_CLASSIC_EXECUTABLES)
        self.assertEqual(builder.CLASSIC_EXECUTABLE_SIZES[public_sha], 14_405_632)


if __name__ == "__main__":
    unittest.main()
