#!/usr/bin/env bash
# See ci_annotate.py. Usage: ci-annotate.sh LEVEL TITLE LOGFILE|- [PATTERN]
py=$(command -v python3 || command -v python)
exec "$py" "$(dirname "$0")/ci_annotate.py" "$@"
