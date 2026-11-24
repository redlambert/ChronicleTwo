#include "platform/menu_prefetch.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "log/log.hpp"
#include "platform/datapacks.hpp"
#include "platform/model_assets.hpp"
#include "platform/texture_catalog.hpp"

namespace menu_prefetch {
namespace {

constexpr std::string_view kMenuTextures = "textures/menu/1";
// The documents the trace named, by the start of their file names (the rest is a content hash).
constexpr std::array<std::string_view, 5> kDocuments = {"frametex-", "allitem-", "edmenu-", "item.json", "img-"};
constexpr std::string_view                kMenuCharacter = "menu/chara4/c01b_menu.gltf";
// The sparkle model every dungeon item pickup and monster drop loads (40 to 60 ms on the game thread each time).
constexpr std::string_view kPickupEffect = "effect/itemget.gltf";

std::atomic<bool> running{false};
std::jthread      worker;

bool Wanted(std::string_view name) {
    if (!name.ends_with(".json")) {
        return false;
    }
    for (const auto prefix : kDocuments) {
        if (name.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

} // namespace

void Start() {
    if (running.load()) {
        return;
    }
    if (worker.joinable()) {
        worker.join();
    }
    // The catalog is read here on the game thread; the worker gets its own copies of the entries.
    auto entries = std::make_shared<std::vector<texture_catalog::Entry>>();
    int  documents = 0;
    try {
        for (const auto &member : datapacks::DirectoryMembers(std::filesystem::path(kMenuTextures))) {
            const std::string name = member.filename().generic_string();
            if (!Wanted(name)) {
                continue;
            }
            if (const auto *source = texture_catalog::Find(std::string(kMenuTextures) + "/" + name)) {
                entries->insert(entries->end(), source->entries.begin(), source->entries.end());
                ++documents;
            }
        }
    } catch (const std::exception &error) {
        CT_LOG_WARN(Main, "menu prefetch: listing failed ({}); the menu loads as before", error.what());
        return;
    }
    running = true;
    try {
        worker = std::jthread([entries] {
            try {
                std::vector<const texture_catalog::Entry *> pointers;
                pointers.reserve(entries->size());
                for (const auto &entry : *entries) {
                    pointers.push_back(&entry);
                }
                if (!pointers.empty()) {
                    datapacks::PrefetchEntries(pointers);
                }
                model_assets::Warm(std::filesystem::path(kMenuCharacter));
                model_assets::Warm(std::filesystem::path(kPickupEffect));
            } catch (...) {
                // Not ready ahead; the menu reads them when it opens, as before.
            }
            running = false;
        });
    } catch (const std::exception &error) {
        running = false;
        CT_LOG_WARN(Main, "menu prefetch: no worker thread ({}); the menu loads as before", error.what());
        return;
    }
    CT_LOG_INFO(Main, "menu prefetch: reading {} menu texture documents, the menu character and the pickup sparkle ahead", documents);
}

} // namespace menu_prefetch
