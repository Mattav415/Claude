#!/bin/bash
set -euo pipefail

# Only run in remote (Claude Code on the web) environments
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

# Add dependency install commands below as the project grows.
# Examples:
#   npm install
#   pip install -r requirements.txt
#   cargo build
