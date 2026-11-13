#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdarg>

class JsonBuilder {
  public:
    JsonBuilder(char *output, size_t capacity) : output_(output), capacity_(capacity) {
        if (!output_ || capacity_ == 0)
            ok_ = false;
        else
            output_[0] = 0;
    }

    bool ok() const { return ok_; }
    const char *data() const { return output_; }
    size_t size() const { return size_; }

    void raw(const char *text) {
        if (!text) {
            ok_ = false;
            return;
        }
        while (*text)
            append(*text++);
    }

    void character(char value) { append(value); }

    void quoted(const char *text) {
        character('"');
        if (!text)
            text = "";
        for (; *text; ++text) {
            const unsigned char value = static_cast<unsigned char>(*text);
            if (value == '"' || value == '\\') {
                character('\\');
                character(static_cast<char>(value));
            } else if (value == '\n')
                raw("\\n");
            else if (value == '\r')
                raw("\\r");
            else if (value == '\t')
                raw("\\t");
            else if (value < 0x20U)
                raw("?");
            else
                character(static_cast<char>(value));
        }
        character('"');
    }

    void boolean(bool value) { raw(value ? "true" : "false"); }
    void integer(int32_t value) { formatted("%ld", static_cast<long>(value)); }
    void unsigned_integer(uint64_t value) { formatted("%llu", static_cast<unsigned long long>(value)); }
    void hexadecimal(uint64_t value) { formatted("%llx", static_cast<unsigned long long>(value)); }
    void real(double value) { formatted("%.8g", value); }

  private:
    void append(char value) {
        if (!ok_ || size_ + 1 >= capacity_) {
            ok_ = false;
            return;
        }
        output_[size_++] = value;
        output_[size_] = 0;
    }

    void formatted(const char *format, ...) {
        if (!ok_ || size_ >= capacity_) {
            ok_ = false;
            return;
        }
        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(output_ + size_, capacity_ - size_, format, arguments);
        va_end(arguments);
        if (written < 0 || static_cast<size_t>(written) >= capacity_ - size_) {
            ok_ = false;
            return;
        }
        size_ += static_cast<size_t>(written);
    }

    char *output_{};
    size_t capacity_{};
    size_t size_{};
    bool ok_{true};
};
