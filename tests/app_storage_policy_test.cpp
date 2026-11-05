#include "app_storage_policy.hpp"
#include "app_paths.hpp"
#include <cassert>
#include <iostream>
int main() {
    assert(opennow::AppHomePath()=="sdmc:/switch/yuanshi-launcher");
    assert(opennow::kAppStorageDirectory==opennow::AppHomePath());
    assert(opennow::kAppExecutablePath==opennow::AppHomePath()+"/yuanshi-launcher.nro");
    assert(opennow::kAppStorageDirectory.find("SwitchNOW")==std::string_view::npos);
    assert(opennow::kAppStorageDirectory.find("OpenNOWSwitch")==std::string_view::npos);
    opennow::PrepareAppStorage(); // No host filesystem mutation.
    std::cout<<"PASS independent yuanshi storage and executable namespace\n";
}
