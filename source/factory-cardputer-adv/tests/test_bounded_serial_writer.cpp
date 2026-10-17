#include "BoundedSerialWriter.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>

struct FakeTransport : FactoryDiagnostics::SerialTransport {
    int64_t time = 0;
    std::vector<uint8_t> output;
    size_t max_write = 64;
    int zero_writes = 0;
    int calls = 0;
    bool stall = false, fail = false, flush_failure = false, overreport = false;
    int64_t nowUs() override { return time; }
    int write(const uint8_t* data, size_t bytes, uint32_t timeout) override {
        ++calls;
        assert(bytes <= 64 && timeout <= 10000);
        if (fail) return -1;
        if (overreport) return bytes + 1;
        if (stall || zero_writes-- > 0) { time += timeout * 2; return 0; }
        time += 100;
        const size_t count = std::min(bytes, max_write);
        output.insert(output.end(), data, data + count);
        return count;
    }
    bool drain(uint32_t timeout) override {
        if (flush_failure) { time += timeout; return false; }
        time += 100;
        return true;
    }
    void yield() override { time += 1000; }
};
int main() {
    std::vector<uint8_t> frame(64800);
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<uint8_t>(i);
    {
        FakeTransport t; t.max_write = 7; t.zero_writes = 3;
        FactoryDiagnostics::BoundedSerialWriter w(t, 4000000);
        assert(w.writeAll(frame.data(), frame.size()) && w.finish());
        assert(t.output == frame && w.acceptedBytes() == frame.size());
        assert(std::strcmp(w.error(), "none") == 0);
    }
    {
        FakeTransport t; t.stall = true;
        FactoryDiagnostics::BoundedSerialWriter w(t, 4000000);
        assert(!w.writeAll(frame.data(), frame.size()));
        assert(t.time >= 4000000 && t.time < 4002000 && t.calls < 500);
        assert(w.acceptedBytes() == 0 && std::strcmp(w.error(), "write_timeout") == 0);
    }
    {
        FakeTransport t;
        FactoryDiagnostics::BoundedSerialWriter w(t, 4000000);
        assert(w.writeAll(frame.data(), 1024));
        t.stall = true;
        assert(!w.writeAll(frame.data() + 1024, frame.size() - 1024));
        assert(w.acceptedBytes() == 1024 && t.output.size() == 1024);
        assert(w.elapsedUs() < 4002000);
    }
    {
        FakeTransport t; t.flush_failure = true;
        FactoryDiagnostics::BoundedSerialWriter w(t, 4000000);
        assert(w.writeAll(frame.data(), frame.size()) && !w.finish());
        assert(t.time == 4000000 && std::strcmp(w.error(), "flush_timeout") == 0);
    }
    {
        FakeTransport t; t.fail = true;
        FactoryDiagnostics::BoundedSerialWriter w(t, 4000000);
        assert(!w.writeAll(frame.data(), frame.size()) && t.calls == 1);
        assert(std::strcmp(w.error(), "write_error") == 0);
    }
    {
        FakeTransport t; t.overreport = true;
        FactoryDiagnostics::BoundedSerialWriter w(t, 4000000);
        assert(!w.writeAll(frame.data(), frame.size()) && w.acceptedBytes() == 0);
    }
    {
        FakeTransport t;
        FactoryDiagnostics::BoundedSerialWriter w(t, 2000000);
        assert(w.writeAll(frame.data(), 64));
        t.time = 2000001;
        assert(!w.writeAll(frame.data(), 64)); // Deadline never resets per write.
    }
}
