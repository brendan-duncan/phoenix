#!/bin/sh
# Builds the web editor and puts it on phoenixanimate.com/webeditor and on
# Phoenix Cloud, stopping at the first step that fails.
#
#   tools/deploy_editor.sh [--skip-tests]
#
# --skip-tests skips the cloud's tests (for changes outside cloud/).

set -e
cd "$(dirname "$0")/.."

PYTHON=python3
command -v "$PYTHON" >/dev/null 2>&1 || PYTHON=python

"$PYTHON" tools/web/build_web.py
"$PYTHON" tools/web/deploy_web.py
"$PYTHON" tools/deploy_cloud.py deploy --editor "$@"

echo
echo "The web editor is built and deployed to the site and the cloud."
