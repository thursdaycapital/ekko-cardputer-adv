#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Bounded-memory PCM WAV storage. Mounting/formatting the SD card belongs to HAL;
// this module never formats a card, replaces a recording, or deletes user files.
namespace factory_record {
namespace detail {
inline std::string join(const std::string& directory, const std::string& name) {
    return directory + (directory.empty() || directory.back() == '/' ? "" : "/") + name;
}
inline std::string systemError(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}
inline int pathStat(const char* path, struct stat* result) {
#ifdef ESP_PLATFORM
    // ESP-IDF FAT VFS has no symlinks and does not implement lstat.
    return ::stat(path, result);
#else
    return ::lstat(path, result);
#endif
}
inline int readFlags() {
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}
inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void put16(uint8_t* p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
inline void put32(uint8_t* p, uint32_t value) {
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}
inline bool littleEndian() {
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1;
}
inline bool seek(int fd, uint64_t offset) {
    if (offset > uint64_t(std::numeric_limits<off_t>::max())) { errno = EOVERFLOW; return false; }
    return ::lseek(fd, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(offset);
}
inline bool readAll(int fd, void* target, size_t size) {
    auto* data = static_cast<uint8_t*>(target);
    size_t received = 0;
    while (received < size) {
        const ssize_t count = ::read(fd, data + received, size - received);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (count == 0) errno = EIO; return false; }
        received += static_cast<size_t>(count);
    }
    return true;
}
inline bool writeAll(int fd, const void* source, size_t size, size_t& written) {
    const auto* data = static_cast<const uint8_t*>(source);
    written = 0;
    while (written < size) {
        const ssize_t count = ::write(fd, data + written, size - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (count == 0) errno = EIO; return false; }
        written += static_cast<size_t>(count);
    }
    return true;
}
inline bool recordingNumber(const std::string& name, uint32_t& number) {
    if (name.size() != 12) return false;
    auto upper = [](char ch) { return ch >= 'a' && ch <= 'z' ? char(ch - 'a' + 'A') : ch; };
    if (upper(name[0]) != 'R' || upper(name[1]) != 'E' || upper(name[2]) != 'C' ||
        name[8] != '.' || upper(name[9]) != 'W' || upper(name[10]) != 'A' || upper(name[11]) != 'V') return false;
    number = 0;
    for (size_t i = 3; i < 8; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
        number = number * 10 + uint32_t(name[i] - '0');
    }
    return number != 0;
}
} // namespace detail

struct Entry {
    std::string name;
    std::string path;
    uint32_t number = 0;
    uint32_t dataBytes = 0;
    uint32_t sampleRate = 0;
};

class WavWriter {
public:
    static constexpr uint32_t kSampleRate = 16000;
    WavWriter() = default;
    ~WavWriter() { finish(); }
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool open(const std::string& directory) {
        if (_fd >= 0 && !finish()) return false;
        _error.clear(); _path.clear(); _data_bytes = 0; _checkpoint_bytes = 0; _needs_truncate = false;
        if (directory.empty()) return fail("Empty recording directory");
        if (::mkdir(directory.c_str(), 0775) != 0 && errno != EEXIST) return failSystem("Create recording directory");
        struct stat status = {};
        if (detail::pathStat(directory.c_str(), &status) != 0) return failSystem("Inspect recording directory");
        if (!S_ISDIR(status.st_mode)) return fail("Recording directory is not a real directory");
        DIR* directory_handle = ::opendir(directory.c_str());
        if (!directory_handle) return failSystem("Read recording directory");
        uint32_t maximum = 0;
        errno = 0;
        while (const dirent* entry = ::readdir(directory_handle)) {
            uint32_t number = 0;
            if (detail::recordingNumber(entry->d_name, number)) maximum = std::max(maximum, number);
            errno = 0;
        }
        const int scan_error = errno;
        const int close_result = ::closedir(directory_handle);
        if (scan_error) { errno = scan_error; return failSystem("Scan recording directory"); }
        if (close_result != 0) return failSystem("Close recording directory");
        for (uint32_t number = maximum + 1; number <= 99999; ++number) {
            char name[13];
            std::snprintf(name, sizeof(name), "REC%05u.WAV", unsigned(number));
            _path = detail::join(directory, name);
            _fd = ::open(_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0664);
            if (_fd >= 0) break;
            if (errno != EEXIST) return failSystem("Create WAV file");
        }
        if (_fd < 0) return fail("Recording numbers exhausted (99999)");
        if (!writeHeader() || ::fsync(_fd) != 0) {
            if (_error.empty()) failSystem("Sync WAV header");
            finish();
            return false;
        }
        return true;
    }

