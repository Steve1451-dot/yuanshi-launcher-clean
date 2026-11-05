// Explicit, read-only local debug helper. Never serializes Authorization.
#include "mihoyo/login_store.hpp"
#include "mihoyo/production_profile.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const auto& profile=cloudgenshin::DefaultGameProfile();
    const auto result=cloudgenshin::login_store::Load(argv[1],profile);
    if(!result.authorization) {std::cout<<"{\"saved_login_ready\":false}";return 1;}
    const auto& login=*result.authorization;
    const nlohmann::json metadata{
        {"saved_login_ready",true},
        {"app_id_matches_profile",login.combo.app_id==cloudgenshin::AppIdText(profile)},
        {"official_channel_one",login.combo.channel_id=="1"},
        {"open_id_matches_account_id",login.combo.open_id==login.account_id},
        {"channel_token_present",!login.channel_token.empty()},
        {"sdk_metadata_present",!login.combo.sdk_login_data.empty()},
        {"device_id_nonempty",!login.device.device_id.empty()}};
    std::cout<<metadata.dump()<<'\n';
}
