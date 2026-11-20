/*************************************************************************************************************
\file       ResourceManager.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements ResourceManager (path resolution, cooked-file lookup, loading and caching) and
            the DDS and .vmdl parsers the runtime reads textures and models through.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "ResourceManager.hpp"
#include "profile/profiler.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace vox {
namespace {
// Largest edge accepted from any image file. GL_MAX_TEXTURE_SIZE is checked again at upload;
// this bound keeps every size computation below far from overflow.
constexpr uint32_t kMaxTextureEdge = 16384;
// TextureCompiler and ModelCompiler mirror the asset tree under this folder. Matches Tools/.
constexpr const char* kCookedFolder = ".cooked";
// Cooked model file: "VMDL", version, mesh count, then per mesh vertex and index counts followed by
// positions, normals, UVs (float) and indices (uint32). Matches Tools/ModelCompiler.
constexpr uint32_t kModelVersion = 1;
// Bounds for one cooked mesh; ModelCompiler rejects anything larger.
constexpr uint32_t kMaxModelMeshes = 4096, kMaxMeshVertices = 1u << 24, kMaxMeshIndices = 1u << 26;

/*!
    @brief
        Whether an extension is a source image TextureCompiler cooks.
    @param ext
        Lower-case extension including the dot
    @return
        true for .png, .jpg, .jpeg, .bmp, .tga and .hdr
    */
bool isSourceImage(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
}
/*!
    @brief
        Whether an extension is a source model ModelCompiler cooks.
    @param ext
        Lower-case extension including the dot
    @return
        true for .obj, .fbx, .gltf, .glb and .dae
    */
bool isSourceModel(const std::string& ext) {
    return ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae";
}
/*!
    @brief
        A path's extension in lower case.
    @param path
        File path
    @return
        The extension including the dot, lower-cased
    */
std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/*!
    @brief
        Whether a cooked file may be used for its source.

        A cooked file is used only when it is at least as new as its source and the source's optional
        .texmeta import settings, so an edited PNG or model is never shadowed by a stale cook.
    @param source
        Full path of the source asset
    @param cooked
        Full path of its cooked file
    @return
        true when the cook exists and is up to date
    */
bool isCookedUpToDate(const std::filesystem::path& source, const std::filesystem::path& cooked) {
    std::error_code ec;
    const auto cookedTime = std::filesystem::last_write_time(cooked, ec);
    if (ec) return false;
    const auto sourceTime = std::filesystem::last_write_time(source, ec);
    if (ec || cookedTime < sourceTime) return false;
    auto meta = source; meta += ".texmeta";
    const auto metaTime = std::filesystem::last_write_time(meta, ec);
    return ec || cookedTime >= metaTime; // no sidecar is fine
}

/*!
    @brief
        Reads a whole file into memory.
    @param full
        Full path of the file
    @param key
        Asset key used in error messages
    @return
        The file's bytes; throws when it cannot be read
    */
std::vector<unsigned char> readFile(const std::filesystem::path& full, const std::string& key) {
    std::ifstream in(full, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read asset: " + key);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(in), {});
    if (in.bad()) throw std::runtime_error("Error reading asset: " + key);
    return bytes;
}

/*!
    @brief
        Reads a little-endian 32-bit value at an offset (unchecked).
    @param data
        Buffer to read from
    @param offset
        Byte offset of the value
    @return
        The value
    */
uint32_t readU32(std::span<const unsigned char> data, size_t offset) {
    uint32_t v{}; std::memcpy(&v, data.data() + offset, sizeof(v)); return v; // DDS is little-endian, as is x64
}
/*!
    @brief
        Packs four characters into a little-endian FourCC code.
    @param a
        First character
    @param b
        Second character
    @param c
        Third character
    @param d
        Fourth character
    @return
        The FourCC as a 32-bit value
    */
constexpr uint32_t fourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
}
} // namespace

/*!
    @brief
        Display name of a pixel format.
    @param format
        The pixel format
    @return
        A static string such as "BC7", or "Unknown"
    */
const char* pixelFormatName(PixelFormat format) {
    switch (format) {
    case PixelFormat::RGBA8: return "RGBA8";
    case PixelFormat::RGBA16F: return "RGBA16F";
    case PixelFormat::BC1: return "BC1";
    case PixelFormat::BC3: return "BC3";
    case PixelFormat::BC4: return "BC4";
    case PixelFormat::BC5: return "BC5";
    case PixelFormat::BC6H_UF16: return "BC6H_UF16";
    case PixelFormat::BC6H_SF16: return "BC6H_SF16";
    case PixelFormat::BC7: return "BC7";
    case PixelFormat::Unknown:
    default: return "Unknown";
    }
}

