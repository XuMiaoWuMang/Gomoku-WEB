#include "../src/util/str.hpp"
#include <iostream>
int main() {
    // std::string str = ",aaa,,,,a,b,c,,,ddddd,,dwa,a,d,e,,,,";
    std::string str = ",aaaaaaaaaaa";

    std::vector<std::string> ret;
    Str_Util::split(str, ",", ret);
    for (int i = 0; i < ret.size(); i++) {
        std::cout << ret[i] << std::endl;
    }
    return 0;
}