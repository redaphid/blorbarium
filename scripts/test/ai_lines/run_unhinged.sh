#!/usr/bin/env bash
# Run r2 as a background job (.jobs/unhinged_r2.{pid,log,rc}): every chat model in
# the Workers AI catalog writes packs with prompt v4 (unhinged but coherent, wild
# sampling) for the ten drifted fixture pets (one per species), then the judges
# score unhinged and makes-sense, and the ranking and sheet are written.
cd "$(dirname "$0")/../../.."
MODELS=@cf/moonshotai/kimi-k2.6,@cf/moonshotai/kimi-k2.7-code,@cf/zai-org/glm-5.3,@cf/zai-org/glm-5.2,@cf/zai-org/glm-5.3-flash,\
@cf/zai-org/glm-4.7-flash,@cf/deepseek-ai/deepseek-v4-pro-0813,@cf/deepseek-ai/deepseek-v4.1-flash-1017,\
@cf/deepseek-ai/deepseek-v4-flash-0731,@cf/qwen/qwen3.8-27b,@cf/qwen/qwen3-30b-a3b-fp8,@cf/qwen/qwq-32b,\
@cf/google/gemma-4-26b-a4b-it,@cf/aisingapore/gemma-sea-lion-v4-27b-it,@cf/mistralai/mistral-small-3.1-24b-instruct,\
@cf/meta/llama-4-scout-17b-16e-instruct,@cf/meta/llama-3.3-70b-instruct-fp8-fast,@cf/openai/gpt-oss-120b,\
@cf/openai/gpt-oss-20b,@cf/nvidia/nemotron-3-120b-a12b
PETS=grungo-b,mossby-b,puffkin-b,gilly-b,mothwick-b,mudge-b,fizz-b,hushwing-b,tinker-b,octopus-b
mkdir -p .jobs && rm -f .jobs/unhinged_r2.rc
( set +e
  # Up to three passes: a pass reuses the packs on disk, so a network outage
  # mid-run costs only the packs it broke (their .error.json files are cleared first).
  { for pass in 1 2 3; do
      rm -f outputs/ai_lines_eval/r2/packs/*.error.json
      node scripts/test/ai_lines_eval.mjs --run r2 --models "$MODELS" --prompts v4 --pets "$PETS" --no-judge --concurrency 16
      ls outputs/ai_lines_eval/r2/packs/*.error.json >/dev/null 2>&1 || break
    done
    node scripts/test/ai_lines/unhinged.mjs --run r2 --judges @cf/qwen/qwen3.8-27b,@cf/openai/gpt-oss-120b "$@"; } > .jobs/unhinged_r2.log 2>&1
  echo $? > .jobs/unhinged_r2.rc ) &
echo $! > .jobs/unhinged_r2.pid
echo "started unhinged_r2 (pid $(cat .jobs/unhinged_r2.pid))"
