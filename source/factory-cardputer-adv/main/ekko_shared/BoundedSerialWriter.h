#pragma once
#include <cstddef>
#include <cstdint>

// All partial/zero writes and the final drain share one absolute deadline.
namespace FactoryDiagnostics {
class SerialTransport {
public:
    virtual ~SerialTransport() = default;
    virtual int64_t nowUs() = 0;
    virtual int write(const uint8_t* data, size_t bytes, uint32_t timeoutUs) = 0;
    virtual bool drain(uint32_t timeoutUs) = 0;
    virtual void yield() = 0;
};

class BoundedSerialWriter {
public:
    BoundedSerialWriter(SerialTransport& transport, uint32_t budgetUs)
        : _transport(transport), _started(transport.nowUs()), _budget(budgetUs) {}

    bool writeAll(const void* data, size_t bytes)
    {
        if ((!data && bytes) || _error) return false;
        const auto* input = static_cast<const uint8_t*>(data);
        size_t offset = 0;
        while (offset < bytes) {
            const uint32_t left = remainingUs();
            if (!left) { _error = "write_timeout"; return false; }
            const size_t request = bytes - offset < 64 ? bytes - offset : 64;
            // The SDK waits once for its mutex and once for its ring buffer.
            const uint32_t timeout = left / 2 < 10000 ? left / 2 : 10000;
            const int written = _transport.write(input + offset, request, timeout);
            if (written < 0 || static_cast<size_t>(written) > request) {
                _error = "write_error";
                return false;
            }
            offset += static_cast<size_t>(written);
            _accepted += static_cast<size_t>(written);
            if (!written) _transport.yield();
        }
        return true;
    }
    bool finish()
    {
        if (_error) return false;
        const uint32_t left = remainingUs();
        if (!left || !_transport.drain(left)) { _error = "flush_timeout"; return false; }
        return true;
    }
    size_t acceptedBytes() const { return _accepted; }
    const char* error() const { return _error ? _error : "none"; }
    uint32_t elapsedUs() const
    {
        const int64_t elapsed = _transport.nowUs() - _started;
        return elapsed < 0 ? _budget : static_cast<uint32_t>(elapsed);
    }
private:
    uint32_t remainingUs() const
    {
        const int64_t elapsed = _transport.nowUs() - _started;
        if (elapsed < 0 || elapsed >= _budget) return 0;
        return _budget - static_cast<uint32_t>(elapsed);
    }
    SerialTransport& _transport;
    const int64_t _started;
    const uint32_t _budget;
    size_t _accepted = 0;
    const char* _error = nullptr;
};
}