/*!
    @brief
        Whether a pixel format is one of the BCn block-compressed formats.
    @param format
        The pixel format
    @return
        true for BC1, BC3, BC4, BC5, BC6H and BC7
    */
bool isBlockCompressed(PixelFormat format) {
    return format == PixelFormat::BC1 || format == PixelFormat::BC3 || format == PixelFormat::BC4
        || format == PixelFormat::BC5 || format == PixelFormat::BC6H_UF16 || format == PixelFormat::BC6H_SF16
        || format == PixelFormat::BC7;
}

/*!
    @brief
        Uncompressed size of a texture's mip chain (8 bytes per texel for HDR, else 4).
    @param image
        A texture resource
    @return
        Bytes the mip chain would take uncompressed
    */
size_t uncompressedEquivalentBytes(const Resource& image) {
    const bool hdr = image.format == PixelFormat::RGBA16F || image.format == PixelFormat::BC6H_UF16
        || image.format == PixelFormat::BC6H_SF16;
    const size_t bytesPerTexel = hdr ? 8 : 4;
    size_t n{};
    for (const auto& mip : image.mips) n += static_cast<size_t>(mip.width) * static_cast<size_t>(mip.height) * bytesPerTexel;
    return n;
}

/*!
    @brief
        Parses a 2D BCn DDS file (legacy FourCC or DX10 header) into a texture resource.
    @param data
        Whole file contents
    @param out
        Receives bytes, width, height, format, mips, srgbHint and sourceChannels
    */
