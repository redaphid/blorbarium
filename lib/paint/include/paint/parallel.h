#pragma once
// A second core for the renderer, when the caller lends one: the background
// and the big sprites are drawn in two halves at once, rows apart, so the two
// never write the same pixel and the frame is the same bytes either way.
// Off (both halves on the caller, one after the other) until the caller
// hands it a runner; the board's runs half 0 on core 0 (src/main.cpp).
namespace paint {

using Half = void (*)(void* job, int half);   // half 0 or 1

struct Parallel {
  // Runs half(job, 0) on the other core and half(job, 1) on this one, and
  // returns when both are done. Null: both here.
  void (*run)(Half half, void* job) = nullptr;
};
extern Parallel parallel;

inline void bothHalves(Half half, void* job) {
  if (parallel.run) return parallel.run(half, job);
  half(job, 0);
  half(job, 1);
}

}  // namespace paint
