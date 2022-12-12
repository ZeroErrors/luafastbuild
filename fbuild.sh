#!/usr/bin/env sh
cd "$(dirname "$0")"

# Execute with FASTBuild as the working directory so we can use its BFF files without modification
cd "thirdparty/fastbuild/Code"

ROOT_DIR=../../../

# Detect OS and run the correct FBuild executable.
if [[ "$OSTYPE" == "linux"* ]]; then
  # Running on Linux
  $ROOT_DIR/.bin/fastbuild/x86_64-pc-linux/FBuild -config "$ROOT_DIR/fbuild.bff" "$@"
elif [[ "$OSTYPE" == "darwin"* ]]; then
  # Running on macOS
  $ROOT_DIR/.bin/fastbuild/x86_64-apple-darwin/FBuild -config "$ROOT_DIR/fbuild.bff" "$@"
elif [[ "$OSTYPE" == "msys"* || "$OSTYPE" == "cygwin"* ]]; then
  # msys and cygwin are actually running on Windows so we run the Windows build.
  $ROOT_DIR/.bin/fastbuild/x86_64-pc-windows/FBuild.exe -config "$ROOT_DIR/fbuild.bff" "$@"
else
  # eg. "linux-gnu"*, "darwin"*, "cygwin", "msys", "freebsd", etc.
  echo "OSTYPE Not supported!"
  echo "OSTYPE: $OSTYPE"
  exit 1
fi