void parseDds(std::span<const unsigned char> data, Resource& out) {
    // Layout: "DDS " magic, 124-byte DDS_HEADER, optional 20-byte DDS_HEADER_DXT10, then every
    // level of the mip chain largest first. Offsets below are from the start of the file.
    constexpr size_t kHeader = 4 + 124, kDx10 = 20;
    constexpr uint32_t kFlagMipCount = 0x20000, kFlagDepth = 0x800000, kPfFourCC = 0x4;
    constexpr uint32_t kCaps2Cubemap = 0x200, kCaps2Volume = 0x200000, kDx10MiscCubemap = 0x4;
    constexpr uint32_t kDimensionTexture2D = 3;

    if (data.size() < kHeader || readU32(data, 0) != fourCC('D', 'D', 'S', ' '))
        throw std::runtime_error("Not a DDS file.");
    if (readU32(data, 4) != 124 || readU32(data, 4 + 72) != 32)
        throw std::runtime_error("DDS header size is invalid.");

    const uint32_t flags = readU32(data, 4 + 4);
    const uint32_t height = readU32(data, 4 + 8);
    const uint32_t width = readU32(data, 4 + 12);
    const uint32_t depth = readU32(data, 4 + 20);
    const uint32_t mipField = readU32(data, 4 + 24);
    const uint32_t pfFlags = readU32(data, 4 + 76);
    const uint32_t pfFourCC = readU32(data, 4 + 80);
    const uint32_t caps2 = readU32(data, 4 + 108);

    if ((caps2 & (kCaps2Cubemap | kCaps2Volume)) || ((flags & kFlagDepth) && depth > 1))
        throw std::runtime_error("DDS cubemaps and volume textures are not supported.");
    if (width == 0 || height == 0 || width > kMaxTextureEdge || height > kMaxTextureEdge)
        throw std::runtime_error("DDS dimensions unsupported.");
    if (!(pfFlags & kPfFourCC))
        throw std::runtime_error("Uncompressed DDS is not supported; use BC1, BC3, BC4, BC5, BC6H or BC7.");

    PixelFormat format = PixelFormat::Unknown;
    bool srgb = false;
    size_t dataStart = kHeader;
    switch (pfFourCC) {
    case fourCC('D', 'X', 'T', '1'): format = PixelFormat::BC1; break;
    case fourCC('D', 'X', 'T', '5'): format = PixelFormat::BC3; break;
    case fourCC('A', 'T', 'I', '1'): case fourCC('B', 'C', '4', 'U'): format = PixelFormat::BC4; break;
    case fourCC('A', 'T', 'I', '2'): case fourCC('B', 'C', '5', 'U'): format = PixelFormat::BC5; break;
    case fourCC('D', 'X', 'T', '2'): case fourCC('D', 'X', 'T', '4'):
        throw std::runtime_error("Premultiplied-alpha DDS (DXT2/DXT4) is not supported.");
    case fourCC('D', 'X', '1', '0'): {
        if (data.size() < kHeader + kDx10) throw std::runtime_error("DDS DX10 header is truncated.");
        const uint32_t dxgi = readU32(data, kHeader);
        if (readU32(data, kHeader + 4) != kDimensionTexture2D || (readU32(data, kHeader + 8) & kDx10MiscCubemap)
            || readU32(data, kHeader + 12) != 1)
            throw std::runtime_error("Only single 2D DDS textures are supported (no arrays or cubemaps).");
        switch (dxgi) {
        case 71: format = PixelFormat::BC1; break;
        case 72: format = PixelFormat::BC1; srgb = true; break;
        case 77: format = PixelFormat::BC3; break;
        case 78: format = PixelFormat::BC3; srgb = true; break;
        case 80: format = PixelFormat::BC4; break;
        case 83: format = PixelFormat::BC5; break;
        case 95: format = PixelFormat::BC6H_UF16; break;
        case 96: format = PixelFormat::BC6H_SF16; break;
        case 98: format = PixelFormat::BC7; break;
        case 99: format = PixelFormat::BC7; srgb = true; break;
        default: throw std::runtime_error("Unsupported DDS DXGI format " + std::to_string(dxgi) + ".");
        }
        dataStart += kDx10;
        break;
    }
    default: throw std::runtime_error("Unsupported DDS FourCC.");
    }

    uint32_t fullChain = 1;
    for (uint32_t edge = std::max(width, height); edge > 1; edge >>= 1) ++fullChain;
    // A zero count, or the flag missing, means the file holds only the base level.
    const uint32_t levels = (flags & kFlagMipCount) && mipField > 0 ? mipField : 1;
    if (levels > fullChain) throw std::runtime_error("DDS declares more mip levels than its size allows.");

    // dwPitchOrLinearSize is unreliable across exporters and ignored; sizes derive from the format.
    const uint64_t blockBytes = (format == PixelFormat::BC1 || format == PixelFormat::BC4) ? 8 : 16;
    std::vector<MipLevel> mips;
    uint64_t cursor = 0;
    for (uint32_t i = 0; i < levels; ++i) {
        const uint32_t w = std::max<uint32_t>(1, width >> i), h = std::max<uint32_t>(1, height >> i);
        const uint64_t size = uint64_t(std::max<uint32_t>(1, (w + 3) / 4)) * std::max<uint32_t>(1, (h + 3) / 4) * blockBytes;
        mips.push_back(MipLevel{ static_cast<size_t>(cursor), static_cast<size_t>(size), static_cast<int>(w), static_cast<int>(h) });
        cursor += size;
    }
    if (data.size() - dataStart < cursor) throw std::runtime_error("DDS image data is truncated.");

    out.bytes.assign(data.begin() + dataStart, data.begin() + dataStart + static_cast<size_t>(cursor));
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.format = format;
    out.mips = std::move(mips);
    out.srgbHint = srgb;
    out.sourceChannels = format == PixelFormat::BC4 ? 1 : format == PixelFormat::BC5 ? 2
        : (format == PixelFormat::BC6H_UF16 || format == PixelFormat::BC6H_SF16) ? 3 : 4;
}

/*!
    @brief
        Parses a cooked .vmdl model, bounds-checking every count and index.
    @param data
        Whole file contents
    @param out
        Receives meshes
    */
