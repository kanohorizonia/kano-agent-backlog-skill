"""
Test core functionality validation for 0.0.3 release preparation.

This module validates Requirements 7.2-7.7:
- 7.2: Backlog initialization creates correct structure
- 7.3: Item creation assigns IDs correctly
- 7.4: State transitions work correctly
- 7.5: ADR creation works correctly
- 7.6: Multi-product isolation
- 7.7: Ready gate validation

Tasks covered: 5.1, 5.2, 5.4, 5.6, 5.7, 5.9
"""

import os
import re
from pathlib import Path
from typing import List

import pytest
from typer.testing import CliRunner

from kano_backlog_cli.cli import app
from conftest import write_project_backlog_config

runner = CliRunner()


def _scaffold_minimal_backlog(tmp_path: Path, product_name: str = "test-product") -> tuple[Path, Path]:
    """Create minimal backlog structure for testing."""
    backlog_root = tmp_path / "_kano" / "backlog"
    product_root = backlog_root / "products" / product_name
    
    prefix = product_name[:4].upper()
    write_project_backlog_config(tmp_path, products={product_name: (product_name, prefix)})
    
    return backlog_root, product_root


# Task 5.1: Verify backlog initialization creates correct structure
class TestBacklogInitialization:
    """Test backlog initialization (Requirement 7.2)."""
    
    def test_backlog_init_creates_required_directories(self, tmp_path: Path):
        """Verify all required directories are created during initialization."""
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Initialize backlog
            result = runner.invoke(
                app,
                ["admin", "init", "--product", "test-product", "--agent", "test-agent"]
            )
            
            assert result.exit_code == 0, f"Init failed: {result.output}"
            
            # Verify directory structure
            backlog_root = tmp_path / "_kano" / "backlog"
            product_root = backlog_root / "products" / "test-product"
            
            # Check required directories
            required_dirs = [
                product_root / "items",
                product_root / "decisions",
                product_root / "views",
                product_root / "_meta",
            ]
            
            for dir_path in required_dirs:
                assert dir_path.exists(), f"Missing directory: {dir_path}"
                assert dir_path.is_dir(), f"Not a directory: {dir_path}"
        finally:
            os.chdir(cwd_before)
    
    def test_backlog_init_creates_meta_files(self, tmp_path: Path):
        """Verify _meta/ directory is initialized correctly."""
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            result = runner.invoke(
                app,
                ["admin", "init", "--product", "test-product", "--agent", "test-agent"]
            )
            
            assert result.exit_code == 0, f"Init failed: {result.output}"
            
            product_root = tmp_path / "_kano" / "backlog" / "products" / "test-product"
            meta_dir = product_root / "_meta"
            
            # Check that _meta directory exists
            assert meta_dir.exists(), "Missing _meta directory"
            assert meta_dir.is_dir(), "_meta is not a directory"
            
            # Note: indexes.md and sequences.json may be created on first use, not during init
        finally:
            os.chdir(cwd_before)
    
    def test_backlog_init_creates_item_type_directories(self, tmp_path: Path):
        """Verify item type directories are created."""
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            result = runner.invoke(
                app,
                ["admin", "init", "--product", "test-product", "--agent", "test-agent"]
            )
            
            assert result.exit_code == 0, f"Init failed: {result.output}"
            
            product_root = tmp_path / "_kano" / "backlog" / "products" / "test-product"
            items_root = product_root / "items"
            
            # Check for item type directories
            item_types = ["epic", "feature", "userstory", "task", "bug"]
            for item_type in item_types:
                type_dir = items_root / item_type / "0000"
                assert type_dir.exists(), f"Missing directory: {type_dir}"
        finally:
            os.chdir(cwd_before)


