#!/bin/bash

# Common configuration path resolution for all scripts
# Usage: source this file and call find_config_file <config_filename>

find_config_file() {
  local config_name="$1"
  local script_dir="$2"
  local repo_root="$3"
  
  # Priority order:
  # 1. REPO_ROOT/scripts/docs/config/ (local execution)
  # 2. REPO_ROOT/_ws/src/demo/scripts/docs/config/ (CI execution)
  # 3. SCRIPT_DIR/config/ (fallback)
  
  if [ -f "$repo_root/scripts/docs/config/$config_name" ]; then
    echo "$repo_root/scripts/docs/config/$config_name"
  elif [ -f "$repo_root/_ws/src/demo/scripts/docs/config/$config_name" ]; then
    echo "$repo_root/_ws/src/demo/scripts/docs/config/$config_name"
  elif [ -f "$script_dir/config/$config_name" ]; then
    echo "$script_dir/config/$config_name"
  else
    echo ""
  fi
}

validate_config_file() {
  local config_path="$1"
  local config_name="$2"
  
  if [ -z "$config_path" ] || [ ! -f "$config_path" ]; then
    echo "ERROR: $config_name not found in any expected location"
    echo "Searched:"
    echo "  - \$REPO_ROOT/scripts/docs/config/$config_name"
    echo "  - \$REPO_ROOT/_ws/src/demo/scripts/docs/config/$config_name"
    echo "  - \$SCRIPT_DIR/config/$config_name"
    return 1
  fi
  return 0
}