void parseModel(std::span<const unsigned char> data, Resource& out) {
    size_t cursor = 0;
    const auto take = [&](void* dst, uint64_t bytes) {
        if (bytes > data.size() - cursor) throw std::runtime_error("Cooked model is truncated.");
        std::memcpy(dst, data.data() + cursor, static_cast<size_t>(bytes));
        cursor += static_cast<size_t>(bytes);
    };
    const auto u32 = [&] { uint32_t v{}; take(&v, sizeof(v)); return v; };

    if (u32() != fourCC('V', 'M', 'D', 'L')) throw std::runtime_error("Not a cooked model file.");
    if (const uint32_t version = u32(); version != kModelVersion)
        throw std::runtime_error("Cooked model version " + std::to_string(version) + " is not supported; rebuild CookAssets.");
    const uint32_t meshCount = u32();
    if (meshCount == 0 || meshCount > kMaxModelMeshes) throw std::runtime_error("Cooked model mesh count is invalid.");

    std::vector<MeshData> meshes(meshCount);
    for (auto& m : meshes) {
        const uint32_t vertices = u32(), indices = u32();
        if (vertices == 0 || vertices > kMaxMeshVertices || indices % 3 != 0 || indices > kMaxMeshIndices)
            throw std::runtime_error("Cooked mesh size is invalid.");
        m.positions.resize(vertices); m.normals.resize(vertices); m.uv.resize(vertices); m.indices.resize(indices);
        take(m.positions.data(), uint64_t(vertices) * sizeof(glm::vec3));
        take(m.normals.data(), uint64_t(vertices) * sizeof(glm::vec3));
        take(m.uv.data(), uint64_t(vertices) * sizeof(glm::vec2));
        take(m.indices.data(), uint64_t(indices) * sizeof(uint32_t));
        for (const uint32_t i : m.indices)
            if (i >= vertices) throw std::runtime_error("Cooked mesh index is out of range.");
    }
    if (cursor != data.size()) throw std::runtime_error("Cooked model has trailing bytes.");
    out.meshes = std::move(meshes);
}

/*!
    @brief
        CPU memory held by the resource's texel bytes and mesh arrays.
    @return
        Size in bytes
    */
size_t Resource::memoryBytes() const {
    size_t n = bytes.size();
    if (font) n += sizeof(fontasset::Font) + font->pixels.size();
    for (const auto& m : meshes) n += (m.positions.size() + m.normals.size()) * sizeof(glm::vec3)
        + m.uv.size() * sizeof(glm::vec2) + m.indices.size() * sizeof(unsigned);
    return n;
}
/*!
    @brief
        Creates a manager rooted at the canonicalised asset folder.
    @param root
        Asset root directory
    */
ResourceManager::ResourceManager(std::filesystem::path root) : root_(std::filesystem::weakly_canonical(root)) {}
/*!
    @brief
        Resolves an asset path to a canonical full path inside the root.
    @param path
        Asset path, relative to the root
    @return
        Canonical full path; throws when empty or outside the root
    */
std::filesystem::path ResourceManager::resolve(const std::string& path) const {
    if (path.empty()) throw std::runtime_error("Asset path is empty.");
    auto full = std::filesystem::weakly_canonical(root_ / std::filesystem::path(path));
    auto rel = full.lexically_relative(root_);
    if (rel.empty() || rel.is_absolute() || *rel.begin() == "..")
        throw std::runtime_error("Asset must be inside the project assets folder: " + path);
    return full;
}
/*!
    @brief
        Converts a path to an asset-relative key and checks it lies inside the root.
    @param path
        Full or relative filesystem path
    @return
        Generic asset-relative path; throws when outside the root
    */
std::string ResourceManager::relative(const std::filesystem::path& path) const {
    auto p = std::filesystem::weakly_canonical(path).lexically_relative(root_).generic_string();
    resolve(p);
    return p;
}
/*!
    @brief
        Path of the cooked file for a source asset (.vmdl for models, .dds otherwise).
    @param key
        Asset-relative source path
    @return
        <root>/.cooked/<key>.vmdl or <root>/.cooked/<key>.dds, normalised
    */
std::filesystem::path ResourceManager::cookedPathFor(const std::string& key) const {
    auto path = root_ / kCookedFolder / std::filesystem::path(key);
    const auto ext = lowerExtension(key);
    path += (ext == ".ttf" || ext == ".otf") ? ".vfont" : isSourceModel(ext) ? ".vmdl" : ".dds";
    return path.lexically_normal();
}
/*!
    @brief
        Reads and parses one asset: images and models from their up-to-date cook,
        .dds directly, anything else as raw bytes (Audio for .wav/.mp3).
    @param key
        Asset-relative path
    @return
        The new resource; throws on any failure
    */
