#pragma once
#include "mihoyo/launch_flow.hpp"

namespace cloudgenshin::login_store {
enum class LoadStatus { Missing, Ready, Unreadable };
struct Loaded {
    LoadStatus status=LoadStatus::Missing;
    std::optional<Authorization> authorization;
};
// The caller supplies AppHomePath(). One entry per game profile; the vault is
// separate from game startup/QR data. No values or exceptions enter diagnostics.
Loaded Load(const std::string& directory,const GameProfile& profile);
bool Save(const std::string& directory,const GameProfile& profile,
          const Authorization& authorization);
bool Clear(const std::string& directory);
}
