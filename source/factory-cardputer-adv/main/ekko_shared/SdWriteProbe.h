#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace factory_storage {
// Exclusive 8.3-compatible temporary name: never overwrite a user's file.
inline bool probeWritable(const char* directory) {
    if (!directory || !*directory) return false;
    std::string path(directory);
    if (path.back() != '/') path += '/';
    path += "TXXXXXX";
    const int fd = mkstemp(&path[0]);
    if (fd < 0) return false;
    FILE* file = fdopen(fd, "w+");
    bool ok = false;
    if (file) {
        constexpr char payload[] = "Cardputer storage probe";
        char received[sizeof(payload)] = {};
        ok = fwrite(payload, 1, sizeof(payload), file) == sizeof(payload) &&
             fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0 &&
             fread(received, 1, sizeof(received), file) == sizeof(received) &&
             memcmp(received, payload, sizeof(payload)) == 0;
        if (fclose(file) != 0) ok = false;
    } else {
        close(fd);
    }
    // Cleanup is attempted after every successful exclusive creation.
    if (remove(path.c_str()) != 0) ok = false;
    return ok;
}
} // namespace factory_storage
