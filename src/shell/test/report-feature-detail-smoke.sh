#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
CPP_ROOT="$REPO_ROOT/src/cpp"
INFRA_LIB_DIR="$CPP_ROOT/shared/infra/scripts/lib"

export KANO_CPP_INFRA_CPP_ROOT="${KANO_CPP_INFRA_CPP_ROOT:-$CPP_ROOT}"
export KANO_CPP_INFRA_REPO_ROOT="${KANO_CPP_INFRA_REPO_ROOT:-$REPO_ROOT}"

# shellcheck disable=SC1091
. "$INFRA_LIB_DIR/native_tool.sh"

SMOKE_ROOT="${KANO_REPORT_FEATURE_DETAIL_SMOKE_ROOT:-$CPP_ROOT/.kano/tmp/report-feature-detail-smoke}"
JUNIT_XML="$SMOKE_ROOT/tests.xml"
BDD_DIR="$SMOKE_ROOT/bdd-metadata"
REPORT_DIR="$SMOKE_ROOT/report"
MANIFEST="$REPO_ROOT/src/cpp/shared/infra/config/bdd-feature-manifest.kano-agent-backlog-skill.json"

rm -rf -- "$SMOKE_ROOT"
mkdir -p "$BDD_DIR" "$REPORT_DIR"

cat > "$JUNIT_XML" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<testsuites>
  <testsuite name="kano-agent-backlog-skill.report-feature-detail-smoke" tests="3" failures="1" errors="0" skipped="1" time="0.060">
    <testcase name="[bdd][featured][feature:cli-workflow][scenario:KOB-BDD-CLI-QUICK-001] KOB CLI quick smoke covers bounded core commands" time="0.010" />
    <testcase name="[bdd][feature:backlog-core][scenario:KOB-BDD-CORE-001] Backlog core loads config, state, and frontmatter contracts" time="0.020"><skipped /></testcase>
    <testcase name="legacy smoke without bdd tags" time="0.030"><failure message="fixture failure">intentional fixture failure</failure></testcase>
  </testsuite>
</testsuites>
XML

kano_cpp_infra_tool generate-bdd-metadata "$JUNIT_XML" "$BDD_DIR" "feature_detail_smoke"
kano_cpp_infra_tool render-junit-report "$JUNIT_XML" "$REPORT_DIR" "Feature Detail Smoke" "$BDD_DIR" "$MANIFEST"

test -f "$REPORT_DIR/index.html"
test -f "$REPORT_DIR/feature-details.json"
test -f "$REPORT_DIR/features/cli-workflow/index.html"

grep -F "Feature Scenario Details" "$REPORT_DIR/index.html" >/dev/null
grep -F "features/cli-workflow/index.html" "$REPORT_DIR/index.html" >/dev/null
grep -F "Uncategorized" "$REPORT_DIR/index.html" >/dev/null
grep -F "KOB-BDD-CLI-QUICK-001" "$REPORT_DIR/features/cli-workflow/index.html" >/dev/null
grep -F "Validates command-line backlog workflows" "$REPORT_DIR/features/cli-workflow/index.html" >/dev/null
grep -F "feature_detail_smoke" "$REPORT_DIR/features/cli-workflow/index.html" >/dev/null
grep -F "legacy smoke without bdd tags" "$REPORT_DIR/feature-details.json" >/dev/null

if grep -F "$SMOKE_ROOT" "$REPORT_DIR/feature-details.json" >/dev/null; then
  echo "feature detail JSON exposed the smoke workspace path" >&2
  exit 1
fi

echo "report_feature_detail_smoke: PASS"
echo "report: $REPORT_DIR/index.html"
