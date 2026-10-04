"""Renderer routing without launching a GUI; uses isolated stub binaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class Launcher(unittest.TestCase):
    def run_launcher(self, overrides, args=()):
        source = Path(__file__).resolve().parents[1] / "play.sh"
        with tempfile.TemporaryDirectory(prefix="swat-launcher-") as directory:
            root = Path(directory)
            (root / "config").mkdir()
            (root / "config/swat.ini").write_text("")
            launcher = root / "swat"
            launcher.write_text(source.read_text())
            launcher.chmod(0o755)
            for name in ["build/swat/swat", "build/swat/server", "build/swat/windows/swat.exe"]:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#!/bin/bash\nprintf "driver=%s\\nshared=%s\\n" "${GALLIUM_DRIVER:-automatic}" "${WSLENV:-}"\nprintf "arg=%s\\n" "$@"\n')
                path.chmod(0o755)
            env = dict(os.environ)
            for key in ["WSL_INTEROP", "WSLENV", "GALLIUM_DRIVER", "MESA_LOADER_DRIVER_OVERRIDE", "LIBGL_ALWAYS_SOFTWARE", "SWAT_NATIVE_WINDOWS", "SWAT_LIGHTING", "SWAT_EXPOSURE", "SWAT_PLASTER_STYLE", "SWAT_ENVIRONMENT_ASSETS"]:
                env.pop(key, None)
            env.update(overrides)
            result = subprocess.run([str(launcher), *args], env=env, text=True, capture_output=True, check=True)
            return result.stdout, result.stderr

    def test_linux_explicit_driver_and_arguments(self):
        out, err = self.run_launcher({"SWAT_NATIVE_WINDOWS": "0", "GALLIUM_DRIVER": "llvmpipe"}, ["play", "--help"])
        self.assertIn("Linux player", err)
        self.assertIn("driver=llvmpipe", out)
        self.assertIn("arg=--help", out)

    def test_explicit_software_choice(self):
        out, _ = self.run_launcher({"SWAT_NATIVE_WINDOWS": "0", "LIBGL_ALWAYS_SOFTWARE": "true"})
        self.assertIn("driver=automatic", out)

    def test_wsl_hardware_fallback(self):
        if not Path("/dev/dxg").exists():
            self.skipTest("WSLg GPU device required")
        out, _ = self.run_launcher({"SWAT_NATIVE_WINDOWS": "0"})
        self.assertIn("driver=d3d12", out)

    def test_native_overrides_preserve_existing_environment(self):
        out, err = self.run_launcher({"WSL_INTEROP": "fixture", "WSLENV": "EXISTING/p", "SWAT_LIGHTING": "0", "SWAT_EXPOSURE": "0.8", "SWAT_ENVIRONMENT_ASSETS": "/tmp/assets"})
        self.assertIn("Native Windows player", err)
        self.assertIn("EXISTING/p:SWAT_LIGHTING/w:SWAT_EXPOSURE/w:SWAT_ENVIRONMENT_ASSETS/pw", out)

    def test_native_registered_interop_without_shell_variable(self):
        if not Path("/proc/sys/fs/binfmt_misc/WSLInterop").exists():
            self.skipTest("registered WSL interop required")
        _, err = self.run_launcher({})
        self.assertIn("Native Windows player", err)


if __name__ == "__main__":
    unittest.main()
