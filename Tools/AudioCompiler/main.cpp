#include "resources/AudioAsset.hpp"
#include <Windows.h>
#include <fmod.hpp>
#include <fmod_errors.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>

namespace fs = std::filesystem;
namespace aa = vox::audioasset;
namespace {
void check(FMOD_RESULT result) {
    if (result != FMOD_OK) throw std::runtime_error(FMOD_ErrorString(result));
}
struct SystemDelete { void operator()(FMOD::System* p) const { if (p) p->release(); } };
struct SoundDelete { void operator()(FMOD::Sound* p) const { if (p) p->release(); } };
void replaceFile(const fs::path& temp, const fs::path& destination) {
    if (!MoveFileExW(temp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace cooked file: " + destination.string());
}
bool cook(FMOD::System& system, const fs::path& root, const fs::path& source, bool force) {
    const auto key = source.lexically_relative(root).generic_string();
    const auto bytes = aa::read(source);
    const auto output = aa::cookedPath(root, key);
    const auto metadata = aa::metadataPath(output);
    aa::Info info;
    if (!force && aa::readInfo(metadata, info) && aa::matches(info, bytes)) {
        try {
            if (aa::matches(info, aa::read(output))) { std::cout << "[AudioCompiler] SKIP " << key << '\n'; return false; }
        } catch (const std::exception&) { /* Missing/damaged output is rebuilt. */ }
    }
    FMOD_CREATESOUNDEXINFO extra{}; extra.cbsize = sizeof(extra);
    extra.length = static_cast<unsigned>(bytes.size());
    FMOD::Sound* raw{};
    // Blocking sample creation decodes the file without playing it.
    check(system.createSound(reinterpret_cast<const char*>(bytes.data()),
        FMOD_OPENMEMORY | FMOD_CREATESAMPLE | FMOD_2D, &extra, &raw));
    const std::unique_ptr<FMOD::Sound, SoundDelete> sound(raw);
    FMOD_SOUND_TYPE type{}; FMOD_SOUND_FORMAT format{}; int channels{}, bits{}; float rate{};
    check(sound->getFormat(&type, &format, &channels, &bits));
    check(sound->getDefaults(&rate, nullptr));
    check(sound->getLength(&info.durationMs, FMOD_TIMEUNIT_MS));
    if ((type != FMOD_SOUND_TYPE_WAV && type != FMOD_SOUND_TYPE_MPEG) ||
        !info.durationMs || rate <= 0 || channels <= 0 || channels > 32)
        throw std::runtime_error("Expected decodable WAV or MPEG audio with a nonzero duration.");
    std::string ext = source.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if ((ext == ".wav" && type != FMOD_SOUND_TYPE_WAV) || (ext == ".mp3" && type != FMOD_SOUND_TYPE_MPEG))
        throw std::runtime_error("Audio contents do not match the file extension.");
    info.hash = aa::fingerprint(bytes); info.size = bytes.size();
    info.channels = static_cast<unsigned>(channels); info.sampleRate = static_cast<unsigned>(rate);
    fs::create_directories(output.parent_path());
    auto temp = output; temp += ".tmp";
    auto metaTemp = metadata; metaTemp += ".tmp";
    try {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close(); if (!out) throw std::runtime_error("Cannot write cooked audio.");
        std::ofstream meta(metaTemp, std::ios::trunc);
        meta << "VOXAUDIO " << aa::version << '\n' << info.hash << ' ' << info.size << ' '
            << info.durationMs << ' ' << info.sampleRate << ' ' << info.channels << '\n';
        meta.close(); if (!meta) throw std::runtime_error("Cannot write audio metadata.");
        replaceFile(temp, output);
        replaceFile(metaTemp, metadata); // Mismatched pairs are never accepted by the loader.
    } catch (...) {
        std::error_code ec; fs::remove(temp, ec); fs::remove(metaTemp, ec); throw;
    }
    std::cout << "[AudioCompiler] COOK " << key << " | " << info.durationMs / 1000.0
        << " s | " << info.sampleRate << " Hz | " << info.channels << " channels\n";
    return true;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "--force")) {
            std::cerr << "Usage: AudioCompiler <Assets folder> [--force]\n"; return 2;
        }
        const auto root = fs::canonical(fs::path(argv[1]));
        if (!fs::is_directory(root)) throw std::runtime_error("Assets path is not a directory.");
        FMOD::System* raw{}; check(FMOD::System_Create(&raw));
        const std::unique_ptr<FMOD::System, SystemDelete> system(raw);
        check(system->setOutput(FMOD_OUTPUTTYPE_NOSOUND));
        check(system->init(1, FMOD_INIT_NORMAL, nullptr));
        std::vector<fs::path> files;
        for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
            if (it->is_directory() && (it->path().filename().string().starts_with('.') || it->is_symlink())) { it.disable_recursion_pending(); continue; }
            if (it->is_symlink() || !it->is_regular_file()) continue;
            auto ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".wav" || ext == ".mp3") files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        unsigned cooked{}, skipped{}, failed{};
        for (const auto& file : files) {
            try { if (cook(*system, root, file, argc == 3)) ++cooked; else ++skipped; }
            catch (const std::exception& e) { ++failed; std::cerr << "[AudioCompiler] ERROR " << file.string() << ": " << e.what() << '\n'; }
        }
        std::cout << "[AudioCompiler] " << cooked << " cooked, " << skipped << " unchanged, " << failed << " failed\n";
        return failed ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << "[AudioCompiler] ERROR " << e.what() << '\n'; return 1; }
}