    bool append(const int16_t* samples, size_t count) {
        if (_fd < 0) return fail("No open WAV file");
        if (!_error.empty()) return false;
        if (!samples && count) return fail("Missing PCM samples");
        constexpr uint64_t riff_limit = uint64_t(UINT32_MAX) - 36;
        const uint64_t seek_limit = uint64_t(std::numeric_limits<off_t>::max()) - 44;
        const uint64_t maximum = std::min(riff_limit, seek_limit) & ~uint64_t(1);
        if (count > (maximum - _data_bytes) / 2) return fail("WAV file size limit reached");
        if (!count) return true;
        if (detail::littleEndian()) {
            if (!appendBytes(samples, count * 2)) return false;
        } else {
            uint8_t bytes[512];
            while (count) {
                const size_t block = std::min<size_t>(count, sizeof(bytes) / 2);
                for (size_t i = 0; i < block; ++i) detail::put16(bytes + i * 2, static_cast<uint16_t>(samples[i]));
                if (!appendBytes(bytes, block * 2)) return false;
                samples += block; count -= block;
            }
        }
        // A valid, synced header at least once per recorded second limits damage
        // from loss of power. Normal stop always finalizes every complete sample.
        return _data_bytes - _checkpoint_bytes < kSampleRate * 2 || checkpoint();
    }

    bool checkpoint() {
        if (_fd < 0) return fail("No open WAV file");
        if (!_error.empty()) return false;
        if (!writeHeader()) return false;
        if (::fsync(_fd) != 0) return failSystem("Sync recording");
        _checkpoint_bytes = _data_bytes;
        return true;
    }

