#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

python3 "${ROOT_DIR}/tools/run_current_feature_validation.py" "$@"
