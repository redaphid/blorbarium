#pragma once
// Where the life stats and analytics (life_tally.h, analytics.h) take their
// memory: never the badge's internal RAM, which the duel's radio must find
// free each time fight mode opens (#53). The firmware points bigHeap at
// PSRAM (heap_caps_malloc with MALLOC_CAP_SPIRAM) before it builds the Dish;
// until then, and natively, it is malloc. Every allocation the stats make,
// lasting or for a moment (a save's bytes, a decoded bucket, STATS json),
// goes through here.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace blorb {

struct BigHeap {
  void* (*alloc)(size_t) = nullptr;   // nullptr: malloc
  void (*release)(void*) = nullptr;   // nullptr: free
};
inline BigHeap bigHeap;

inline void* bigAlloc(size_t n) {
  void* p = bigHeap.alloc ? bigHeap.alloc(n) : std::malloc(n);
  if (!p) std::abort();   // as a failed new does on the badge (no exceptions)
  return p;
}
inline void bigFree(void* p) { bigHeap.release ? bigHeap.release(p) : std::free(p); }

template <class T>
struct BigAllocator {
  using value_type = T;
  BigAllocator() = default;
  template <class U> BigAllocator(const BigAllocator<U>&) {}
  T* allocate(size_t n) { return static_cast<T*>(bigAlloc(n * sizeof(T))); }
  void deallocate(T* p, size_t) { bigFree(p); }
  template <class U> bool operator==(const BigAllocator<U>&) const { return true; }
  template <class U> bool operator!=(const BigAllocator<U>&) const { return false; }
};

template <class T> using BigVector = std::vector<T, BigAllocator<T>>;
using BigBytes = BigVector<uint8_t>;
using BigString = std::basic_string<char, std::char_traits<char>, BigAllocator<char>>;

struct BigDelete {
  template <class T> void operator()(T* p) const {
    p->~T();
    bigFree(p);
  }
};
template <class T> using BigPtr = std::unique_ptr<T, BigDelete>;
template <class T, class... A> BigPtr<T> makeBig(A&&... a) {
  void* m = bigAlloc(sizeof(T));
  return BigPtr<T>(new (m) T(std::forward<A>(a)...));
}

}  // namespace blorb
