#include "../main/wifi_password.hpp"
#include <cassert>
#include <string>
int main() {
    using sirokko::wifi::valid_password;
    assert(valid_password(""));
    assert(!valid_password(nullptr));
    assert(!valid_password("1234567"));
    assert(valid_password("12345678"));
    assert(valid_password(std::string(63, 'A').c_str()));
    assert(!valid_password(std::string(64, 'A').c_str()));
    assert(!valid_password("abcd efgh"));
    assert(!valid_password("abcdefg\nh"));
    assert(!valid_password("пароль123"));
    assert(valid_password("Ab3!~@#z"));
}
