#!/usr/bin/env bash
# The full #37 eval as a background job (pid and exit code in .jobs/, never pgrep):
#   bash scripts/test/ai_lines/run_eval.sh <run> [extra args for ai_lines_eval.mjs]
#   until [ -f .jobs/ai_eval_<run>.rc ]; do sleep 5; done
cd "$(dirname "$0")/../../.."
run=${1:-r1}; shift || true
mkdir -p .jobs && rm -f ".jobs/ai_eval_$run.rc"
( set +e
  node scripts/test/ai_lines_eval.mjs --run "$run" \
    --models kimi,deepseek,gemma,mistral,llama,gptoss --prompts v1,v2 \
    --judges @cf/deepseek-ai/deepseek-v4-pro-0813,@cf/zai-org/glm-5.3-flash --concurrency 8 "$@" \
    > ".jobs/ai_eval_$run.log" 2>&1
  echo $? > ".jobs/ai_eval_$run.rc" ) &
echo $! > ".jobs/ai_eval_$run.pid"
echo "started ai_eval_$run (pid $(cat ".jobs/ai_eval_$run.pid")), log .jobs/ai_eval_$run.log"
