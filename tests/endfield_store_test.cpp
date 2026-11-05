#include "endfield/endfield_store.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

static std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main() {
    using namespace cloudgenshin::endfield::store;
    const auto directory = std::filesystem::temp_directory_path() /
        ("cg-endfield-store-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto dir = directory.string();

    assert(Load(dir).status == LoadStatus::Missing);

    // A confirmed scan must persist independently of optional uid lookup.
    // The v1 record can obtain cloud/game tickets without a vault upgrade.
    assert(Save(dir, "synthetic-hg-token", ""));
    auto loaded = Load(dir);
    assert(loaded.status == LoadStatus::Ready && loaded.hg_token &&
           *loaded.hg_token == "synthetic-hg-token" && loaded.uid.empty());
    assert(read_file(directory / "endfield-login.vault")
               .find("synthetic-hg-token") == std::string::npos);
    assert(read_file(directory / "endfield-login.key").size() == 32);

    // The uid recovery rewrites the same vault as v2 without losing the token.
    assert(Save(dir, "synthetic-hg-token", "synthetic-uid"));
    loaded = Load(dir);
    assert(loaded.status == LoadStatus::Ready && loaded.hg_token &&
           *loaded.hg_token == "synthetic-hg-token" &&
           loaded.uid == "synthetic-uid");

    // Field validation still rejects an empty token and an oversized uid.
    int stage = 0;
    assert(!Save(dir, "", "uid-1", &stage) && stage == 3);
    assert(!Save(dir, "token-2", std::string(65, 'x'), &stage) && stage == 3);
    loaded = Load(dir);
    assert(loaded.status == LoadStatus::Ready && loaded.uid == "synthetic-uid");

    // QR remembered-device proof is kept in the authenticated vault, including
    // scans without optional role metadata. Old v1/v2 reads remain supported.
    assert(Save(dir,"synthetic-hg-token","",nullptr,"synthetic-device-proof"));
    loaded=Load(dir);
    assert(loaded.status==LoadStatus::Ready && loaded.uid.empty() &&
           loaded.device_token=="synthetic-device-proof");
    assert(read_file(directory/"endfield-login.vault").find("synthetic-device-proof")==std::string::npos);

    // A damaged primary recovers the previous complete authenticated file.
    std::filesystem::rename(directory / "endfield-login.vault",
                            directory / "endfield-login.vault.bak");
    assert(Load(dir).status == LoadStatus::Ready);

    // Logout removes the material and prevents an old backup from reviving it.
    assert(Clear(dir));
    assert(Load(dir).status == LoadStatus::Missing);
    std::filesystem::remove(directory / "endfield-login.disabled");
    std::filesystem::remove_all(directory);
}
