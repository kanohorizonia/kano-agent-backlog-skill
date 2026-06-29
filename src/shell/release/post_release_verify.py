#!/usr/bin/env python3
"""Post-release install verification and release-channel recheck helpers."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def fetch_json(url: str) -> dict[str, Any]:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": "kano-post-release-verify",
        },
    )
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def load_release(repo: str, tag: str, release_json: str | None) -> dict[str, Any]:
    if release_json:
        return load_json(Path(release_json))
    return fetch_json(f"https://api.github.com/repos/{repo}/releases/tags/{tag}")


def release_assets(release: dict[str, Any]) -> list[dict[str, Any]]:
    return list(release.get("assets") or [])


def asset_name(asset: dict[str, Any]) -> str:
    return str(asset.get("name") or "")


def asset_url(asset: dict[str, Any]) -> str:
    return str(asset.get("browser_download_url") or asset.get("url") or "")


def asset_sha256(asset: dict[str, Any]) -> str:
    digest = str(asset.get("digest") or "")
    if digest.startswith("sha256:"):
        return digest.split(":", 1)[1]
    return digest


def asset_summary(asset: dict[str, Any] | None) -> dict[str, Any]:
    if not asset:
        return {}
    return {
        "name": asset_name(asset),
        "url": asset_url(asset),
        "sha256": asset_sha256(asset),
        "size": asset.get("size"),
    }


def is_manifest_sidecar(name: str) -> bool:
    lowered = name.lower()
    return (
        lowered.endswith(".manifest.csv")
        or lowered.endswith(".manifest.summary.json")
        or lowered in {"release-artifact-index.json", "release-artifact-checksums.sha256.txt"}
    )


def is_release_payload(name: str) -> bool:
    lowered = name.lower()
    return lowered.endswith("-release-payload.tar") or "-release-payload.tar." in lowered


def is_installable_asset(name: str) -> bool:
    lowered = name.lower()
    if is_manifest_sidecar(lowered) or is_release_payload(lowered):
        return False
    return lowered.endswith((".tar.gz", ".tgz", ".zip", ".msi"))


def public_name_is_concise(name: str) -> bool:
    lowered = name.lower()
    return not (
        lowered.startswith("kanohorizonia.kanobacklog-")
        or lowered.startswith("kano-agent-backlog-skill-")
    )


def find_platform_asset(assets: list[dict[str, Any]], platform_id: str, suffixes: tuple[str, ...] = (".tar.gz", ".tgz")) -> dict[str, Any] | None:
    lowered_platform = platform_id.lower()
    candidates = []
    for asset in assets:
        name = asset_name(asset)
        lowered = name.lower()
        if is_manifest_sidecar(lowered) or is_release_payload(lowered):
            continue
        if lowered_platform in lowered and lowered.endswith(suffixes):
            candidates.append(asset)
    candidates.sort(key=lambda item: asset_name(item))
    return candidates[0] if candidates else None


def find_windows_msi(assets: list[dict[str, Any]]) -> dict[str, Any] | None:
    candidates = [
        asset
        for asset in assets
        if "windows" in asset_name(asset).lower() and asset_name(asset).lower().endswith(".msi")
    ]
    candidates.sort(key=lambda item: asset_name(item))
    return candidates[0] if candidates else None


def formula_class_name(formula_name: str) -> str:
    parts = [part for part in "".join(ch if ch.isalnum() else " " for ch in formula_name).split() if part]
    return "".join(part[:1].upper() + part[1:] for part in parts) or "KanoBacklog"


def render_homebrew_formula(
    formula_name: str,
    version: str,
    repo: str,
    arm_asset: dict[str, Any],
    x64_asset: dict[str, Any],
) -> str:
    class_name = formula_class_name(formula_name)
    return f'''class {class_name} < Formula
  desc "Kano Agent Backlog native CLI"
  homepage "https://github.com/{repo}"
  version "{version}"

  on_macos do
    if Hardware::CPU.arm?
      url "{asset_url(arm_asset)}"
      sha256 "{asset_sha256(arm_asset)}"
    else
      url "{asset_url(x64_asset)}"
      sha256 "{asset_sha256(x64_asset)}"
    end
  end

  def install
    libexec.install Dir["*"]
    payload = Dir[libexec/"*/scripts/kob"].map {{ |kob| Pathname.new(kob).dirname.parent }}.first
    odie "installed payload does not contain scripts/kob" unless payload
    bin.install_symlink payload/"scripts/kob" => "kob"
    bin.install_symlink payload/"scripts/kano-backlog" => "kano-backlog"
  end

  def caveats
    <<~EOS
      Run `kob --help` after install. The skill payload is installed under Homebrew libexec.
    EOS
  end

  test do
    system "#{{bin}}/kano-backlog", "version"
  end
end
'''


def markdown_table(rows: list[tuple[str, str]]) -> str:
    lines = ["| Field | Value |", "| --- | --- |"]
    for key, value in rows:
        lines.append(f"| {key} | {value} |")
    return "\n".join(lines) + "\n"


def command_exists(name: str) -> bool:
    return shutil.which(name) is not None


def run_command(command: list[str], cwd: Path | None = None, timeout: int = 120) -> dict[str, Any]:
    started = time.time()
    proc = subprocess.run(command, cwd=str(cwd) if cwd else None, text=True, capture_output=True, timeout=timeout)
    return {
        "command": command,
        "exitCode": proc.returncode,
        "durationSeconds": round(time.time() - started, 3),
        "stdoutTail": proc.stdout[-4000:],
        "stderrTail": proc.stderr[-4000:],
    }


def safe_extract_tar(archive: Path, destination: Path) -> None:
    with tarfile.open(archive, "r:*") as tar:
        for member in tar.getmembers():
            target = destination / member.name
            resolved = target.resolve()
            if not str(resolved).startswith(str(destination.resolve())):
                raise RuntimeError(f"unsafe tar member path: {member.name}")
        tar.extractall(destination)


def download_asset(asset: dict[str, Any], destination: Path) -> Path:
    def to_local_path(value: str) -> Path:
        if os.name == "nt" and len(value) > 3 and value[0] == "/" and value[2] == "/":
            return Path(f"{value[1]}:{value[2:]}")
        return Path(value)

    if asset.get("local_path"):
        return to_local_path(str(asset["local_path"]))
    url = asset_url(asset)
    if url.startswith("file://"):
        return to_local_path(url[7:])
    target = destination / asset_name(asset)
    with urllib.request.urlopen(url, timeout=120) as response:
        target.write_bytes(response.read())
    return target


def run_portable_tar_smoke(asset: dict[str, Any], output_dir: Path) -> dict[str, Any]:
    def path_for_bash(path: Path) -> str:
        text = str(path)
        if os.name != "nt":
            return text
        normalized = text.replace("\\", "/")
        if len(normalized) > 2 and normalized[1] == ":":
            return f"/{normalized[0].lower()}{normalized[2:]}"
        return normalized

    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kano-install-verify-", dir=str(output_dir.resolve())) as temp_text:
        temp_dir = Path(temp_text)
        archive = download_asset(asset, temp_dir)
        extract_dir = temp_dir / "extract"
        extract_dir.mkdir()
        safe_extract_tar(archive, extract_dir)
        if os.name == "nt":
            candidates = [
                extract_dir / "kano-agent-backlog-skill" / "scripts" / "kob.bat",
                extract_dir / "scripts" / "kob.bat",
                extract_dir / "kano-agent-backlog-skill" / "scripts" / "kano-backlog.bat",
                extract_dir / "scripts" / "kano-backlog.bat",
                extract_dir / "kano-agent-backlog-skill" / "scripts" / "kob",
                extract_dir / "scripts" / "kob",
            ]
        else:
            candidates = [
                extract_dir / "kano-agent-backlog-skill" / "scripts" / "kob",
                extract_dir / "scripts" / "kob",
                extract_dir / "kano-agent-backlog-skill" / "scripts" / "kano-backlog",
                extract_dir / "scripts" / "kano-backlog",
            ]
        executable = next((path for path in candidates if path.exists()), None)
        if not executable:
            recursive_names = ["kob.bat", "kano-backlog.bat", "kob", "kano-backlog"] if os.name == "nt" else ["kob", "kano-backlog", "kob.bat", "kano-backlog.bat"]
            for script_name in recursive_names:
                matches = sorted(path for path in extract_dir.rglob(script_name) if path.parent.name == "scripts")
                if matches:
                    executable = matches[0]
                    break
        if not executable:
            return {"status": "fail", "reason": "portable archive did not contain scripts/kob or scripts/kano-backlog"}
        relative_executable = str(executable.relative_to(extract_dir))
        if os.name == "nt" and executable.suffix.lower() in {".bat", ".cmd"}:
            command = ["cmd", "/c", str(executable), "version"]
            command_display = ["cmd", "/c", relative_executable, "version"]
        elif command_exists("bash"):
            command = ["bash", path_for_bash(executable), "version"]
            command_display = ["bash", relative_executable, "version"]
        else:
            command = [str(executable), "version"]
            command_display = [relative_executable, "version"]
        result = run_command(command, timeout=60)
        temp_markers = [str(temp_dir), path_for_bash(temp_dir)]
        for key in ("stdoutTail", "stderrTail"):
            for marker in temp_markers:
                result[key] = str(result[key]).replace(marker, "<install-verify-workdir>")
        result["command"] = command_display
        result["status"] = "pass" if result["exitCode"] == 0 else "fail"
        result["executable"] = relative_executable
        return result


def asset_recheck(args: argparse.Namespace) -> int:
    release = load_release(args.repo, args.tag, args.release_json)
    assets = release_assets(release)
    sidecars = [asset_name(asset) for asset in assets if is_manifest_sidecar(asset_name(asset))]
    payloads = [asset_name(asset) for asset in assets if is_release_payload(asset_name(asset))]
    installable = [asset_name(asset) for asset in assets if is_installable_asset(asset_name(asset))]
    windows_msi = [asset_name(asset) for asset in assets if asset_name(asset).lower().endswith(".msi") and "windows" in asset_name(asset).lower()]
    concise_violations = [name for name in installable if not public_name_is_concise(name)]
    blockers = []
    if release.get("draft"):
        blockers.append("release is still draft")
    if release.get("prerelease"):
        blockers.append("release is still prerelease")
    if sidecars:
        blockers.append("public release still exposes internal manifest/checksum/index sidecars")
    if payloads:
        blockers.append("public release still exposes release payload tar artifacts")
    if not windows_msi:
        blockers.append("public release has no Windows MSI asset")
    if concise_violations:
        blockers.append("public installable asset names still include internal package prefixes")
    status = "pass" if not blockers else "blocked"

    output_dir = Path(args.output_dir)
    report = {
        "schemaVersion": 1,
        "kind": "kano-release-asset-msi-recheck",
        "repo": args.repo,
        "tag": args.tag,
        "releaseUrl": release.get("html_url") or f"https://github.com/{args.repo}/releases/tag/{args.tag}",
        "status": status,
        "assetCount": len(assets),
        "installableAssets": installable,
        "internalSidecars": sidecars,
        "releasePayloads": payloads,
        "windowsMsiAssets": windows_msi,
        "conciseNameViolations": concise_violations,
        "blockers": blockers,
    }
    write_json(output_dir / "release-asset-msi-recheck.json", report)
    rows = [
        ("Repo", args.repo),
        ("Tag", args.tag),
        ("Status", status),
        ("Asset count", str(len(assets))),
        ("Installable assets", str(len(installable))),
        ("Internal sidecars", str(len(sidecars))),
        ("Windows MSI assets", str(len(windows_msi))),
        ("Concise name violations", str(len(concise_violations))),
        ("Blockers", "; ".join(blockers) if blockers else "none"),
    ]
    write_text(output_dir / "release-asset-msi-recheck.md", "# Release Asset / MSI Recheck\n\n" + markdown_table(rows))
    print(output_dir / "release-asset-msi-recheck.json")
    return 1 if args.fail_on_blocker and blockers else 0


def homebrew_validate(args: argparse.Namespace) -> int:
    release = load_release(args.repo, args.tag, args.release_json)
    assets = release_assets(release)
    arm_asset = find_platform_asset(assets, "macos-arm64")
    x64_asset = find_platform_asset(assets, "macos-x64")
    output_dir = Path(args.output_dir)
    blockers = []
    if not arm_asset:
        blockers.append("missing macos-arm64 tar.gz release asset")
    if not x64_asset:
        blockers.append("missing macos-x64 tar.gz release asset")
    formula_path = output_dir / f"{args.formula_name}.rb"
    if arm_asset and x64_asset:
        formula = render_homebrew_formula(args.formula_name, args.version, args.repo, arm_asset, x64_asset)
        write_text(formula_path, formula)
        if f"https://github.com/{args.repo}/releases/download/{args.tag}/" not in formula:
            blockers.append("formula URL does not target the requested GitHub release")
        if not asset_sha256(arm_asset) or not asset_sha256(x64_asset):
            blockers.append("formula asset SHA256 data missing")
        if 'payload/"scripts/kob"' not in formula or 'payload/"scripts/kano-backlog"' not in formula:
            blockers.append("formula does not expose expected kob/kano-backlog symlinks")
        if "def caveats" not in formula:
            blockers.append("formula does not include post-install usage guidance")
    else:
        write_text(formula_path, "# Formula not generated because macOS release assets are incomplete.\n")

    host = {
        "os": platform.system(),
        "machine": platform.machine(),
        "brewAvailable": command_exists("brew"),
    }
    install_smoke: dict[str, Any] = {"status": "skipped", "reason": "execute install disabled"}
    if args.execute_install:
        if host["os"] != "Darwin":
            install_smoke = {"status": "skipped", "reason": "Homebrew install smoke requires macOS host"}
        elif not host["brewAvailable"]:
            install_smoke = {"status": "skipped", "reason": "brew command not available"}
        elif blockers:
            install_smoke = {"status": "skipped", "reason": "formula inspection blockers present"}
        else:
            install_smoke = run_command(["brew", "install", "--formula", str(formula_path)], timeout=600)
            install_smoke["status"] = "pass" if install_smoke["exitCode"] == 0 else "fail"
            if install_smoke["status"] == "pass":
                install_smoke["kobVersion"] = run_command(["kob", "--version"], timeout=120)
    status = "pass" if not blockers and install_smoke.get("status") in {"pass", "skipped"} else "blocked"
    if install_smoke.get("status") == "skipped":
        status = "validated-without-install" if not blockers else "blocked"

    report = {
        "schemaVersion": 1,
        "kind": "kano-homebrew-owned-tap-validation",
        "repo": args.repo,
        "tag": args.tag,
        "version": args.version,
        "formulaName": args.formula_name,
        "status": status,
        "formulaPath": str(formula_path.name),
        "assets": {
            "macosArm64": asset_summary(arm_asset),
            "macosX64": asset_summary(x64_asset),
        },
        "host": host,
        "installSmoke": install_smoke,
        "blockers": blockers,
        "nonGoals": [
            "homebrew-core not touched",
            "shared tap branch not pushed",
            "formula generation is validation evidence, not publication",
        ],
    }
    write_json(output_dir / "homebrew-owned-tap-validation.json", report)
    rows = [
        ("Repo", args.repo),
        ("Tag", args.tag),
        ("Status", status),
        ("Formula", formula_path.name),
        ("macOS arm64 asset", asset_name(arm_asset or {})),
        ("macOS x64 asset", asset_name(x64_asset or {})),
        ("Install smoke", str(install_smoke.get("status"))),
        ("Install smoke reason", str(install_smoke.get("reason", ""))),
        ("Blockers", "; ".join(blockers) if blockers else "none"),
    ]
    write_text(output_dir / "homebrew-owned-tap-validation.md", "# Homebrew Owned Tap Validation\n\n" + markdown_table(rows))
    print(output_dir / "homebrew-owned-tap-validation.json")
    return 1 if args.fail_on_blocker and blockers else 0


def install_verify(args: argparse.Namespace) -> int:
    release = load_release(args.repo, args.tag, args.release_json)
    assets = release_assets(release)
    output_dir = Path(args.output_dir)
    platform_id = args.platform
    attempts: list[dict[str, Any]] = []
    selected: dict[str, Any] | None = None

    def add_attempt(channel: str, status: str, reason: str, asset: dict[str, Any] | None = None, command: str = "") -> None:
        attempts.append({
            "channel": channel,
            "status": status,
            "reason": reason,
            "artifactUrl": asset_url(asset or {}),
            "artifactName": asset_name(asset or {}),
            "installCommand": command,
        })

    if platform_id.startswith("macos"):
        if args.enable_homebrew:
            add_attempt("homebrew", "candidate", "owned tap formula validation is available", command=f"brew install {args.formula_name}")
            selected = attempts[-1]
        else:
            add_attempt("homebrew", "skipped", "homebrew channel disabled")
    elif platform_id.startswith("windows"):
        if args.enable_winget:
            if args.execute_install and command_exists("winget"):
                add_attempt("winget", "candidate", "winget command available", command="winget install --id KanoHorizonia.KanoBacklog -e")
                selected = attempts[-1]
            else:
                add_attempt("winget", "skipped", "winget disabled by host or execute_install=false")
        msi = find_windows_msi(assets)
        if args.enable_msi and msi:
            add_attempt("msi", "candidate", "Windows MSI release asset found", msi, command=f"msiexec /i {asset_url(msi)} /qn")
            selected = selected or attempts[-1]
        elif args.enable_msi:
            add_attempt("msi", "skipped", "Windows MSI release asset not found")
    elif platform_id.startswith("linux"):
        if args.enable_apt:
            add_attempt("apt", "skipped", "apt repository source is not configured for this release")

    tar_asset = find_platform_asset(assets, platform_id)
    if not selected and args.allow_tar_fallback and tar_asset:
        add_attempt("portable-tar", "candidate", "fallback portable archive selected", tar_asset, command=f"download/extract/run {asset_name(tar_asset)}")
        selected = attempts[-1]
    elif not selected and args.allow_tar_fallback:
        add_attempt("portable-tar", "skipped", f"no portable tar asset found for {platform_id}")

    cli_smoke: dict[str, Any] = {"status": "not-run", "reason": "no selected candidate"}
    if selected:
        selected["status"] = "selected"
        if args.execute_install and selected["channel"] == "portable-tar" and tar_asset:
            cli_smoke = run_portable_tar_smoke(tar_asset, output_dir)
        elif args.execute_install:
            cli_smoke = {"status": "not-run", "reason": f"{selected['channel']} install execution is not automated by this repo-local script"}
        else:
            cli_smoke = {"status": "not-run", "reason": "execute_install=false"}

    status = "pass" if cli_smoke.get("status") == "pass" else ("warn" if selected else "fail")
    report = {
        "schemaVersion": 1,
        "kind": "kano-post-release-install-verification",
        "repo": args.repo,
        "tag": args.tag,
        "version": args.version,
        "platform": platform_id,
        "status": status,
        "selectedChannel": selected["channel"] if selected else "",
        "artifactUrl": selected.get("artifactUrl", "") if selected else "",
        "installCommand": selected.get("installCommand", "") if selected else "",
        "fallbackReason": selected.get("reason", "") if selected and selected["channel"] == "portable-tar" else "",
        "cliSmokeResult": cli_smoke,
        "attempts": attempts,
        "cloudBackend": args.cloud_backend,
        "executeInstall": args.execute_install,
    }
    write_json(output_dir / f"install-verification-{platform_id}.json", report)
    rows = [
        ("Repo", args.repo),
        ("Tag", args.tag),
        ("Platform", platform_id),
        ("Status", status),
        ("Selected channel", report["selectedChannel"] or "none"),
        ("Artifact URL", report["artifactUrl"] or "none"),
        ("Install command", report["installCommand"] or "none"),
        ("CLI smoke", str(cli_smoke.get("status"))),
        ("Fallback reason", report["fallbackReason"] or "none"),
    ]
    write_text(output_dir / f"install-verification-{platform_id}.md", "# Post-Release Install Verification\n\n" + markdown_table(rows))
    print(output_dir / f"install-verification-{platform_id}.json")
    if args.fail_if_no_pass and status != "pass":
        return 1
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Kano post-release verification helpers")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_release_flags(p: argparse.ArgumentParser) -> None:
        p.add_argument("--repo", default="kanohorizonia/kano-agent-backlog-skill")
        p.add_argument("--tag", default="v0.0.4")
        p.add_argument("--release-json", default="")
        p.add_argument("--output-dir", required=True)

    p = sub.add_parser("asset-recheck")
    add_release_flags(p)
    p.add_argument("--fail-on-blocker", action="store_true")
    p.set_defaults(func=asset_recheck)

    p = sub.add_parser("homebrew-validate")
    add_release_flags(p)
    p.add_argument("--version", default="0.0.4")
    p.add_argument("--formula-name", default="kano-backlog")
    p.add_argument("--execute-install", action="store_true")
    p.add_argument("--fail-on-blocker", action="store_true")
    p.set_defaults(func=homebrew_validate)

    p = sub.add_parser("install-verify")
    add_release_flags(p)
    p.add_argument("--version", default="0.0.4")
    p.add_argument("--platform", required=True)
    p.add_argument("--formula-name", default="kano-backlog")
    p.add_argument("--enable-homebrew", action=argparse.BooleanOptionalAction, default=True)
    p.add_argument("--enable-winget", action=argparse.BooleanOptionalAction, default=True)
    p.add_argument("--enable-apt", action=argparse.BooleanOptionalAction, default=True)
    p.add_argument("--enable-msi", action=argparse.BooleanOptionalAction, default=True)
    p.add_argument("--allow-tar-fallback", action=argparse.BooleanOptionalAction, default=True)
    p.add_argument("--execute-install", action="store_true")
    p.add_argument("--fail-if-no-pass", action="store_true")
    p.add_argument("--cloud-backend", default="local")
    p.set_defaults(func=install_verify)
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
