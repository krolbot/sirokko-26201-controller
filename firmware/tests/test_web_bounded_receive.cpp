#include "bounded_receive.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

struct Step {
    int result{};
    uint64_t elapsed_ms{};
    const char* bytes{};
};

struct Source {
    std::vector<Step> steps;
    size_t next{};
    uint64_t now_ms{};
};

int receive_step(void* context, char* output, size_t capacity) {
    auto& source = *static_cast<Source*>(context);
    if (source.next >= source.steps.size()) return 0;
    const Step& step = source.steps[source.next++];
    source.now_ms += step.elapsed_ms;
    if (step.result > 0) {
        if (!step.bytes || static_cast<size_t>(step.result) > capacity) return 0;
        std::memcpy(output, step.bytes, static_cast<size_t>(step.result));
    }
    return step.result;
}

uint64_t clock_ms(void* context) {
    return static_cast<Source*>(context)->now_ms;
}

struct Sink {
    std::string bytes;
    bool allow{true};
};

bool consume(void* context, const char* bytes, size_t size) {
    auto& sink = *static_cast<Sink*>(context);
    if (!sink.allow) return false;
    sink.bytes.append(bytes, size);
    return true;
}

BoundedReceiveResult run(Source* source, Sink* sink, size_t total, uint64_t budget) {
    char chunk[3]{};
    return receive_exact_bounded(total, budget, -123, chunk, sizeof(chunk), source, receive_step,
                                 sink, consume, source, clock_ms);
}

}  // namespace

int main() {
    {
        Source source{{{2, 1, "ab"}, {2, 1, "cd"}}};
        Sink sink{};
        check(run(&source, &sink, 4, 10) == BoundedReceiveResult::COMPLETE,
              "segmented receive completes within one absolute budget");
        check(sink.bytes == "abcd", "complete receive applies every byte once");
    }
    {
        Source source{{{2, 10, "ab"}}};
        Sink sink{};
        check(run(&source, &sink, 2, 10) == BoundedReceiveResult::DEADLINE_EXCEEDED,
              "bytes completing exactly at deadline are rejected");
        check(sink.bytes.empty(), "deadline-expired bytes are never passed to the sink");
    }
    {
        Source source{{{-123, 4, nullptr}, {-123, 6, nullptr}}};
        Sink sink{};
        check(run(&source, &sink, 1, 10) == BoundedReceiveResult::DEADLINE_EXCEEDED,
              "repeated transport timeouts share one absolute deadline");
        check(source.next == 2, "deadline ends retry loop without an unbounded receive");
    }
    {
        Source source{{{1, 1, "x"}}};
        Sink sink{};
        sink.allow = false;
        check(run(&source, &sink, 1, 10) == BoundedReceiveResult::CONSUME_ERROR,
              "consumer refusal terminates bounded receive");
    }
    {
        Source source{{{0, 1, nullptr}}};
        Sink sink{};
        check(run(&source, &sink, 1, 10) == BoundedReceiveResult::RECEIVE_ERROR,
              "zero-length non-timeout receive is rejected");
    }
    if (failures == 0) {
        std::puts("PASS: bounded receive absolute deadline and sink safety");
    }
    return failures == 0 ? 0 : 1;
}
