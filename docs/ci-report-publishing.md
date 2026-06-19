# CI Report Publishing

`agent-skill-cloud-build.yml` is the public GitHub Actions cloud-build pilot used by Jenkins.

The workflow publishes:

- JUnit/Cobertura/HTML evidence staged under `_github-reports`.
- a Markdown GitHub job summary from `_github-reports/summary/github-summary.md`.
- a stable `<artifact-prefix>-reports-<platform>` artifact.
- raw `test-reports.tar.gz` and `coverage-reports.tar.gz` archives when existing tasks produce them.

Python tests are conditional because this repo no longer has a Python package/test project. Native C++ tests are the supported gate. GitHub Code Quality coverage upload is opt-in, warning-only for the first pilot, and skipped for fork pull requests.

When Jenkins delegates to GitHub Actions, Jenkins downloads the cloud artifact and stages `_github-reports` back into its canonical roots:

- `_github-reports/reports/**` -> `out/reports/<platform>/**`
- `_github-reports/coverage/**` -> `out/coverage/<platform>/**`
- `_github-reports/site/**` -> `artifacts/report-sites/<platform>/**`
- `_github-reports/manifest/github-report-manifest.json` -> `artifacts/manifests/<platform>-github-report-manifest.json`

## Public GitHub Pages report slots

Every agent-skill public site should expose the same stable report paths:

- `reports/latest/test-report/`
- `reports/latest/coverage-report/`
- `reports/latest/public-report-slots.json`

Jenkins HTML Publisher URLs and controller-local job paths are internal producer evidence. They must not be the public external link. Before publishing `gh-pages`, stage the publishable static report payload into the site build with:

```bash
KANO_PUBLIC_REPORT_SOURCE_DIR=/path/to/static/report-site \
  bash src/shell/docs/build-and-deploy.sh --prep-deploy
```

For one-off backfills from an authenticated Jenkins HTML Publisher URL, use the rescue path without committing credentials:

```bash
KANO_PUBLIC_REPORT_SOURCE_URL=https://jenkins.example/job/.../Reports/ \
KANO_PUBLIC_REPORT_SOURCE_CURL_USER="$JENKINS_USER" \
KANO_PUBLIC_REPORT_SOURCE_CURL_PASSWORD="$JENKINS_TOKEN" \
  bash src/shell/docs/build-and-deploy.sh --prep-deploy
```

The staging script copies the report landing page into `reports/latest/test-report/`, copies coverage HTML into `reports/latest/coverage-report/` when present, and writes `public-report-slots.json`. If no publishable report is staged, deployment writes explicit placeholders and the release gate remains blocked.

CTest-based agent-skill lanes should keep enough JUnit stdout for BDD review by honoring:

- `KANO_CTEST_OUTPUT_SIZE_PASSED` (default `262144`)
- `KANO_CTEST_OUTPUT_SIZE_FAILED` (default `1048576`)

The public report should preserve scenario evidence in the report payload. Jenkins `testReport` pages are not sufficient because they may show only the JUnit `system-out` text already stored in XML.
