#include "apps/app_record/record_storage.h"

#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sys/resource.h>

namespace fs = std::filesystem;
using factory_record::WavReader;
using factory_record::WavWriter;
static size_t checks = 0;
static void checked(const char* description) { ++checks; std::cout << "PASS " << description << '\n'; }
static std::vector<uint8_t> bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}
static void store(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream stream(path, std::ios::binary);
    assert(stream);
    stream.write(reinterpret_cast<const char*>(data.data()), data.size());
    assert(stream.good());
}
static void addChunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& payload) {
    out.insert(out.end(), tag, tag + 4);
    const size_t position = out.size(); out.resize(out.size() + 4);
    factory_record::detail::put32(out.data() + position, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    if (payload.size() & 1) out.push_back(0);
}
static void setRiffSize(std::vector<uint8_t>& out) {
    factory_record::detail::put32(out.data() + 4, out.size() - 8);
}
static std::vector<uint8_t> emptyRiff() {
    return {'R','I','F','F',0,0,0,0,'W','A','V','E'};
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path root(argv[1]);
    const fs::path directory = root / "RECORD";
    fs::create_directory(directory);
    const fs::path userFile = directory / "USER.TXT";
    store(userFile, {'k','e','e','p'});
    store(directory / "REC00001.WAV", {'o','r','i','g','i','n','a','l'});
    const auto userBefore = bytes(userFile);
    const auto originalBefore = bytes(directory / "REC00001.WAV");

    std::vector<int16_t> pcm(40001);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>((i * 197u) & 0xffff);
    std::string recorded;
    {
        WavWriter writer;
        assert(writer.open(directory));
        assert(fs::path(writer.path()).filename() == "REC00002.WAV");
        assert(writer.active() && writer.dataBytes() == 0);
        recorded = writer.path();
        for (size_t index = 0; index < pcm.size();) {
            const size_t count = std::min<size_t>(733, pcm.size() - index);
            assert(writer.append(pcm.data() + index, count)); index += count;
        }
        assert(writer.dataBytes() == pcm.size() * 2);
        assert(writer.checkpoint());
        assert(writer.finish() && !writer.active());
        assert(writer.finish());
    }
    checked("8.3 sequential name, streamed 2.5-second WAV, checked finalization");
    assert(bytes(userFile) == userBefore && bytes(directory / "REC00001.WAV") == originalBefore);
    checked("existing user files and existing invalid WAV are never replaced");
    const auto valid = bytes(recorded);
    assert(valid.size() == 44 + pcm.size() * 2);
    assert(factory_record::detail::u32(valid.data() + 4) == valid.size() - 8);
    assert(factory_record::detail::u32(valid.data() + 40) == pcm.size() * 2);
    assert(valid[44] == 0 && valid[45] == 0 && valid[46] == 197 && valid[47] == 0);
    checked("RIFF/data sizes and little-endian signed PCM encoding");

    WavReader reader;
    assert(reader.open(recorded));
    assert(reader.sampleRate() == 16000 && reader.dataBytes() == pcm.size() * 2);
    std::vector<int16_t> received;
    int16_t block[113];
    while (const size_t count = reader.read(block, 113)) received.insert(received.end(), block, block + count);
    assert(!reader.failed() && reader.remainingSamples() == 0 && received == pcm);
    reader.close();
    checked("bounded 113-sample reads preserve all samples and EOF state");

    {
        WavWriter writer;
        assert(writer.open(directory));
        assert(fs::path(writer.path()).filename() == "REC00003.WAV");
        assert(writer.append(pcm.data(), 10));
        // RAII must finalize when the app leaves its page normally.
    }
    assert(reader.open((directory / "REC00003.WAV").string()) && reader.dataBytes() == 20);
    reader.close();
    checked("normal scope exit finalizes without retaining audio in RAM");

    {
        WavWriter one, two;
        assert(one.open(directory) && two.open(directory));
        assert(one.path() != two.path());
        assert(one.finish() && two.finish());
    }
    checked("two active writers use exclusive distinct files");
    {
        WavWriter writer;
        assert(writer.open(directory));
        assert(writer.append(pcm.data(), 16000));
        WavReader checkpoint;
        assert(checkpoint.open(writer.path()) && checkpoint.dataBytes() == 32000);
        assert(writer.append(pcm.data(), 3));
        assert(writer.finish());
        assert(reader.open(writer.path()) && reader.dataBytes() == 32006);
        reader.close();
    }
    checked("one-second synced header checkpoints and final tail preservation");

    const fs::path invalid = root / "INVALID.WAV";
    auto expectInvalid = [&](std::vector<uint8_t> data) {
        store(invalid, data);
        assert(!reader.open(invalid.string()) && reader.failed() && !reader.error().empty());
    };
    expectInvalid({});
    expectInvalid({'R','I','F','F'});
    auto bad = valid; bad.pop_back(); expectInvalid(bad);
    bad = valid; bad[0] = 'X'; expectInvalid(bad);
    bad = valid; factory_record::detail::put32(bad.data() + 4, UINT32_MAX); expectInvalid(bad);
    bad = valid; factory_record::detail::put32(bad.data() + 40, UINT32_MAX); expectInvalid(bad);
    bad = valid; factory_record::detail::put32(bad.data() + 40, 3); expectInvalid(bad);
    bad = valid; factory_record::detail::put16(bad.data() + 20, 3); expectInvalid(bad);
    bad = valid; factory_record::detail::put16(bad.data() + 22, 2); expectInvalid(bad);
    bad = valid; factory_record::detail::put16(bad.data() + 34, 8); expectInvalid(bad);
    bad = valid; factory_record::detail::put16(bad.data() + 32, 4); expectInvalid(bad);
    bad = valid; factory_record::detail::put32(bad.data() + 24, 0); expectInvalid(bad);
    bad = valid; factory_record::detail::put32(bad.data() + 28, 123); expectInvalid(bad);
    checked("truncated/overflowing RIFF, odd PCM, non-PCM/stereo/8-bit/bad alignment/rate rejected");

    const std::vector<uint8_t> format(valid.begin() + 20, valid.begin() + 36);
    const std::vector<uint8_t> data(valid.begin() + 44, valid.end());
    auto extra = emptyRiff();
    addChunk(extra, "JUNK", {1, 2, 3});
    addChunk(extra, "data", data);
    addChunk(extra, "LIST", {9, 8, 7, 6, 5});
    addChunk(extra, "fmt ", format);
    setRiffSize(extra); store(invalid, extra);
    assert(reader.open(invalid.string()) && reader.dataBytes() == data.size());
    assert(reader.read(block, 113) == 113 && std::equal(block, block + 113, pcm.begin()));
    reader.close();
    checked("unknown odd-padded chunks and data-before-fmt parse correctly");
    bad = emptyRiff(); addChunk(bad, "fmt ", format); addChunk(bad, "fmt ", format); addChunk(bad, "data", data);
    setRiffSize(bad); expectInvalid(bad);
    bad = emptyRiff(); addChunk(bad, "fmt ", format); addChunk(bad, "data", data); addChunk(bad, "data", {});
    setRiffSize(bad); expectInvalid(bad);
    bad = emptyRiff(); addChunk(bad, "fmt ", format); setRiffSize(bad); expectInvalid(bad);
    bad = emptyRiff(); addChunk(bad, "data", data); setRiffSize(bad); expectInvalid(bad);
    bad = emptyRiff(); addChunk(bad, "fmt ", format); addChunk(bad, "data", data); bad.push_back(1);
    setRiffSize(bad); expectInvalid(bad);
    checked("duplicate/missing chunks and dangling chunk headers rejected");

    store(invalid, valid);
    assert(reader.open(invalid.string()));
    fs::resize_file(invalid, 45);
    assert(reader.read(block, 10) == 0 && reader.failed());
    store(invalid, valid); assert(reader.open(invalid.string()));
    assert(reader.read(nullptr, 2) == 0 && reader.failed());
    assert(reader.open(recorded) && !reader.failed()); reader.close();
    checked("card/file truncation during playback is an error and reopen clears error");

    fs::create_directory_symlink(directory, root / "ALIAS");
    WavWriter writer;
    assert(!writer.open((root / "ALIAS").string()));
    assert(!writer.open(userFile.string()));
    assert(!writer.open((root / "MISSING" / "RECORD").string()));
    fs::create_symlink(recorded, directory / "REC00100.WAV");
    assert(!reader.open((directory / "REC00100.WAV").string()));
    assert(writer.open(directory));
    assert(fs::path(writer.path()).filename() == "REC00101.WAV");
    assert(writer.finish());
    assert(bytes(recorded) == valid);
    checked("symlink directories/files, occupied paths and missing parents cannot overwrite data");

    store(directory / "rec00200.wav", valid);
    store(directory / "REC00201.WAV", {1, 2, 3});
    store(directory / "OTHER.WAV", valid);
    std::string error = "old";
    const auto entries = factory_record::listRecordings(directory, &error);
    assert(error.empty() && entries.size() == 7);
    assert(entries[0].number == 200 && entries[1].number == 101 && entries.back().number == 2);
    assert(entries[0].sampleRate == 16000 && entries[0].dataBytes == data.size());
    const auto limited = factory_record::listRecordings(directory, &error, 2);
    assert(limited.size() == 2 && limited[0].number == 200 && limited[1].number == 101);
    assert(factory_record::listRecordings(directory, &error, 0).empty());
    assert(factory_record::listRecordings(root / "MISSING", &error).empty() && error.empty());
    assert(factory_record::listRecordings(root / "ALIAS", &error).empty() && !error.empty());
    checked("newest-first bounded listing filters malformed, unrelated and symlink WAV files");

    const fs::path full = root / "FULL";
    {
        WavWriter partial;
        assert(partial.open(full));
        const std::string partialPath = partial.path();
        struct rlimit original = {};
        assert(::getrlimit(RLIMIT_FSIZE, &original) == 0);
        struct rlimit limitedSize = original; limitedSize.rlim_cur = 47;
        const auto previousSignal = std::signal(SIGXFSZ, SIG_IGN);
        assert(::setrlimit(RLIMIT_FSIZE, &limitedSize) == 0);
        const bool appended = partial.append(pcm.data(), 20);
        const bool finished = partial.finish();
        assert(::setrlimit(RLIMIT_FSIZE, &original) == 0);
        std::signal(SIGXFSZ, previousSignal);
        assert(!appended && !finished && !partial.error().empty() && !partial.active());
        assert(partial.dataBytes() == 2 && fs::file_size(partialPath) == 46);
        assert(reader.open(partialPath) && reader.dataBytes() == 2);
        assert(reader.read(block, 113) == 1 && block[0] == pcm[0]); reader.close();
    }
    checked("real OS short-write limit salvages complete samples, trims odd byte and reports failure");

    const fs::path exhausted = root / "EXHAUST";
    fs::create_directory(exhausted);
    store(exhausted / "REC99999.WAV", {'k','e','e','p'});
    assert(!writer.open(exhausted) && writer.error().find("exhausted") != std::string::npos);
    assert(bytes(exhausted / "REC99999.WAV") == std::vector<uint8_t>({'k','e','e','p'}));
    checked("counter exhaustion is explicit and never wraps or overwrites");

    assert(writer.open(root / "RETRY"));
    assert(!writer.append(nullptr, 1) && !writer.finish());
    assert(writer.open(root / "RETRY"));
    assert(writer.append(nullptr, 0) && writer.finish());
    checked("invalid append arguments fail safely and a new recording can recover");
    assert(bytes(userFile) == userBefore && bytes(directory / "REC00001.WAV") == originalBefore);
    std::cout << checks << " real-filesystem recording storage checks passed\n";
}