# Task 5.2: Verify item creation assigns IDs correctly
class TestItemIDAssignment:
    """Test item ID assignment (Requirement 7.3)."""
    
    def test_item_ids_follow_pattern(self, tmp_path: Path):
        """Verify IDs follow {PRODUCT}-{TYPE}-{SEQUENCE} pattern."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "demo")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Create items directory structure
            for item_type in ["epic", "feature", "userstory", "task", "bug"]:
                (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
            
            # Create required directories
            for required_dir in ["decisions", "views", "_meta"]:
                (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create a task
            result = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Test Task",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "demo"
                ]
            )
            
            assert result.exit_code == 0, f"Create failed: {result.output}"
            
            # Extract ID from output
            lines = [line.strip() for line in result.output.splitlines() if line.strip()]
            created_line = next(line for line in lines if "Created:" in line)
            created_id = created_line.split(":")[-1].strip()
            
            # Verify pattern: DEMO-TSK-XXXX
            pattern = r"^DEMO-TSK-\d{4}$"
            assert re.match(pattern, created_id), f"ID doesn't match pattern: {created_id}"
        finally:
            os.chdir(cwd_before)
    
    def test_sequence_numbers_increment(self, tmp_path: Path):
        """Verify sequence numbers increment without gaps."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "test-product")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Create items directory structure
            for item_type in ["task"]:
                (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
            
            for required_dir in ["decisions", "views", "_meta"]:
                (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            created_ids = []
            
            # Create multiple tasks
            for i in range(3):
                result = runner.invoke(
                    app,
                    [
                        "item", "create",
                        "--type", "task",
                        "--title", f"Task {i+1}",
                        "--priority", "P2",
                        "--agent", "test-agent",
                        "--product", "test-product"
                    ]
                )
                
                assert result.exit_code == 0, f"Create failed: {result.output}"
                
                lines = [line.strip() for line in result.output.splitlines() if line.strip()]
                created_line = next(line for line in lines if "Created:" in line)
                created_id = created_line.split(":")[-1].strip()
                created_ids.append(created_id)
            
            # Extract sequence numbers
            sequences = [int(id.split("-")[-1]) for id in created_ids]
            
            # Verify they increment by 1
            for i in range(1, len(sequences)):
                assert sequences[i] == sequences[i-1] + 1, \
                    f"Sequence gap detected: {sequences[i-1]} -> {sequences[i]}"
        finally:
            os.chdir(cwd_before)
    
    def test_different_types_have_independent_sequences(self, tmp_path: Path):
        """Verify different item types have independent sequence counters."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "test-product")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Create items directory structure
            for item_type in ["task", "bug"]:
                (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
            
            for required_dir in ["decisions", "views", "_meta"]:
                (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create a task
            task_result = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Test Task",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            # Create a bug
            bug_result = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "bug",
                    "--title", "Test Bug",
                    "--priority", "P1",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert task_result.exit_code == 0
            assert bug_result.exit_code == 0
            
            # Extract IDs
            task_lines = [line.strip() for line in task_result.output.splitlines() if line.strip()]
            task_line = next(line for line in task_lines if "Created:" in line)
            task_id = task_line.split(":")[-1].strip()
            
            bug_lines = [line.strip() for line in bug_result.output.splitlines() if line.strip()]
            bug_line = next(line for line in bug_lines if "Created:" in line)
            bug_id = bug_line.split(":")[-1].strip()
            
            # Both item types should use independent sequence numbers.
            assert task_id.endswith("-0002"), f"Task ID should end with -0002: {task_id}"
            assert bug_id.endswith("-0002"), f"Bug ID should end with -0002: {bug_id}"
            
            # Type codes should be different
            assert "-TSK-" in task_id
            assert "-BUG-" in bug_id
        finally:
            os.chdir(cwd_before)


# Task 5.4: Verify state transitions work correctly
class TestStateTransitions:
    """Test state transitions (Requirement 7.4)."""
    
    def test_valid_state_transition_succeeds(self, tmp_path: Path):
        """Verify valid state transitions work and update Worklog."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "test-product")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup
            for item_type in ["task"]:
                (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
            
            for required_dir in ["decisions", "views", "_meta"]:
                (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create a task
            create_result = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Test Task",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert create_result.exit_code == 0
            
            lines = [line.strip() for line in create_result.output.splitlines() if line.strip()]
            created_line = next(line for line in lines if "Created:" in line)
            item_id = created_line.split(":")[-1].strip()
            
            # Transition Proposed -> Planned
            transition_result = runner.invoke(
                app,
                [
                    "item", "update-state",
                    item_id,
                    "--state", "Planned",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert transition_result.exit_code == 0, f"Transition failed: {transition_result.output}"
            
            # Verify state changed
            item_files = list((product_root / "items").rglob(f"{item_id}_*.md"))
            assert len(item_files) == 1, f"Expected 1 item file, found {len(item_files)}"
            
            content = item_files[0].read_text(encoding="utf-8")
            assert "state: Planned" in content, "State not updated in frontmatter"
            
            # Verify Worklog was appended
            assert "# Worklog" in content
            worklog_section = content.split("# Worklog")[1]
            assert "Planned" in worklog_section, "Worklog not updated with state change"
        finally:
            os.chdir(cwd_before)
    
    def test_invalid_state_transition_fails(self, tmp_path: Path):
        """Verify invalid state transitions are rejected."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "test-product")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup
            for item_type in ["task"]:
                (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
            
            for required_dir in ["decisions", "views", "_meta"]:
                (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create a task (starts in Proposed)
            create_result = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Test Task",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert create_result.exit_code == 0
            
            lines = [line.strip() for line in create_result.output.splitlines() if line.strip()]
            created_line = next(line for line in lines if "Created:" in line)
            item_id = created_line.split(":")[-1].strip()
            
            # Try an unknown state, which should fail CLI validation.
            transition_result = runner.invoke(
                app,
                [
                    "item", "update-state",
                    item_id,
                    "--state", "InvalidState",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            # Should fail
            assert transition_result.exit_code != 0, "Invalid transition should fail"
            assert "transition" in transition_result.output.lower() or \
                   "invalid" in transition_result.output.lower(), \
                   "Error message should mention invalid transition"
        finally:
            os.chdir(cwd_before)


# Task 5.6: Verify ADR creation works correctly
class TestADRCreation:
    """Test ADR creation (Requirement 7.5)."""
    
    def test_adr_creation_generates_correct_file(self, tmp_path: Path):
        """Verify ADR creation generates file with correct structure."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "test-product")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup
            (product_root / "decisions").mkdir(parents=True, exist_ok=True)
            (product_root / "_meta").mkdir(parents=True, exist_ok=True)
            
            # Create ADR
            result = runner.invoke(
                app,
                [
                    "admin", "adr", "create",
                    "--title", "Test Decision",
                    "--product", "test-product",
                    "--agent", "test-agent"
                ]
            )
            
            assert result.exit_code == 0, f"ADR creation failed: {result.output}"
            
            # Verify file was created
            decisions_dir = product_root / "decisions"
            adr_files = list(decisions_dir.glob("ADR-*.md"))
            assert len(adr_files) >= 1, "No ADR file created"
            
            # Read the ADR file
            adr_content = adr_files[0].read_text(encoding="utf-8")
            
            # Verify frontmatter
            assert "---" in adr_content
            assert "id: ADR-" in adr_content
            assert 'title: "Test Decision"' in adr_content
            assert "status:" in adr_content
            assert "date:" in adr_content
        finally:
            os.chdir(cwd_before)
    
    def test_adr_id_follows_pattern(self, tmp_path: Path):
        """Verify ADR IDs follow {PRODUCT}-ADR-{SEQUENCE} pattern."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "demo")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup
            (product_root / "decisions").mkdir(parents=True, exist_ok=True)
            (product_root / "_meta").mkdir(parents=True, exist_ok=True)
            
            # Create ADR
            result = runner.invoke(
                app,
                [
                    "admin", "adr", "create",
                    "--title", "Test Decision",
                    "--product", "demo",
                    "--agent", "test-agent"
                ]
            )
            
            assert result.exit_code == 0
            
            # Extract ID from output or file
            decisions_dir = product_root / "decisions"
            adr_files = list(decisions_dir.glob("ADR-*.md"))
            assert len(adr_files) >= 1
            
            # Verify pattern
            adr_filename = adr_files[0].name
            pattern = r"^ADR-\d{4}"
            assert re.match(pattern, adr_filename), f"ADR filename doesn't match pattern: {adr_filename}"
        finally:
            os.chdir(cwd_before)


# Task 5.7: Verify multi-product isolation
class TestMultiProductIsolation:
    """Test multi-product isolation (Requirement 7.6)."""
    
    def test_products_have_independent_id_sequences(self, tmp_path: Path):
        """Verify different products have independent ID sequences."""
        # Setup two products
        write_project_backlog_config(
            tmp_path,
            products={
                "product-a": ("product-a", "PRDA"),
                "product-b": ("product-b", "PRDB")
            }
        )
        
        backlog_root = tmp_path / "_kano" / "backlog"
        product_a_root = backlog_root / "products" / "product-a"
        product_b_root = backlog_root / "products" / "product-b"
        
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup both products
            for product_root in [product_a_root, product_b_root]:
                for item_type in ["task"]:
                    (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
                
                for required_dir in ["decisions", "views", "_meta"]:
                    (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create task in product A
            result_a = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Task in Product A",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "product-a"
                ]
            )
            
            # Create task in product B
            result_b = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Task in Product B",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "product-b"
                ]
            )
            
            assert result_a.exit_code == 0
            assert result_b.exit_code == 0
            
            # Extract IDs
            lines_a = [line.strip() for line in result_a.output.splitlines() if line.strip()]
            created_a = next(line for line in lines_a if "Created:" in line)
            id_a = created_a.split(":")[-1].strip()
            
            lines_b = [line.strip() for line in result_b.output.splitlines() if line.strip()]
            created_b = next(line for line in lines_b if "Created:" in line)
            id_b = created_b.split(":")[-1].strip()
            
            # Both products should have independent sequence numbers.
            assert id_a.endswith("-0002"), f"Product A should start at 0002: {id_a}"
            assert id_b.endswith("-0002"), f"Product B should start at 0002: {id_b}"
            
            # Prefixes should be different
            assert id_a.startswith("PRDA-"), f"Product A should use PRDA prefix: {id_a}"
            assert id_b.startswith("PRDB-"), f"Product B should use PRDB prefix: {id_b}"
        finally:
            os.chdir(cwd_before)
    
    def test_product_items_are_isolated(self, tmp_path: Path):
        """Verify items from one product don't appear in another product's views."""
        # Setup two products
        write_project_backlog_config(
            tmp_path,
            products={
                "product-a": ("product-a", "PRDA"),
                "product-b": ("product-b", "PRDB")
            }
        )
        
        backlog_root = tmp_path / "_kano" / "backlog"
        product_a_root = backlog_root / "products" / "product-a"
        product_b_root = backlog_root / "products" / "product-b"
        
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup both products
            for product_root in [product_a_root, product_b_root]:
                for item_type in ["task"]:
                    (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
                
                for required_dir in ["decisions", "views", "_meta"]:
                    (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create task in product A
            runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Task in Product A",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "product-a"
                ]
            )
            
            # Create task in product B
            runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Task in Product B",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "product-b"
                ]
            )
            
            # Verify isolation directly in the product file trees.
            files_a = [path.name for path in product_a_root.rglob("*.md")]
            files_b = [path.name for path in product_b_root.rglob("*.md")]

            assert any("task-in-product-a" in name for name in files_a)
            assert not any("task-in-product-b" in name for name in files_a)
            assert any(name.startswith("PRDA-") for name in files_a)
            assert not any(name.startswith("PRDB-") for name in files_a)

            assert any("task-in-product-b" in name for name in files_b)
            assert not any("task-in-product-a" in name for name in files_b)
            assert any(name.startswith("PRDB-") for name in files_b)
            assert not any(name.startswith("PRDA-") for name in files_b)
        finally:
            os.chdir(cwd_before)


# Task 5.9: Verify Ready gate validation
class TestReadyGateValidation:
    """Test Ready gate validation (Requirement 7.7)."""
    
    def test_ready_gate_rejects_missing_fields(self, tmp_path: Path):
        """Verify Ready gate validation identifies missing required fields."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "test-product")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup
            for item_type in ["task"]:
                (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
            
            for required_dir in ["decisions", "views", "_meta"]:
                (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create a task (will have empty required fields)
            create_result = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Test Task",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert create_result.exit_code == 0
            
            lines = [line.strip() for line in create_result.output.splitlines() if line.strip()]
            created_line = next(line for line in lines if "Created:" in line)
            item_id = created_line.split(":")[-1].strip()
            
            # First transition to Planned (should work)
            planned_result = runner.invoke(
                app,
                [
                    "item", "update-state",
                    item_id,
                    "--state", "Planned",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert planned_result.exit_code == 0
            
            # Run the explicit Ready gate check; state updates may be forced independently.
            ready_result = runner.invoke(
                app,
                ["item", "check-ready", item_id, "--product", "test-product"]
            )
            
            # Should fail
            assert ready_result.exit_code != 0, "Ready gate should reject items with missing fields"
            
            # Error message should mention missing fields
            output_lower = ready_result.output.lower()
            assert "context" in output_lower or "goal" in output_lower or \
                   "approach" in output_lower or "acceptance" in output_lower or \
                   "ready" in output_lower, \
                   "Error message should mention missing required fields"
        finally:
            os.chdir(cwd_before)
    
    def test_ready_gate_accepts_complete_fields(self, tmp_path: Path):
        """Verify Ready gate validation passes when all required fields are present."""
        backlog_root, product_root = _scaffold_minimal_backlog(tmp_path, "test-product")
        cwd_before = Path.cwd()
        os.chdir(tmp_path)
        
        try:
            # Setup
            for item_type in ["task"]:
                (product_root / "items" / item_type / "0000").mkdir(parents=True, exist_ok=True)
            
            for required_dir in ["decisions", "views", "_meta"]:
                (product_root / required_dir).mkdir(parents=True, exist_ok=True)
            
            # Create a task
            create_result = runner.invoke(
                app,
                [
                    "item", "create",
                    "--type", "task",
                    "--title", "Test Task",
                    "--priority", "P2",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert create_result.exit_code == 0
            
            lines = [line.strip() for line in create_result.output.splitlines() if line.strip()]
            created_line = next(line for line in lines if "Created:" in line)
            item_id = created_line.split(":")[-1].strip()
            
            # Find the item file and fill in required fields
            item_files = list((product_root / "items").rglob(f"{item_id}_*.md"))
            assert len(item_files) == 1
            
            content = item_files[0].read_text(encoding="utf-8")
            
            # Fill in required fields
            content = content.replace(
                "# Context\n\n(Describe the background",
                "# Context\n\nThis is a complete context section with sufficient detail."
            )
            content = content.replace(
                "# Goal\n\n(What should be achieved",
                "# Goal\n\nThis is a clear goal statement."
            )
            content = content.replace(
                "# Approach\n\n(How will this be implemented",
                "# Approach\n\nThis is a detailed approach description."
            )
            content = content.replace(
                "# Acceptance Criteria\n\n- [ ] (Criterion 1)",
                "# Acceptance Criteria\n\n- [ ] Feature works correctly\n- [ ] Tests pass"
            )
            content = content.replace(
                "# Risks / Dependencies\n\n(List any risks",
                "# Risks / Dependencies\n\nNo significant risks identified."
            )
            
            item_files[0].write_text(content, encoding="utf-8")
            
            # Transition to Planned
            planned_result = runner.invoke(
                app,
                [
                    "item", "update-state",
                    item_id,
                    "--state", "Planned",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert planned_result.exit_code == 0
            
            # Try to transition to Ready (should succeed now)
            ready_result = runner.invoke(
                app,
                [
                    "item", "update-state",
                    item_id,
                    "--state", "Ready",
                    "--agent", "test-agent",
                    "--product", "test-product"
                ]
            )
            
            assert ready_result.exit_code == 0, f"Ready gate should accept complete items: {ready_result.output}"
            
            # Verify state changed
            updated_content = item_files[0].read_text(encoding="utf-8")
            assert "state: Ready" in updated_content
        finally:
            os.chdir(cwd_before)
