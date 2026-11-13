#pragma once

#include <cstddef>
#include <cstdint>

enum class BoundedReceiveResult : uint8_t {
    COMPLETE,
    DEADLINE_EXCEEDED,
    RECEIVE_ERROR,
    CONSUME_ERROR,
    INVALID_ARGUMENT,
};

using ReceiveChunk = int (*)(void *context, char *buffer, size_t capacity);
using ConsumeChunk = bool (*)(void *context, const char *buffer, size_t size);
using MonotonicMilliseconds = uint64_t (*)(void *context);

BoundedReceiveResult receive_exact_bounded(size_t total_size, uint64_t budget_ms, int timeout_result,
                                           char *chunk_buffer, size_t chunk_capacity, void *receive_context,
                                           ReceiveChunk receive_chunk, void *consume_context,
                                           ConsumeChunk consume_chunk, void *clock_context,
                                           MonotonicMilliseconds monotonic_ms);
