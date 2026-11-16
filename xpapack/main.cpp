// xpapack for the PeOS port of Rift Ascend (GPL-3.0, as Rift Ascend): the
// game's extracted files (league-assets/<Name>.wad/...) packed into one XPA
// archive per WAD directory (<out>/<Name>.wad.xpa), which the client and the
// server mount instead of the loose files (fmt::vfs::mount_assets). Rift
// Ascend's CMake names tools/xpapack/main.cpp, which its published sources
// leave out: build.sh copies this file there.
//
// 400 files instead of 900,000: on a USB stick or through PeOS's exFAT every
// file costs a lookup and a command, 75 files a second (2026-11-14).
//
//   xpapack <league-assets> <out> [--level N] [--jobs N] [--max-part MB] [--only Name.wad,...]
//   xpapack --list <archive.xpa>          the entries, to check one
//   xpapack --verify <archive.xpa>        every entry read and its hash checked
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fmt/archive/xpa.h"

namespace fs = std::filesystem;

namespace {

bool readFile(const fs::path& path, std::vector<std::byte>& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) return false;
    in.seekg(0);
    data.resize(size_t(size));
    return size == 0 || bool(in.read(reinterpret_cast<char*>(data.data()), size));
}

// One WAD directory into <out>/<mount>.xpa, in pieces prepared in parallel
// (compression) and appended in name order. Past max_part bytes the rest goes
// on in patch layers, <mount>.p0001.xpa, .p0002... (Rift Ascend mounts them
// all at the same directory): PeOS's XFS keeps a file's extents in its inode,
// about 20, one at least per 96 MB group, so a file stays under ~1 GB there
// (2026-11-15). 0: one archive whatever its size.
bool packOne(const fs::path& directory, const fs::path& out, const fmt::xpa::WriteOptions& options,
             unsigned jobs, uint64_t max_part) {
    const std::string mount = directory.filename().string();
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(directory))
        if (entry.is_regular_file()) files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    uint32_t layer = 0;
    fs::path target = out / fmt::xpa::archive_file_name(mount, layer);
    auto writer = std::make_unique<fmt::xpa::Writer>();
    std::string error;
    if (!writer->begin(target, options, &error)) {
        std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
        return false;
    }
    const auto report = [&](const fmt::xpa::Writer& w, const fs::path& t) {
        const auto& s = w.statistics();
        std::printf("xpapack: %s: %zu files, %.1f MB -> %.1f MB\n", t.filename().string().c_str(), s.entries,
                    double(s.uncompressed_size) / 1e6, double(s.written_bytes) / 1e6);
        std::fflush(stdout);
    };
    const size_t piece = 512;
    for (size_t start = 0; start < files.size(); start += piece) {
        const size_t end = std::min(files.size(), start + piece);
        std::vector<fmt::xpa::PreparedEntry> prepared(end - start);
        std::atomic<size_t> next{start};
        std::atomic<bool> failed{false};
        std::vector<std::thread> workers;
        for (unsigned j = 0; j < jobs; ++j)
            workers.emplace_back([&] {
                std::vector<std::byte> data;
                for (size_t i = next++; i < end; i = next++) {
                    if (!readFile(files[i], data)) {
                        std::fprintf(stderr, "xpapack: cannot read %s\n", files[i].string().c_str());
                        failed = true;
                        continue;
                    }
                    const std::string name =
                        fmt::xpa::normalize_name(fs::relative(files[i], directory).generic_string());
                    prepared[i - start] = fmt::xpa::prepare(name, data, options);
                }
            });
        for (std::thread& worker : workers) worker.join();
        if (failed) {
            writer->abort();
            return false;
        }
        for (fmt::xpa::PreparedEntry& entry : prepared) {
            // this layer full: finished, the next one begun
            if (max_part && writer->statistics().entries > 0 &&
                writer->statistics().written_bytes + entry.stored_data.size() > max_part) {
                if (!writer->finish(&error)) {
                    std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
                    return false;
                }
                report(*writer, target);
                writer = std::make_unique<fmt::xpa::Writer>();
                target = out / fmt::xpa::archive_file_name(mount, ++layer);
                if (!writer->begin(target, options, &error)) {
                    std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
                    return false;
                }
            }
            if (!writer->add(std::move(entry), &error)) {
                std::fprintf(stderr, "xpapack: %s: %s\n", mount.c_str(), error.c_str());
                writer->abort();
                return false;
            }
        }
    }
    if (!writer->finish(&error)) {
        std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
        return false;
    }
    report(*writer, target);
    return true;
}

int listOrVerify(const fs::path& path, bool verify) {
    fmt::xpa::Archive archive;
    std::string error;
    if (!archive.open(path, &error)) {
        std::fprintf(stderr, "xpapack: %s: %s\n", path.string().c_str(), error.c_str());
        return 1;
    }
    size_t bad = 0;
    std::vector<std::byte> data;
    for (const fmt::xpa::Entry& entry : archive.entries()) {
        if (verify) {
            if (!archive.read(entry, data, &error, true) || data.size() != entry.uncompressed_size) {
                std::fprintf(stderr, "xpapack: %.*s: %s\n", int(archive.name(entry).size()),
                             archive.name(entry).data(), error.c_str());
                ++bad;
            }
        } else {
            std::printf("%10u %.*s\n", entry.uncompressed_size, int(archive.name(entry).size()),
                        archive.name(entry).data());
        }
    }
    if (verify)
        std::printf("xpapack: %s: %zu entries, %zu bad\n", path.filename().string().c_str(), archive.entries().size(),
                    bad);
    return bad ? 1 : 0;
}

