#include "bounded_receive.hpp"

#include <algorithm>

namespace {

bool budget_open(uint64_t started_ms, uint64_t budget_ms, uint64_t now_ms) {
    return now_ms >= started_ms && now_ms - started_ms < budget_ms;
}

}  // namespace

BoundedReceiveResult receive_exact_bounded(
    size_t total_size,
    uint64_t budget_ms,
    int timeout_result,
    char* chunk_buffer,
    size_t chunk_capacity,
    void* receive_context,
    ReceiveChunk receive_chunk,
    void* consume_context,
    ConsumeChunk consume_chunk,
    void* clock_context,
    MonotonicMilliseconds monotonic_ms) {
    if (total_size == 0 || budget_ms == 0 || !chunk_buffer || chunk_capacity == 0 ||
        !receive_chunk || !consume_chunk || !monotonic_ms || timeout_result >= 0) {
        return BoundedReceiveResult::INVALID_ARGUMENT;
    }

    const uint64_t started_ms = monotonic_ms(clock_context);
    size_t received_total = 0;
    while (received_total < total_size) {
        if (!budget_open(started_ms, budget_ms, monotonic_ms(clock_context))) {
            return BoundedReceiveResult::DEADLINE_EXCEEDED;
        }
        const size_t requested = std::min(chunk_capacity, total_size - received_total);
        const int received = receive_chunk(receive_context, chunk_buffer, requested);
        const uint64_t completed_ms = monotonic_ms(clock_context);
        if (received == timeout_result) {
            if (!budget_open(started_ms, budget_ms, completed_ms)) {
                return BoundedReceiveResult::DEADLINE_EXCEEDED;
            }
            continue;
        }
        if (received <= 0 || static_cast<size_t>(received) > requested) {
            return BoundedReceiveResult::RECEIVE_ERROR;
        }
        // Bytes that complete after the absolute deadline are never applied to a command or OTA image.
        if (!budget_open(started_ms, budget_ms, completed_ms)) {
            return BoundedReceiveResult::DEADLINE_EXCEEDED;
        }
        if (!consume_chunk(consume_context, chunk_buffer, static_cast<size_t>(received))) {
            return BoundedReceiveResult::CONSUME_ERROR;
        }
        received_total += static_cast<size_t>(received);
    }
    return BoundedReceiveResult::COMPLETE;
}
