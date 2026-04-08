#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${KOG_CPP_ROOT:-}" ]]; then
  echo "KOG_CPP_ROOT is not set." >&2
  exit 1
fi

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/windows/windows_preset_build.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/windows/windows_preset_build.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/windows/windows_preset_build.sh"
  return 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_metadata.sh"

KOG_WINDOWS_PS_HELPER="$SCRIPT_DIR/windows_preset_helper.ps1"

kog_powershell_bin() {
  local candidate
  for candidate in powershell powershell.exe pwsh pwsh.exe; do
    if command -v "$candidate" >/dev/null 2>&1; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

kog_run_windows_ps_helper() {
  local PowerShellBin=""
  PowerShellBin="$(kog_powershell_bin)" || return 127
  "$PowerShellBin" -NoProfile -ExecutionPolicy Bypass -File "$KOG_WINDOWS_PS_HELPER" "$@"
}

kog_windows_file_exists() {
  local InPath="$1"
  kog_run_windows_ps_helper -Action test-path -Path "$InPath" >/dev/null 2>&1
}

kog_detect_vcvarsall() {
  local Found=""
  if kog_powershell_bin >/dev/null 2>&1; then
    Found="$(kog_run_windows_ps_helper -Action detect-vcvarsall 2>/dev/null | tr -d '\r')"
  fi

  if [[ -n "$Found" ]] && kog_windows_file_exists "$Found"; then
    printf '%s\n' "$Found"
    return 0
  fi

  return 1
}

kog_resolve_windows_source_root() {
  local InRootWin="$1"
  local InConfigurePreset="$2"
  local DecisionAndRoot=""
  local Decision=""
  local EffectiveRoot=""

  DecisionAndRoot="$(
    kog_run_windows_ps_helper -Action resolve-source-root -Root "$InRootWin" -Preset "$InConfigurePreset" \
      | tr -d '\r'
  )"

  Decision="${DecisionAndRoot%%|*}"
  EffectiveRoot="${DecisionAndRoot#*|}"
  if [[ -z "$EffectiveRoot" ]]; then
    EffectiveRoot="$InRootWin"
  fi

  case "$Decision" in
    use-cache-home)
      echo "[launcher][cmake-cache][info] detected path-alias cache; reuse source root: $EffectiveRoot" >&2
      ;;
    clean-cache)
      echo "[launcher][cmake-cache][warn] removed incompatible cache dir for preset: $InConfigurePreset" >&2
      ;;
  esac

  printf '%s\n' "$EffectiveRoot"
}

kog_prepare_windows_subst_root() {
  local InRootWin="$1"
  local InConfigurePreset="$2"
  local InSubstPurpose="$3"
  local InPreferredSubstDrive="$4"
  local InSubstMode="${KOG_SUBST_MODE:-auto}"

  : "$InSubstPurpose"
  kog_run_windows_ps_helper -Action prepare-subst-root -Root "$InRootWin" -Preset "$InConfigurePreset" -PreferredDrive "$InPreferredSubstDrive" -Mode "$InSubstMode" \
    | tr -d '\r'
}

kog_cleanup_windows_subst_drive() {
  local InMappedDrive="$1"
  local InCleanupFlag="$2"
  local InSubstPurpose="$3"

  if [[ "$InCleanupFlag" != "1" ]]; then
    return 0
  fi
  if [[ -z "$InMappedDrive" ]]; then
    return 0
  fi

  kog_run_windows_ps_helper -Action cleanup-subst -Drive "$InMappedDrive" >/dev/null 2>&1 || true
  echo "[launcher][subst][info] unmapped $InMappedDrive (purpose: $InSubstPurpose)" >&2
}

kog_run_windows_preset() {
  local InConfigurePreset="$1"
  local InBuildPreset="$2"
  local InVcvarsArch="$3"
  local InSubstPurpose="${KOG_SUBST_PURPOSE:-kano-git cpp build}"
  local InPreferredSubstDrive="${KOG_SUBST_DRIVE:-}"

  if ! command -v cmd.exe >/dev/null 2>&1; then
    echo "cmd.exe is required." >&2
    exit 1
  fi

  if ! kog_powershell_bin >/dev/null 2>&1; then
    echo "powershell is required." >&2
    exit 1
  fi

  if [[ ! -f "$KOG_WINDOWS_PS_HELPER" ]]; then
    echo "windows preset helper script not found: $KOG_WINDOWS_PS_HELPER" >&2
    exit 1
  fi

  local RequestedVcvars="${KOG_VCVARSALL:-}"
  local ResolvedVcvars=""
  if [[ -n "$RequestedVcvars" ]]; then
    ResolvedVcvars="$RequestedVcvars"
  else
    ResolvedVcvars="$(kog_detect_vcvarsall || true)"
  fi

  if ! kog_windows_file_exists "$ResolvedVcvars"; then
    echo "vcvarsall.bat not found." >&2
    echo "Set KOG_VCVARSALL explicitly, e.g.:" >&2
    echo "  KOG_VCVARSALL='C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat'" >&2
    exit 1
  fi

  local RootWin

  kog_apply_self_build_config
  kog_collect_build_metadata

  if command -v cygpath >/dev/null 2>&1; then
    RootWin="$(cygpath -w "$KOG_CPP_ROOT")"
  else
    RootWin="$(cd "$KOG_CPP_ROOT" && pwd -W)"
  fi

  local BuildRootWin
  local EffectiveRootWin
  local SubstDrive
  local SubstCleanupFlag
  local SubstLine

  BuildRootWin="$RootWin"
  SubstDrive=""
  SubstCleanupFlag="0"
  SubstLine="$(kog_prepare_windows_subst_root "$BuildRootWin" "$InConfigurePreset" "$InSubstPurpose" "$InPreferredSubstDrive")"
  if [[ -n "$SubstLine" ]]; then
    BuildRootWin="${SubstLine%%$'\t'*}"
    local _rest="${SubstLine#*$'\t'}"
    SubstDrive="${_rest%%$'\t'*}"
    SubstCleanupFlag="${SubstLine##*$'\t'}"
  fi
  if [[ -n "$SubstDrive" && "$BuildRootWin" != "$RootWin" ]]; then
    echo "[launcher][subst][info] mapped $SubstDrive -> $RootWin (purpose: $InSubstPurpose)" >&2
  fi

  EffectiveRootWin="$(kog_resolve_windows_source_root "$BuildRootWin" "$InConfigurePreset")"
  if [[ -n "$EffectiveRootWin" ]]; then
    BuildRootWin="$EffectiveRootWin"
  fi

  local ExitCode=0
  kog_run_windows_ps_helper \
    -Action run-preset \
    -Root "$BuildRootWin" \
    -Vcvars "$ResolvedVcvars" \
    -Arch "$InVcvarsArch" \
    -ConfigurePreset "$InConfigurePreset" \
    -BuildPreset "$InBuildPreset" || ExitCode=$?

  kog_cleanup_windows_subst_drive "$SubstDrive" "$SubstCleanupFlag" "$InSubstPurpose"
  return "$ExitCode"
}

