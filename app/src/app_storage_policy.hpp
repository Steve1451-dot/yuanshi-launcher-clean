#pragma once
#include <string_view>

namespace opennow {
// Clean-install namespace. No legacy lookup, credential import or migration.
inline constexpr std::string_view kAppStorageDirectory="sdmc:/switch/yuanshi-launcher";
inline constexpr std::string_view kAppExecutablePath="sdmc:/switch/yuanshi-launcher/yuanshi-launcher.nro";
} // namespace opennow
