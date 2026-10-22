#!/usr/bin/env bash
# Finishes run r1 after the network outage, as a background job (pid and exit
# code in .jobs/finish_r1.*): the v3 packs for the two leading models, then one
# judging and summary pass over everything on disk. The judge is qwen3.8-27b
# with thinking off (not a candidate, and seconds a call; deepseek-v4-pro took
# minutes a call under load).
cd "$(dirname "$0")/../../.."
mkdir -p .jobs && rm -f .jobs/finish_r1.rc
( set +e
  { node scripts/test/ai_lines_eval.mjs --run r1 --models kimi,gemma --prompts v3 --no-judge --concurrency 6 &&
    node scripts/test/ai_lines_eval.mjs --run r1 --models kimi,deepseek,gemma,mistral,llama,gptoss --prompts v1,v2,v3 \
      --no-generate --judges @cf/qwen/qwen3.8-27b --concurrency 10; } > .jobs/finish_r1.log 2>&1
  echo $? > .jobs/finish_r1.rc ) &
echo $! > .jobs/finish_r1.pid
echo "started finish_r1 (pid $(cat .jobs/finish_r1.pid))"
