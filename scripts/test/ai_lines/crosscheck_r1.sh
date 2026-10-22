#!/usr/bin/env bash
# A second judge on the two leading models (deepseek-v4-pro, thinking off; minutes
# a call, so only these), then the summary over every model with both judges.
# Background job: .jobs/crosscheck_r1.{pid,log,rc}.
cd "$(dirname "$0")/../../.."
mkdir -p .jobs && rm -f .jobs/crosscheck_r1.rc
( set +e
  { node scripts/test/ai_lines_eval.mjs --run r1 --models kimi,gemma --prompts v2,v3 --no-generate \
      --judges @cf/deepseek-ai/deepseek-v4-pro-0813 --concurrency 12 &&
    node scripts/test/ai_lines_eval.mjs --run r1 --models kimi,deepseek,gemma,mistral,llama,gptoss --prompts v1,v2,v3 \
      --no-generate --no-judge --judges @cf/qwen/qwen3.8-27b,@cf/deepseek-ai/deepseek-v4-pro-0813; } > .jobs/crosscheck_r1.log 2>&1
  echo $? > .jobs/crosscheck_r1.rc ) &
echo $! > .jobs/crosscheck_r1.pid
echo "started crosscheck_r1 (pid $(cat .jobs/crosscheck_r1.pid))"
