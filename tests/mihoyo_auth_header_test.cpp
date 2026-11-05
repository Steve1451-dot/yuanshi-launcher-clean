#include "mihoyo/auth_header.hpp"

#include <cassert>

int main() {
    using cloudgenshin::auth::BuildComboTokenHeader;
    const cloudgenshin::auth::ComboLoginFields fields{"A", "C", "O", "T"};
    const auto value = BuildComboTokenHeader(fields, "test-key", "test-biz");
    assert(value && *value ==
        "ai=A;ci=C;oi=O;ct=T;si=75390aed8fba0373b42cbca6f6ee0da531b3542e7a2def468fdc59698490e057;bi=test-biz");
    assert(!BuildComboTokenHeader(fields, "", "test-biz"));
    assert(!BuildComboTokenHeader({"A\r\nInjected: x", "C", "O", "T"}, "key", "biz"));
    assert(!BuildComboTokenHeader({"A", "C", "O", "T;bad"}, "key", "biz"));
}