// An archive already made, its entries as they are stored (no compression
// again), into <out>/<mount>.xpa and patch layers of at most max_part bytes.
int splitArchive(const fs::path& path, const fs::path& out, uint64_t max_part) {
    fmt::xpa::Archive archive;
    std::string error;
    if (!archive.open(path, &error)) {
        std::fprintf(stderr, "xpapack: %s: %s\n", path.string().c_str(), error.c_str());
        return 1;
    }
    fmt::xpa::ArchiveName name;
    if (!fmt::xpa::parse_archive_name(path.filename().string(), name) || name.layer != 0) {
        std::fprintf(stderr, "xpapack: %s: not a base archive (<Name>.wad.xpa)\n", path.string().c_str());
        return 1;
    }
    fs::create_directories(out);
    for (const fs::path& old : fmt::xpa::find_layers(out, name.mount)) fs::remove(old);
    fmt::xpa::WriteOptions options;
    uint32_t layer = 0;
    fs::path target = out / fmt::xpa::archive_file_name(name.mount, layer);
    auto writer = std::make_unique<fmt::xpa::Writer>();
    if (!writer->begin(target, options, &error)) {
        std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
        return 1;
    }
    const auto report = [&] {
        const auto& s = writer->statistics();
        std::printf("xpapack: %s: %zu files, %.1f MB\n", target.filename().string().c_str(), s.entries,
                    double(s.written_bytes) / 1e6);
        std::fflush(stdout);
    };
    std::vector<std::byte> stored;
    for (const fmt::xpa::Entry& entry : archive.entries()) {
        if (!archive.read_raw(entry, stored, &error)) {
            std::fprintf(stderr, "xpapack: %.*s: %s\n", int(archive.name(entry).size()), archive.name(entry).data(),
                         error.c_str());
            writer->abort();
            return 1;
        }
        if (max_part && writer->statistics().entries > 0 &&
            writer->statistics().written_bytes + stored.size() > max_part) {
            if (!writer->finish(&error)) {
                std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
                return 1;
            }
            report();
            writer = std::make_unique<fmt::xpa::Writer>();
            target = out / fmt::xpa::archive_file_name(name.mount, ++layer);
            if (!writer->begin(target, options, &error)) {
                std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
                return 1;
            }
        }
        if (!writer->add_raw(archive.name(entry), entry, std::move(stored), &error)) {
            std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
            writer->abort();
            return 1;
        }
        stored.clear();
    }
    if (!writer->finish(&error)) {
        std::fprintf(stderr, "xpapack: %s: %s\n", target.string().c_str(), error.c_str());
        return 1;
    }
    report();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // xpapack --split <archive.xpa> <out> [--max-part MB]
    if (argc >= 4 && std::string(argv[1]) == "--split") {
        uint64_t max_part = 768ull << 20;
        if (argc >= 6 && std::string(argv[4]) == "--max-part")
            max_part = uint64_t(std::max(0, std::atoi(argv[5]))) << 20;
        return splitArchive(argv[2], argv[3], max_part);
    }
    if (argc == 3 && (std::string(argv[1]) == "--list" || std::string(argv[1]) == "--verify"))
        return listOrVerify(argv[2], std::string(argv[1]) == "--verify");
    // one argument, an archive: verified (PeOS's test systems pass one
    // argument, runarg=: the archive read as the client reads it)
    if (argc == 2) {
        const std::string one = argv[1];
        if (one.size() > 4 && one.substr(one.size() - 4) == ".xpa") return listOrVerify(one, true);
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: xpapack <league-assets> <out> [--level N] [--jobs N] [--max-part MB]\n"
                             "                [--only A.wad,B.wad]   (--max-part 0: no layers; default 768)\n"
                             "       xpapack --list|--verify <archive.xpa>\n");
        return 2;
    }
    const fs::path root = argv[1], out = argv[2];
    fmt::xpa::WriteOptions options;
    options.compression_level = 3;
    unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
    uint64_t max_part = 768ull << 20;   // PeOS's XFS: see packOne
    std::vector<std::string> only;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--level") options.compression_level = std::atoi(value.c_str());
        else if (key == "--jobs") jobs = unsigned(std::max(1, std::atoi(value.c_str())));
        else if (key == "--max-part") max_part = uint64_t(std::max(0, std::atoi(value.c_str()))) << 20;
        else if (key == "--only") {
            for (size_t a = 0, b; a <= value.size(); a = b + 1) {
                b = value.find(',', a);
                if (b == std::string::npos) b = value.size();
                if (b > a) only.push_back(value.substr(a, b - a));
            }
        }
    }
    fs::create_directories(out);
    std::vector<fs::path> directories;
    for (const auto& entry : fs::directory_iterator(root)) {
        const std::string name = entry.path().filename().string();
        if (!entry.is_directory() || name.size() < 5 || name.substr(name.size() - 4) != ".wad") continue;
        if (!only.empty() && std::find(only.begin(), only.end(), name) == only.end()) continue;
        directories.push_back(entry.path());
    }
    std::sort(directories.begin(), directories.end());
    size_t failed = 0, done = 0;
    for (const fs::path& directory : directories) {
        // a run again skips the directories done (all their layers written:
        // the marker comes last), and packs again one left halfway
        const std::string mount = directory.filename().string();
        const fs::path marker = out / ("." + mount + ".done");
        if (fs::exists(marker)) {
            ++done;
            continue;
        }
        for (const fs::path& old : fmt::xpa::find_layers(out, mount)) fs::remove(old);
        if (!packOne(directory, out, options, jobs, max_part)) {
            ++failed;
            continue;
        }
        std::ofstream(marker).put('\n');
        ++done;
    }
    std::printf("xpapack: %zu archives in %s, %zu failed\n", done, out.string().c_str(), failed);
    return failed ? 1 : 0;
}