kog_configure_windows_preset() {
  local InConfigurePreset="$1"
  local InVcvarsArch="$2"
  local InSubstPurpose="${KOG_SUBST_PURPOSE:-kano-git cpp configure}"
  local InPreferredSubstDrive="${KOG_SUBST_DRIVE:-}"

  if ! command -v cmd.exe >/dev/null 2>&1; then
    echo "cmd.exe is required." >&2
    exit 1
  fi

  if ! kog_powershell_bin >/dev/null 2>&1; then
    echo "powershell is required." >&2
    exit 1
  fi

  if [[ ! -f "$KOG_WINDOWS_PS_HELPER" ]]; then
    echo "windows preset helper script not found: $KOG_WINDOWS_PS_HELPER" >&2
    exit 1
  fi

  local RequestedVcvars="${KOG_VCVARSALL:-}"
  local ResolvedVcvars=""
  if [[ -n "$RequestedVcvars" ]]; then
    ResolvedVcvars="$RequestedVcvars"
  else
    ResolvedVcvars="$(kog_detect_vcvarsall || true)"
  fi

  if ! kog_windows_file_exists "$ResolvedVcvars"; then
    echo "vcvarsall.bat not found." >&2
    echo "Set KOG_VCVARSALL explicitly, e.g.:" >&2
    echo "  KOG_VCVARSALL='C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat'" >&2
    exit 1
  fi

  local RootWin
  kog_apply_self_build_config
  kog_collect_build_metadata

  if command -v cygpath >/dev/null 2>&1; then
    RootWin="$(cygpath -w "$KOG_CPP_ROOT")"
  else
    RootWin="$(cd "$KOG_CPP_ROOT" && pwd -W)"
  fi

  local BuildRootWin="$RootWin"
  local EffectiveRootWin=""
  local SubstDrive=""
  local SubstCleanupFlag="0"
  local SubstLine=""

  SubstLine="$(kog_prepare_windows_subst_root "$BuildRootWin" "$InConfigurePreset" "$InSubstPurpose" "$InPreferredSubstDrive")"
  if [[ -n "$SubstLine" ]]; then
    BuildRootWin="${SubstLine%%$'\t'*}"
    local _rest="${SubstLine#*$'\t'}"
    SubstDrive="${_rest%%$'\t'*}"
    SubstCleanupFlag="${SubstLine##*$'\t'}"
  fi
  if [[ -n "$SubstDrive" && "$BuildRootWin" != "$RootWin" ]]; then
    echo "[launcher][subst][info] mapped $SubstDrive -> $RootWin (purpose: $InSubstPurpose)" >&2
  fi

  EffectiveRootWin="$(kog_resolve_windows_source_root "$BuildRootWin" "$InConfigurePreset")"
  if [[ -n "$EffectiveRootWin" ]]; then
    BuildRootWin="$EffectiveRootWin"
  fi

  local SolutionPath=""
  local ExitCode=0
  SolutionPath="$({ kog_run_windows_ps_helper \
    -Action configure-preset \
    -Root "$BuildRootWin" \
    -Vcvars "$ResolvedVcvars" \
    -Arch "$InVcvarsArch" \
    -ConfigurePreset "$InConfigurePreset"; } 2>&1)" || ExitCode=$?

  kog_cleanup_windows_subst_drive "$SubstDrive" "$SubstCleanupFlag" "$InSubstPurpose"
  if [[ "$ExitCode" -ne 0 ]]; then
    printf '%s\n' "$SolutionPath" >&2
    return "$ExitCode"
  fi

  SolutionPath="$(printf '%s\n' "$SolutionPath" | tr -d '\r' | tail -n 1)"
  printf '%s\n' "$SolutionPath"
}
