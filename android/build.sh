#!/usr/bin/env bash
# Build the relay app on this machine: the JDK and SDK under ~/android (see README.md).
#   ./build.sh                 # unit tests, then the debug APK
#   ./build.sh test            # unit tests only
#   ./build.sh assembleDebug   # any gradle tasks
set -euo pipefail
cd "$(dirname "$0")"
export JAVA_HOME="${JAVA_HOME:-$HOME/android/jdk-21}"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/android}"
[ -f local.properties ] || printf 'sdk.dir=%s\n' "$ANDROID_HOME" > local.properties
if [ $# -eq 0 ]; then set -- testDebugUnitTest assembleDebug; fi
if [ "$1" = test ]; then set -- testDebugUnitTest; fi
exec ./gradlew --console=plain "$@"
