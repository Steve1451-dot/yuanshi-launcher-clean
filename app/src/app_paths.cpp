#include "app_paths.hpp"
#include "app_storage_policy.hpp"

#include <sys/stat.h>

namespace opennow
{
const std::string& AppHomePath()
{
    static const std::string path(kAppStorageDirectory);
    return path;
}

void PrepareAppStorage()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);

    mkdir(AppHomePath().c_str(), 0777);
    mkdir((AppHomePath()+"/backgrounds").c_str(), 0777);
#endif
}

} // namespace opennow
