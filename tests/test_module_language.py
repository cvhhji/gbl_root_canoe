"""Run the actual module shell with temporary metadata and fake Android APIs."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "targets/magisk_module/module/bin/bl_flasher.sh").read_text(encoding="utf-8")
BASH = shutil.which("bash") or "D:/Git/Git/bin/bash.exe"
MOCKS = r'''
getprop() {
  case "$1" in
    ro.boot.slot_suffix) printf '_a\n' ;;
    *) printf 'en-US\n' ;;
  esac
}
command() {
  if [ "$1" = -v ] && [ "$2" = ksud ] && [ "$MOCK_KSU" = 0 ]; then return 1; fi
  builtin command "$@"
}
ksud() {
  [ "$MOCK_KSU" = 1 ] || return 127
  case "$3" in
    get) cat "$MODDIR/ksu_language" 2>/dev/null ;;
    set)
      [ "${MOCK_SAVE_FAIL:-0}" = 0 ] || return 1
      printf '%s\n' "$5" > "$MODDIR/ksu_language" ;;
    *) return 1 ;;
  esac
}
'''


class ModuleLanguageTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.module = Path(self.directory.name)
        self.prop = self.module / "module.prop"
        self.prop.write_text("id=fake_bl_efisp\nname=假回锁\ndescription=before\n", encoding="utf-8")

    def run_script(self, *args, **settings):
        env = {**os.environ, "MODDIR": self.module.as_posix(), "MOCK_KSU": "0", **settings}
        runner = self.module / "runner.sh"
        runner.write_text(MOCKS + "\n" + SCRIPT, encoding="utf-8", newline="\n")
        return subprocess.run([BASH, runner.as_posix(), *args],
                              env=env, text=True, encoding="utf-8", capture_output=True, timeout=10)

    def test_apatch_and_folkpatch_language_survives_new_shell(self):
        for language, name in (("en", "Fake BL EFISP"), ("zh", "假回锁")):
            result = self.run_script("set-language", language)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("name=" + name, self.prop.read_text(encoding="utf-8"))
            status = self.run_script("status")
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertIn("USER_LANG=" + language, status.stdout)
        self.assertFalse((self.module / "ksu_language").exists())
        self.assertFalse((self.module / "lang.txt").exists())

    def test_kernelsu_configuration_remains_authoritative(self):
        config = self.module / "ksu_language"
        config.write_text("en\n", encoding="utf-8")
        status = self.run_script("status", MOCK_KSU="1")
        self.assertIn("USER_LANG=en", status.stdout)
        result = self.run_script("set-language", "zh", MOCK_KSU="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(config.read_text().strip(), "zh")

    def test_kernelsu_save_failure_is_not_reported_as_success(self):
        before = self.prop.read_bytes()
        result = self.run_script("set-language", "en", MOCK_KSU="1", MOCK_SAVE_FAIL="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.prop.read_bytes(), before)

    def test_invalid_language_cannot_modify_metadata(self):
        before = self.prop.read_bytes()
        result = self.run_script("set-language", "invalid")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.prop.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
