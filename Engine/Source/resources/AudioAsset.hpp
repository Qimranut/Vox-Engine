#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace vox::audioasset {
constexpr size_t maxBytes = 256ull * 1024 * 1024;
constexpr unsigned version = 1;
struct Info { uint64_t hash{}, size{}; unsigned durationMs{}, sampleRate{}, channels{}; };
inline uint64_t fingerprint(std::span<const unsigned char> bytes) {
    uint64_t h = 14695981039346656037ull;
    for (auto b : bytes) { h ^= b; h *= 1099511628211ull; }
    return h;
}
inline std::vector<unsigned char> read(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (!size || size > maxBytes) throw std::runtime_error("Audio file is empty or exceeds 256 MiB: " + path.string());
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read audio: " + path.string());
    return bytes;
}
inline std::filesystem::path cookedPath(const std::filesystem::path& root, const std::string& key) {
    return root / ".cooked" / key;
}
inline std::filesystem::path metadataPath(std::filesystem::path path) { path += ".audioinfo"; return path; }
inline bool readInfo(const std::filesystem::path& path, Info& info) {
    std::ifstream in(path);
    std::string magic; unsigned v{};
    if (!(in >> magic >> v >> info.hash >> info.size >> info.durationMs >> info.sampleRate >> info.channels)) return false;
    in >> std::ws;
    return in.eof() && magic == "VOXAUDIO" && v == version && info.size > 0 && info.size <= maxBytes &&
        info.durationMs > 0 && info.sampleRate > 0 && info.channels > 0 && info.channels <= 32;
}
inline bool matches(const Info& info, std::span<const unsigned char> bytes) {
    return info.size == bytes.size() && info.hash == fingerprint(bytes);
}
}
