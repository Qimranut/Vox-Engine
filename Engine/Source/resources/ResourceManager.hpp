/*************************************************************************************************************
\file       ResourceManager.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      CPU-side asset types (Resource, MeshData, PixelFormat) and ResourceManager, which loads and
            caches assets relative to the asset root, reading textures and models only from their
            cooked DDS / .vmdl files. Also the DDS and .vmdl parsers.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "AudioAsset.hpp"
#include "FontAsset.hpp"

namespace vox {
enum class ResourceKind { Binary, Texture, Model, Audio, Font };
// Texel layout of Resource::bytes. GL-free on purpose: graphics maps it to an internal format.
// Every file-backed texture is BCn from a DDS (TextureCompiler cooks PNG/JPG/TGA/BMP/HDR to one).
// RGBA8 and RGBA16F are only for textures built in code, such as the white and missing fallbacks.
//   BC1  4bpp RGB + 1-bit alpha      BC3  8bpp RGBA          BC7  8bpp RGBA, best quality
//   BC4  4bpp one channel (masks)    BC5  8bpp two channels (normal X,Y)
//   BC6H 8bpp RGB half float (HDR), unsigned or signed
enum class PixelFormat { Unknown, RGBA8, RGBA16F, BC1, BC3, BC4, BC5, BC6H_UF16, BC6H_SF16, BC7 };
// One mip level inside Resource::bytes. Level 0 is the full-size image.
struct MipLevel {
    size_t offset{}, size{};
    int width{}, height{};
};
struct MeshData {
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uv; // top-left origin (v = 0 is the image's first row), matching every texture
    std::vector<unsigned> indices;
};
// CPU resources: GPU objects and FMOD voices remain the responsibility of their systems.
// Textures keep the file's row order: row 0 is the TOP of the image. Nothing is flipped,
// because block-compressed data cannot be; mesh UVs use the same top-left origin instead.
struct Resource {
    ResourceKind kind{};
    std::string path;
    std::vector<unsigned char> bytes;
    std::vector<MeshData> meshes;
    int width{}, height{};
    PixelFormat format{};
    std::vector<MipLevel> mips;  // textures only; exactly one level unless the file stores a chain
    int sourceChannels{};        // channels the texture format carries (BC4 1, BC5 2, BC6H 3, else 4)
    bool srgbHint{};             // the file declares its colour data sRGB (DDS DXGI *_SRGB)
    std::string cookedPath;      // asset-relative cooked file the data came from; empty for a .dds loaded directly
    /*!
        @brief
            CPU memory held by the resource's texel bytes and mesh arrays.
        @return
            Size in bytes
        */
    size_t memoryBytes() const;
    audioasset::Info audioInfo;
    std::unique_ptr<fontasset::Font> font;
    std::string audioCookedPath; // Empty when loading the original, unvalidated file.
};
/*!
    @brief
        Display name of a pixel format.
    @param format
        The pixel format
    @return
        A static string such as "BC7", or "Unknown"
    */
const char* pixelFormatName(PixelFormat format);
/*!
    @brief
        Whether a pixel format is one of the BCn block-compressed formats.
    @param format
        The pixel format
    @return
        true for BC1, BC3, BC4, BC5, BC6H and BC7
    */
bool isBlockCompressed(PixelFormat format);
/*!
    @brief
        Uncompressed size of a texture's mip chain, for reporting the compression ratio.
    @param image
        A texture resource
    @return
        Bytes the same mip chain would take uncompressed (RGBA8, or RGBA16F for HDR formats), for
        reporting the compression ratio against Resource::bytes
    */
size_t uncompressedEquivalentBytes(const Resource& image);
/*!
    @brief
        Parses a 2D DDS file (legacy FourCC or DX10 header) holding BC1, BC3, BC4, BC5, BC6H or BC7 data.

        Throws std::runtime_error on anything unsupported or malformed: cubemaps, volumes, arrays,
        premultiplied DXT2/DXT4, a mip count the size cannot hold, or a truncated level.
    @param data
        Whole file contents
    @param out
        Receives bytes, width, height, format, mips and srgbHint
    */
void parseDds(std::span<const unsigned char> data, Resource& out);
/*!
    @brief
        Parses a cooked model (.vmdl) written by Tools/ModelCompiler.

        Throws std::runtime_error on a wrong magic or version, a truncated or oversized mesh, an index
        past its mesh's vertices, or trailing bytes.
    @param data
        Whole file contents
    @param out
        Receives meshes
    */
void parseModel(std::span<const unsigned char> data, Resource& out);
using ResourceHandle = std::shared_ptr<const Resource>;
class ResourceManager {
public:
    /*!
        @brief
            Creates a manager rooted at the (canonicalised) asset folder.
        @param root
            Asset root directory
        */
    explicit ResourceManager(std::filesystem::path root);
    /*!
        @brief
            Returns the cached resource for a path, reading it on a cache miss.
        @param path
            Asset path, relative to the root
        @return
            Shared handle to the resource; throws on failure
        */
    ResourceHandle load(const std::string& path);
    /*!
        @brief
            Reads a resource again and replaces the cached entry.

            A failed reload throws and leaves the cached version and live handles untouched.
        @param path
            Asset path, relative to the root
        @return
            Shared handle to the fresh resource
        */
    ResourceHandle reload(const std::string& path);
    /*!
        @brief
            Drops every cached resource no one else holds a handle to.
        */
    void collectUnused();
    /*!
        @brief
            CPU memory held by every cached resource.
        @return
            Total size in bytes
        */
    size_t memoryBytes() const;
    /*!
        @brief
            The cache, keyed by asset-relative path.
        @return
            The cache map
        */
    const auto& entries() const { return cache_; }
    /*!
        @brief
            The canonical asset root.
        @return
            The root path
        */
    const auto& root() const { return root_; }
    /*!
        @brief
            Resolves an asset path to a full path, refusing anything outside the root.
        @param path
            Asset path, relative to the root
        @return
            Canonical full path; throws when empty or outside the root
        */
    std::filesystem::path resolve(const std::string& path) const;
    /*!
        @brief
            Converts a full path to an asset-relative key, checking it lies inside the root.
        @param path
            Full or relative filesystem path
        @return
            Generic asset-relative path; throws when outside the root
        */
    std::string relative(const std::filesystem::path& path) const;
    /*!
        @brief
            Where TextureCompiler writes the compressed version of a source image, or ModelCompiler
            the cooked version of a source model.
        @param key
            Asset-relative source path, e.g. "textures/foo.png" or "models/bar.fbx"
        @return
            <root>/.cooked/textures/foo.png.dds, or <root>/.cooked/models/bar.fbx.vmdl. The full source
            name is kept so foo.png and foo.tga never share an output.
        */
    std::filesystem::path cookedPathFor(const std::string& key) const;
private:
    /*!
        @brief
            Reads and parses one asset from disk (cooked file for images and models).
        @param key
            Asset-relative path
        @return
            The new resource; throws on any failure
        */
    ResourceHandle read(const std::string& key) const;
    std::filesystem::path root_;
    std::unordered_map<std::string, ResourceHandle> cache_;
};
}
