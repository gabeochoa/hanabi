import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("branding", ROOT / "scripts/branding.py")
branding = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(branding)


class BrandingTest(unittest.TestCase):
    def test_alternate_identity_with_spaces(self):
        values = branding.validate({
            "app_name": "Ember Desktop",
            "bundle_id": "io.github.gabeochoa.ember",
            "executable_name": "Ember Runner",
            "url_scheme": "ember",
        })
        with tempfile.TemporaryDirectory() as directory:
            branding.generate(values, ROOT / "resources/macos/Info.plist", directory)
            plist = plistlib.loads((Path(directory) / "Info.plist").read_bytes())
            header = (Path(directory) / "branding.h").read_text(encoding="utf-8")
        self.assertEqual(plist["CFBundleDisplayName"], "Ember Desktop")
        self.assertEqual(plist["CFBundleExecutable"], "Ember Runner")
        self.assertEqual(plist["CFBundleIdentifier"], "io.github.gabeochoa.ember")
        self.assertEqual(plist["CFBundleURLTypes"][0]["CFBundleURLSchemes"], ["ember"])
        self.assertIn('kSpotlightDomain[] = "io.github.gabeochoa.ember.threads"', header)
        self.assertIn('kNotificationPrefix[] = "io.github.gabeochoa.ember.notification."', header)

    def test_invalid_app_names(self):
        base = branding.load_config(ROOT / "resources/macos/branding.json")
        for value in ("", "../Ember", " Ember", "Ember.app", "Ember;open"):
            changed = dict(base, app_name=value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                branding.validate(changed)

    def test_invalid_executable_names(self):
        base = branding.load_config(ROOT / "resources/macos/branding.json")
        for value in ("", "../ember", "Ember/Runner", "ember.app", "ember$(id)"):
            changed = dict(base, executable_name=value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                branding.validate(changed)

    def test_invalid_bundle_ids(self):
        base = branding.load_config(ROOT / "resources/macos/branding.json")
        for value in ("ember", "io.ember", ".io.github.ember", "io.github.bad_value", "1o.github.ember"):
            changed = dict(base, bundle_id=value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                branding.validate(changed)

    def test_invalid_url_schemes(self):
        base = branding.load_config(ROOT / "resources/macos/branding.json")
        for value in ("", "1ember", "ember app", "ember://", "ember_shell"):
            changed = dict(base, url_scheme=value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                branding.validate(changed)


if __name__ == "__main__":
    unittest.main()
