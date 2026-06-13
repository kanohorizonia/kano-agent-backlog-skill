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
