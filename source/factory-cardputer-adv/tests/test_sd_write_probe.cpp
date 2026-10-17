#include "SdWriteProbe.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <iostream>
#include <vector>
#include <cstdlib>
namespace fs = std::filesystem;
static std::string readAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
int main(int argc, char** argv) {
    assert(argc == 1 || argc == 2);
    const fs::path parent = argc == 2 ? fs::path(argv[1]) : fs::temp_directory_path();
    std::string pattern = (parent / "factory-sd-probe-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const char* created = ::mkdtemp(writable.data());
    assert(created);
    const fs::path directory(created);
    std::ofstream(directory / "test.txt") << "user data must stay";
    std::ofstream(directory / "T000000") << "existing name";
    const auto originalTime = fs::last_write_time(directory / "test.txt");
    for (int i = 0; i < 40; ++i) {
        assert(factory_storage::probeWritable(directory.c_str()));
        assert(readAll(directory / "test.txt") == "user data must stay");
        assert(fs::last_write_time(directory / "test.txt") == originalTime);
        assert(readAll(directory / "T000000") == "existing name");
        assert(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == 2);
    }
    assert(!factory_storage::probeWritable(nullptr));
    assert(!factory_storage::probeWritable(""));
    assert(!factory_storage::probeWritable((directory / "missing").c_str()));
    assert(fs::exists(directory / "T000000"));
    assert(!factory_storage::probeWritable((directory / "test.txt").c_str()));
    assert(fs::remove(directory / "test.txt"));
    assert(fs::remove(directory / "T000000"));
    assert(fs::is_empty(directory));
    assert(fs::remove(directory));
    std::cout << "SD probe: 40 real POSIX write/read probes; user files unchanged; no temporary files remain\n";
}