    bool finish() {
        if (_fd < 0) return _error.empty();
        // Even after a short write/disk-full failure, keep all complete samples
        // recoverable. Never unlink the user's partial recording.
        if (_needs_truncate && ::ftruncate(_fd, static_cast<off_t>(uint64_t(44) + _data_bytes)) != 0)
            failSystem("Trim incomplete PCM sample");
        writeHeader();
        if (::fsync(_fd) != 0) failSystem("Sync completed recording");
        const int fd = _fd; _fd = -1;
        if (::close(fd) != 0) failSystem("Close completed recording");
        return _error.empty();
    }
    bool active() const { return _fd >= 0; }
    uint32_t dataBytes() const { return _data_bytes; }
    const std::string& path() const { return _path; }
    const std::string& error() const { return _error; }

private:
    int _fd = -1;
    uint32_t _data_bytes = 0;
    uint32_t _checkpoint_bytes = 0;
    bool _needs_truncate = false;
    std::string _path;
    std::string _error;
    bool fail(const std::string& message) { if (_error.empty()) _error = message; return false; }
    bool failSystem(const char* operation) { return fail(detail::systemError(operation)); }
    bool appendBytes(const void* samples, size_t bytes) {
        size_t written = 0;
        const bool ok = detail::writeAll(_fd, samples, bytes, written);
        _data_bytes += static_cast<uint32_t>(written & ~size_t(1));
        if (!ok) { _needs_truncate = (written & 1) != 0; return failSystem("Write recording (card full or unavailable)"); }
        return true;
    }
    bool writeHeader() {
        uint8_t header[44] = {};
        std::memcpy(header, "RIFF", 4); detail::put32(header + 4, _data_bytes + 36);
        std::memcpy(header + 8, "WAVEfmt ", 8); detail::put32(header + 16, 16);
        detail::put16(header + 20, 1); detail::put16(header + 22, 1);
        detail::put32(header + 24, kSampleRate); detail::put32(header + 28, kSampleRate * 2);
        detail::put16(header + 32, 2); detail::put16(header + 34, 16);
        std::memcpy(header + 36, "data", 4); detail::put32(header + 40, _data_bytes);
        size_t written = 0;
        if (!detail::seek(_fd, 0) || !detail::writeAll(_fd, header, sizeof(header), written) ||
            !detail::seek(_fd, uint64_t(44) + _data_bytes)) return failSystem("Finalize WAV header");
        return true;
    }
};

class WavReader {
public:
    WavReader() = default;
    ~WavReader() { close(); }
    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;
    bool open(const std::string& path) {
        close(); _error.clear(); _sample_rate = 0; _data_bytes = 0; _remaining = 0;
        struct stat status = {};
        if (detail::pathStat(path.c_str(), &status) != 0) return failSystem("Inspect WAV file");
        if (!S_ISREG(status.st_mode)) return fail("WAV path is not a regular file");
        _fd = ::open(path.c_str(), detail::readFlags());
        if (_fd < 0) return failSystem("Open WAV file");
        if (::fstat(_fd, &status) != 0) return failSystem("Inspect open WAV file");
        if (!S_ISREG(status.st_mode) || status.st_size < 12) return fail("Truncated WAV header");
        uint8_t header[12];
        if (!detail::readAll(_fd, header, sizeof(header))) return failSystem("Read WAV header");
        if (std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return fail("Not a RIFF WAVE file");
        const uint64_t end = uint64_t(detail::u32(header + 4)) + 8;
        if (end < 12 || end > uint64_t(status.st_size)) return fail("Truncated RIFF payload");
        bool have_format = false, have_data = false;
        uint64_t offset = 12, data_offset = 0;
        while (offset < end) {
            if (end - offset < 8) return fail("Truncated WAV chunk header");
            uint8_t chunk[8];
            if (!detail::seek(_fd, offset) || !detail::readAll(_fd, chunk, sizeof(chunk))) return failSystem("Read WAV chunk");
            const uint32_t size = detail::u32(chunk + 4);
            const uint64_t next = offset + 8 + uint64_t(size) + (size & 1u);
            if (next > end) return fail("Truncated WAV chunk");
            if (!std::memcmp(chunk, "fmt ", 4)) {
                if (have_format || size < 16) return fail("Invalid WAV format chunk");
                uint8_t format[16];
                if (!detail::readAll(_fd, format, sizeof(format))) return failSystem("Read PCM format");
                _sample_rate = detail::u32(format + 4);
                if (detail::u16(format) != 1 || detail::u16(format + 2) != 1 ||
                    detail::u16(format + 12) != 2 || detail::u16(format + 14) != 16 ||
                    !_sample_rate || _sample_rate > UINT32_MAX / 2 || detail::u32(format + 8) != _sample_rate * 2)
                    return fail("Only mono 16-bit PCM WAV is supported");
                have_format = true;
            } else if (!std::memcmp(chunk, "data", 4)) {
                if (have_data || (size & 1u)) return fail("Invalid PCM data length");
                data_offset = offset + 8; _data_bytes = size; have_data = true;
            }
            offset = next;
        }
        if (!have_format || !have_data) return fail("WAV format or audio data is missing");
        if (!detail::seek(_fd, data_offset)) return failSystem("Seek WAV audio");
        _remaining = _data_bytes;
        return true;
    }
    size_t read(int16_t* destination, size_t capacitySamples) {
        if (_fd < 0 || !_error.empty()) return 0;
        if (!destination && capacitySamples) { fail("Missing playback buffer"); return 0; }
        const size_t samples = std::min<uint64_t>(capacitySamples, _remaining / 2);
        if (!samples) return 0;
        if (!detail::readAll(_fd, destination, samples * 2)) { failSystem("Read recording (card removed or truncated)"); return 0; }
        if (!detail::littleEndian()) {
            auto* bytes = reinterpret_cast<uint8_t*>(destination);
            for (size_t i = 0; i < samples; ++i) destination[i] = static_cast<int16_t>(detail::u16(bytes + i * 2));
        }
        _remaining -= static_cast<uint32_t>(samples * 2);
        return samples;
    }
    void close() {
        if (_fd >= 0) {
            const int fd = _fd; _fd = -1;
            if (::close(fd) != 0 && _error.empty()) _error = detail::systemError("Close WAV reader");
        }
        _remaining = 0;
    }
    uint32_t sampleRate() const { return _sample_rate; }
    uint32_t dataBytes() const { return _data_bytes; }
    uint32_t remainingSamples() const { return _remaining / 2; }
    bool failed() const { return !_error.empty(); }
    const std::string& error() const { return _error; }

private:
    int _fd = -1;
    uint32_t _sample_rate = 0, _data_bytes = 0, _remaining = 0;
    std::string _error;
    bool fail(const std::string& message) {
        if (_error.empty()) _error = message;
        if (_fd >= 0) { ::close(_fd); _fd = -1; }
        _remaining = 0;
        return false;
    }
    bool failSystem(const char* operation) { return fail(detail::systemError(operation)); }
};

// Keep the newest entries only so a full card cannot exhaust the device heap.
inline std::vector<Entry> listRecordings(const std::string& directory, std::string* error = nullptr,
                                         size_t maxEntries = 64) {
    if (error) error->clear();
    std::vector<Entry> entries;
    if (!maxEntries) return entries;
    struct stat status = {};
    if (detail::pathStat(directory.c_str(), &status) != 0) {
        // First use has no RECORD directory yet; opening a writer creates it.
        if (errno == ENOENT) return entries;
        if (error) *error = detail::systemError("Inspect recording directory");
        return entries;
    }
    if (!S_ISDIR(status.st_mode)) {
        if (error) *error = "Recording directory is unavailable";
        return entries;
    }
    DIR* handle = ::opendir(directory.c_str());
    if (!handle) { if (error) *error = detail::systemError("Open recording directory"); return entries; }
    errno = 0;
    while (const dirent* item = ::readdir(handle)) {
        uint32_t number = 0;
        const std::string name(item->d_name);
        if (detail::recordingNumber(name, number)) {
            const std::string path = detail::join(directory, name);
            WavReader reader;
            if (reader.open(path)) {
                entries.push_back({name, path, number, reader.dataBytes(), reader.sampleRate()});
                std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.number > b.number; });
                if (entries.size() > maxEntries) entries.pop_back();
            }
        }
        errno = 0;
    }
    const int scan_error = errno;
    const int close_result = ::closedir(handle);
    if (scan_error && error) { errno = scan_error; *error = detail::systemError("Scan recordings"); }
    else if (close_result != 0 && error) *error = detail::systemError("Close recording directory");
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.number > b.number; });
    return entries;
}
} // namespace factory_record
