#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"

# Conda's shell activation can put an obsolete Python ahead of a newer
# Homebrew/python.org installation. Select a compatible interpreter explicitly.
PYTHON=""
for candidate in /opt/homebrew/bin/python3 /usr/local/bin/python3 \
  /Library/Frameworks/Python.framework/Versions/Current/bin/python3 \
  "$(command -v python3 2>/dev/null || true)"; do
  if [ -x "$candidate" ] &&
     [ "$("$candidate" -c 'import sys; print(int(sys.version_info >= (3, 9)))' 2>/dev/null)" = 1 ]; then
    PYTHON="$candidate"
    break
  fi
done
if [ -z "$PYTHON" ]; then
  echo 'Python 3.9 or newer is required. Install Python 3.11 from python.org, then try again.'
  exit 1
fi

# Replace a stale environment made by an older launcher (for example Python 3.8).
if [ ! -x .venv/bin/python ] ||
   [ "$(.venv/bin/python -c 'import sys; print(int(sys.version_info >= (3, 9)))' 2>/dev/null || true)" != 1 ]; then
  "$PYTHON" -m venv --clear .venv
fi
.venv/bin/python -m pip install --upgrade 'pip<26'
.venv/bin/python -m pip install --only-binary=:all: -r requirements.txt
.venv/bin/python upgrade.py "$@"
