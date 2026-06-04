from typer.testing import CliRunner

from kano_backlog_cli.cli import app


runner = CliRunner()


def test_export_accepts_jenkins_compatibility_flags():
    result = runner.invoke(
        app,
        ["export", "--single", "--no-validate-release-archive"],
    )

    assert result.exit_code == 0, result.output
    assert "export:" in result.output
    assert "single=True" in result.output
    assert "validate-release-archive=False" in result.output
    assert "no-op" in result.output
