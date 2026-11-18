/**
 * @file ring_buffer.h
 * Fixed-capacity byte FIFO (single context, no locking). N must be a power of two.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

template <size_t N>
class RingBuffer {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "capacity must be a power of two");

 public:
  size_t size() const { return head_ - tail_; }
  size_t space() const { return N - size(); }
  bool empty() const { return head_ == tail_; }
  void clear() { head_ = tail_ = 0; }

  /** Returns false (byte dropped) when full. */
  bool push(uint8_t b) {
    if (space() == 0) return false;
    buf_[head_++ & (N - 1)] = b;
    return true;
  }

  /** Returns -1 when empty. */
  int pop() {
    if (empty()) return -1;
    return buf_[tail_++ & (N - 1)];
  }

 private:
  uint8_t buf_[N] = {};
  size_t head_ = 0;  // free running, wraps naturally
  size_t tail_ = 0;
};

}  // namespace core
