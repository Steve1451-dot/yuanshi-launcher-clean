#pragma once

// Encrypted at-rest storage for Endfield Cloud login material.
//
// Mirrors mihoyo/login_store: AES-256-GCM vault with a random key file,
// atomic replacement, a ".bak" recovery copy, and a "disabled" marker so a
// completed logout cannot be revived by an interrupted delete. The caller
// supplies AppHomePath(). Values never enter diagnostics.

#include <optional>
#include <string>

namespace cloudgenshin::endfield::store {

enum class LoadStatus { Missing, Ready, Unreadable };

struct Loaded {
    LoadStatus status = LoadStatus::Missing;
    std::optional<std::string> hg_token;
    // Optional role metadata, saved since vault v2; "" for v1 vaults.
    std::string uid;
    std::string device_token; // v3; absent on older vaults, which remain valid.
};

Loaded Load(const std::string& directory);
// Failure stage: 3 = field validation, 4 = vault write/replace or round-trip
// check, 2 = disabled-marker removal. nullptr OK. Device proof writes v3;
// otherwise the existing v1 token-only / v2 token+uid formats are preserved.
bool Save(const std::string& directory, const std::string& hg_token,
          const std::string& uid = std::string(), int* failure_stage = nullptr,
          const std::string& device_token = std::string());
bool Clear(const std::string& directory);

}  // namespace cloudgenshin::endfield::store
