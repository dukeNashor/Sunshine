"""Run the real NSIS MSI scanner against a read-only registry fixture.

The fixture has child keys but cannot match an uninstall candidate. The
scanner must finish when EnumRegKey returns an empty name for the next index.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile
import winreg


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--makensis", required=True, type=pathlib.Path)
    args = parser.parse_args()

    registry_path = r"Software\Microsoft\Windows\CurrentVersion\Explorer"
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_path) as key:
        if winreg.QueryInfoKey(key)[0] == 0:
            raise RuntimeError("The registry fixture has no child keys")

    source_path = pathlib.Path(__file__).resolve().parents[2] / "cmake/packaging/windows_nsis_msi_upgrade.nsh"
    source = source_path.read_text(encoding="utf-8")
    original_root = r'HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
    fixture_root = r'HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer'
    if source.count(original_root) < 2:
        raise RuntimeError("The MSI scan registry path has changed")

    # Keep the real enumeration and termination instructions, but make the
    # uninstall branch unreachable so this probe cannot modify installed apps.
    probe = source.replace(original_root, fixture_root)
    candidate_check = 'StrCmp $R2 "Sunshine" 0 privacy_msi_next'
    if probe.count(candidate_check) != 1:
        raise RuntimeError("The MSI candidate check has changed")
    probe = probe.replace(candidate_check, "Goto privacy_msi_next")

    with tempfile.TemporaryDirectory(prefix="sunshine-nsis-scan-") as temporary:
        directory = pathlib.Path(temporary)
        script = directory / "scanner.nsi"
        executable = directory / "scanner.exe"
        install_directory = directory / "install"
        script.write_text(
            'Unicode true\n'
            'Name "Sunshine MSI scan probe"\n'
            f'OutFile "{executable.as_posix()}"\n'
            'SilentInstall silent\n'
            'RequestExecutionLevel user\n'
            f'InstallDir "{install_directory.as_posix()}"\n'
            'Section\n'
            f'{probe}\n'
            'SectionEnd\n',
            encoding="utf-8",
        )
        subprocess.run([str(args.makensis), "/V1", str(script)], check=True, timeout=30)
        try:
            completed = subprocess.run([str(executable)], check=False, timeout=3)
        except subprocess.TimeoutExpired as error:
            raise AssertionError("The NSIS MSI scan did not terminate") from error
        if completed.returncode != 0:
            raise AssertionError(f"The NSIS MSI scan exited with {completed.returncode}")

    print("NSIS MSI scan terminated after the final registry key")
    return 0


if __name__ == "__main__":
    sys.exit(main())
