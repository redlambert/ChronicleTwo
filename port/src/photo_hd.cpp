#include "photo_hd.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "gfx/gfx.hpp"
#include "log/log.hpp"
#include "platform/imagefile.hpp"
#include "platform/paths.hpp"

namespace {

// Guards every member below. The preload thread fills them while the game runs.
std::mutex                                         photos_mutex;
std::unordered_map<uint64_t, std::vector<uint8_t>> photos;
std::unordered_set<uint64_t>                       missing;
// Set once the preload has read the photos folder. From then on a photo not in `photos` has no file, so
// lookups never touch the disk; the Switch's SD card made each of those first lookups slow enough that the
// first Make page (30 photo slots) froze for over a second.
bool preloaded = false;

uint64_t Hash(const uint8_t *bytes, size_t size) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }
    return hash;
}

std::filesystem::path PhotoPath(uint64_t hash) {
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.png", static_cast<unsigned long long>(hash));
    return PathsSaveRoot() / "photos" / name;
}

/**
 * Reads one stored photo, or returns false when it is missing or the wrong size.
 */
bool LoadPhoto(const std::filesystem::path &path, std::vector<uint8_t> &rgba) {
    int width = 0;
    int height = 0;
    if (!imagefile::LoadPng(path, width, height, rgba) || std::cmp_not_equal(width, kPhotoHdSize) ||
        std::cmp_not_equal(height, kPhotoHdSize)) {
        return false;
    }
    for (size_t i = 3; i < rgba.size(); i += 4) {
        rgba[i] = 0xFF; // Opaque in the renderer's units, as retail draws photos (TEXA TA0 = TA1 = 0x80).
    }
    return true;
}

/**
 * Reads every stored photo into memory, then marks the folder as fully known.
 */
void PreloadPhotos() {
    const auto      start = std::chrono::steady_clock::now();
    const auto      folder = PathsSaveRoot() / "photos";
    std::error_code error;
    int             loaded = 0;
    for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error)) {
        const auto name = entry->path().filename().string();
        if (name.size() != 20 || name.substr(16) != ".png") {
            continue;
        }
        char          *parsed_end = nullptr;
        const uint64_t hash = std::strtoull(name.substr(0, 16).c_str(), &parsed_end, 16);
        if (parsed_end == nullptr || *parsed_end != '\0') {
            continue;
        }
        {
            std::lock_guard lock(photos_mutex);
            if (photos.contains(hash)) {
                continue;
            }
        }
        std::vector<uint8_t> rgba;
        if (!LoadPhoto(entry->path(), rgba)) {
            continue;
        }
        std::lock_guard lock(photos_mutex);
        photos.try_emplace(hash, std::move(rgba));
        ++loaded;
    }
    {
        std::lock_guard lock(photos_mutex);
        preloaded = true;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CT_LOG_INFO(Gfx, "photo: preloaded {} sharp photos in {:.1f} ms", loaded, ms);
}

} // namespace

bool PhotoHdIsWorkTexture(const char *name) {
    return name != nullptr && std::strcmp(name, "fix_work") == 0;
}

void PhotoHdPreload() {
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    std::thread(PreloadPhotos).detach();
}

void PhotoHdRemember(const uint8_t *image16, size_t bytes, std::vector<uint8_t> rgba) {
    if (image16 == nullptr || rgba.size() != static_cast<size_t>(kPhotoHdSize) * kPhotoHdSize * 4) {
        return;
    }
    const uint64_t  hash = Hash(image16, bytes);
    const auto      path = PhotoPath(hash);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (!gfx::WritePng(path, rgba.data(), kPhotoHdSize, kPhotoHdSize)) {
        CT_LOG_ERROR(Gfx, "photo: could not write {}", path.string());
    }
    std::lock_guard lock(photos_mutex);
    missing.erase(hash);
    photos[hash] = std::move(rgba);
}

const std::vector<uint8_t> *PhotoHdFind(const uint8_t *image16, size_t bytes) {
    if (image16 == nullptr) {
        return nullptr;
    }
    const uint64_t hash = Hash(image16, bytes);
    {
        std::lock_guard lock(photos_mutex);
        // Map nodes keep their address while others are added, so the pointer stays valid after unlocking.
        if (const auto found = photos.find(hash); found != photos.end()) {
            return &found->second;
        }
        if (preloaded || missing.contains(hash)) {
            return nullptr;
        }
    }
    // The preload has not finished: read this one now, as before.
    std::vector<uint8_t> rgba;
    if (!LoadPhoto(PhotoPath(hash), rgba)) {
        std::lock_guard lock(photos_mutex);
        missing.insert(hash);
        return nullptr;
    }
    std::lock_guard lock(photos_mutex);
    return &photos.try_emplace(hash, std::move(rgba)).first->second;
}
