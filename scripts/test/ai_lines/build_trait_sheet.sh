#!/usr/bin/env bash
# Builds tools/trait_sheet.cpp (the engine's own genome decoding, #37) into
# .jobs/trait_sheet, under the same lock as every pio build on this laptop,
# then writes its two outputs: the species baselines the Worker describes
# against, and the eval's fixture genomes.
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p .jobs web/test/fixtures
srcs=(lib/blorb/src/*.cpp)
flock /tmp/blorb-pio.lock g++ -std=c++17 -O1 -Ilib/blorb/include -o .jobs/trait_sheet tools/trait_sheet.cpp "${srcs[@]}"
.jobs/trait_sheet baselines > web/src/baselines.json
.jobs/trait_sheet fixtures > web/test/fixtures/genomes.json
echo "wrote web/src/baselines.json and web/test/fixtures/genomes.json"
