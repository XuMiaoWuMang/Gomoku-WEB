#include "../src/util/file.hpp"

int main()
{
    std::string body;
    File_Util::read("../src/makefile", body);

    DLOG("makefile content: %s", body.c_str());
    

    return 0;
}