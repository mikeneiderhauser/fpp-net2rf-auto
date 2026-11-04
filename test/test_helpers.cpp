// Host tests for src/Helpers.h:
//   g++ -std=c++17 -I../src test_helpers.cpp -o t && ./t
#include "Helpers.h"

#include <cstdio>

using namespace n2r;

static int fails = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
            fails++;                                                \
        }                                                           \
    } while (0)

int main() {
    // base64 (RFC 4648 test vectors), and the Basic auth header the controller expects
    CHECK(base64("") == "");
    CHECK(base64("f") == "Zg==");
    CHECK(base64("fo") == "Zm8=");
    CHECK(base64("foo") == "Zm9v");
    CHECK(base64("foob") == "Zm9vYg==");
    CHECK(base64("fooba") == "Zm9vYmE=");
    CHECK(base64("foobar") == "Zm9vYmFy");
    CHECK(base64("admin:yourpassword") == "YWRtaW46eW91cnBhc3N3b3Jk");
    CHECK(base64(std::string("\xff\xfe\x00", 3)) == "//4A");   // bytes above 0x7F and NUL

    // colours: FPP's color argument is "#RRGGBB"; the controller wants "RRGGBB"
    CHECK(hexColour("#FF8000") == "FF8000");
    CHECK(hexColour("#ff8000") == "FF8000");
    CHECK(hexColour("0xff8000") == "FF8000");
    CHECK(hexColour("0XFF8000") == "FF8000");
    CHECK(hexColour("ff8000") == "FF8000");
    CHECK(hexColour("") == "" && hexColour("#") == "" && hexColour("#FFF") == "" && hexColour("#FF80001") == "");
    CHECK(hexColour("#GG8000") == "" && hexColour("red") == "" && hexColour("0x0xFF80") == "");

    // on/off arguments
    CHECK(argTrue("true") && argTrue("True") && argTrue("1") && argTrue("on") && argTrue("ON") && argTrue("yes"));
    CHECK(!argTrue("false") && !argTrue("0") && !argTrue("off") && !argTrue("") && !argTrue("no"));

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
