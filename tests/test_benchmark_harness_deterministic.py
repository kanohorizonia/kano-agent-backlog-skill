from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKILL_SRC = ROOT / "src"
sys.path.insert(0, str(SKILL_SRC))

from kano_backlog_ops.benchmark_embeddings import (  # noqa: E402
    BenchmarkHarnessOptions,
    run_benchmark,
)
from conftest import write_project_backlog_config  # noqa: E402


def test_benchmark_report_is_deterministic(tmp_path: Path) -> None:
    write_project_backlog_config(
        tmp_path,
        products={"kano-agent-backlog-skill": ("kano-agent-backlog-skill", "KABSD")},
    )
    corpus = ROOT / "tests" / "fixtures" / "benchmark_corpus.json"
    queries = ROOT / "tests" / "fixtures" / "benchmark_queries.json"

    out1 = tmp_path / "run1"
    out2 = tmp_path / "run2"

    opts1 = BenchmarkHarnessOptions(include_embedding=False, include_vector=False, output_dir=out1)
    opts2 = BenchmarkHarnessOptions(include_embedding=False, include_vector=False, output_dir=out2)

    cwd = Path.cwd()
    try:
        os.chdir(tmp_path)
        _report1, paths1 = run_benchmark(
            product="kano-agent-backlog-skill",
            agent="opencode",
            corpus_path=corpus,
            queries_path=queries,
            options=opts1,
        )
        _report2, paths2 = run_benchmark(
            product="kano-agent-backlog-skill",
            agent="opencode",
            corpus_path=corpus,
            queries_path=queries,
            options=opts2,
        )
    finally:
        os.chdir(cwd)

    b1 = paths1.report_json.read_bytes()
    b2 = paths2.report_json.read_bytes()
    assert b1 == b2

    m1 = paths1.report_md.read_bytes()
    m2 = paths2.report_md.read_bytes()
    assert m1 == m2
