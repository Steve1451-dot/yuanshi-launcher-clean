#pragma once

#ifndef OPENNOW_APP_VERSION
#define OPENNOW_APP_VERSION "0.0.6"
#endif

#ifndef CLOUDGENSHIN_APP_VERSION
#define CLOUDGENSHIN_APP_VERSION "0.2.111"
#endif

namespace opennow
{

inline constexpr const char* kAppVersion = OPENNOW_APP_VERSION;
inline constexpr const char* kCloudGenshinVersion = CLOUDGENSHIN_APP_VERSION;
inline constexpr const char* kNativeAppName = "yuanshi launcher";

} // namespace opennow
