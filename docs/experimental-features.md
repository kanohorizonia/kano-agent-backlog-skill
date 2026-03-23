# Experimental Features

This document lists features that are experimental and may change or be removed in future releases.

## Feature Stability Levels

### Stable (Core CLI)
These features are stable and ready for production use:
- Backlog initialization (`kob admin init`)
- Item creation and management (`kob item create/list/update-state`)
- ADR creation (`kob adr create`)
- Basic configuration management
- Doctor command for environment validation

### Beta (Validation & Reporting)
These features are functional but may have minor changes:
- Ready gate validation
- State transition enforcement
- Worklog management
- Multi-product support
- View generation

### Experimental (Advanced Features)
These features are under active development and may change significantly:

#### Vector Search
- **Status**: Experimental
- **Commands**: `kob search hybrid`, `kob embedding`
- **Why experimental**: Performance optimization ongoing, API may change
- **Dependencies**: Requires `[vector]` extras: `pip install kano-agent-backlog-skill[vector]`
- **Known limitations**: 
  - Large backlogs may have slow indexing
  - Embedding model selection is limited
  - Query syntax may change

#### Advanced Querying
- **Status**: Experimental
- **Commands**: `kob item list`, complex filters
- **Why experimental**: Query language design in progress
- **Known limitations**:
  - Limited filter operators
  - No query optimization yet
  - Syntax may change

#### Tokenizer Diagnostics
- **Status**: Experimental
- **Commands**: `kob tokenizer adapter-status`
- **Why experimental**: Diagnostic output format evolving
- **Known limitations**:
  - Output format may change
  - Limited tokenizer support

## Using Experimental Features

When using experimental features, you may see runtime warnings:

```python
import logging
logging.warning("Vector search is experimental and may change in future releases")
```

To suppress these warnings (not recommended):
```python
import warnings
warnings.filterwarnings("ignore", category=UserWarning, module="kano_backlog_ops.backlog_vector_index")
```

## Feedback

If you're using experimental features, please provide feedback:
- Open an issue on GitHub
- Share your use case and pain points
- Suggest improvements

Your feedback helps us stabilize these features faster!