ResourceHandle ResourceManager::read(const std::string& key) const {
    // Only cache misses get here, so every one of these zones is real disk and parse time.
    VOX_PROFILE_SCOPE_C("Load asset", profile::colour::kResource);
    VOX_PROFILE_TEXT(key.data(), key.size());
    const auto full = resolve(key);
    if (!std::filesystem::is_regular_file(full)) throw std::runtime_error("Asset not found: " + key);
    if (std::filesystem::file_size(full) > 256ull * 1024 * 1024) throw std::runtime_error("Asset exceeds the 256 MiB import limit: " + key);
    auto r = std::make_shared<Resource>(); r->path = key;
    const std::string ext = lowerExtension(full);
    if (isSourceImage(ext) || isSourceModel(ext)) {
        const bool model = isSourceModel(ext);
        r->kind = model ? ResourceKind::Model : ResourceKind::Texture;
        // The runtime has no image decoder or model importer: those live in the offline
        // TextureCompiler/ModelCompiler. r->path stays the source key, so caches, texture
        // handles and scene files never see the cooked name.
        const auto cooked = cookedPathFor(key);
        if (!isCookedUpToDate(full, cooked))
            throw std::runtime_error("Asset is not cooked, or its cook is out of date (build CookAssets): " + key);
        const auto cookedKey = relative(cooked);
        const auto file = readFile(cooked, cookedKey);
        try { model ? parseModel(file, *r) : parseDds(file, *r); }
        catch (const std::exception& e) { throw std::runtime_error("Cannot load " + cookedKey + ": " + e.what()); }
        r->cookedPath = cookedKey;
    } else if (ext == ".ttf" || ext == ".otf") {
        const auto cooked = cookedPathFor(key);
        if (!isCookedUpToDate(full, cooked))
            throw std::runtime_error("Font is not cooked or is out of date (build CookAssets): " + key);
        r->kind = ResourceKind::Font;
        r->font = std::make_unique<fontasset::Font>(fontasset::read(cooked));
        r->cookedPath = relative(cooked);
    } else if (ext == ".dds") {
        r->kind = ResourceKind::Texture;
        const auto file = readFile(full, key);
        try { parseDds(file, *r); }
        catch (const std::exception& e) { throw std::runtime_error("Cannot load " + key + ": " + e.what()); }
    } else if (ext == ".wav" || ext == ".mp3") {
        r->kind = ResourceKind::Audio;
        r->bytes = audioasset::read(full);
        const auto cooked = audioasset::cookedPath(root_, key);
        audioasset::Info info;
        if (audioasset::readInfo(audioasset::metadataPath(cooked), info) && audioasset::matches(info, r->bytes)) {
            try {
                auto packaged = audioasset::read(cooked);
                if (audioasset::matches(info, packaged)) {
                    r->bytes = std::move(packaged);
                    r->audioInfo = info;
                    r->audioCookedPath = cooked.lexically_relative(root_).generic_string();
                }
            } catch (const std::exception&) { /* Source remains usable if generated output is missing. */ }
        }
        if (r->audioCookedPath.empty())
            std::clog << "[Audio] Source fallback: " << key << " (build CookAudio to validate/package)\n";
        else std::clog << "[Audio] Loaded cooked: " << r->audioCookedPath << '\n';
    } else {
        r->kind = ResourceKind::Binary;
        std::ifstream in(full, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot read asset: " + key);
        r->bytes.assign(std::istreambuf_iterator<char>(in), {});
        if (in.bad()) throw std::runtime_error("Error reading asset: " + key);
    }
    return r;
}
/*!
    @brief
        Returns the cached resource for a path, reading it on a cache miss.
    @param path
        Asset path, relative to the root
    @return
        Shared handle to the resource
    */
ResourceHandle ResourceManager::load(const std::string& path) {
    const auto key = relative(resolve(path));
    if (const auto it = cache_.find(key); it != cache_.end()) return it->second;
    auto resource = read(key); cache_.emplace(key, resource); return resource;
}
/*!
    @brief
        Reads a resource again and replaces the cached entry; a failure keeps the old one.
    @param path
        Asset path, relative to the root
    @return
        Shared handle to the fresh resource
    */
ResourceHandle ResourceManager::reload(const std::string& path) {
    const auto key = relative(resolve(path));
    auto resource = read(key); // Failed reload preserves the cached version and all live handles.
    cache_[key] = resource; return resource;
}
/*!
    @brief
        Drops every cached resource whose only handle is the cache's own.
    */
void ResourceManager::collectUnused() {
    std::erase_if(cache_, [](const auto& entry) { return entry.second.use_count() == 1; });
}
/*!
    @brief
        CPU memory held by every cached resource.
    @return
        Total size in bytes
    */
size_t ResourceManager::memoryBytes() const {
    size_t n{}; for (const auto& [key, r] : cache_) n += r->memoryBytes(); return n;
}
}
