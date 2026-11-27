#include <cstdio>
#include "core/perception_selftest.h"
int main() {
    const std::string r = recon::runPerceptionSelfTest();
    std::fputs(r.c_str(), stdout);
    return r.find("perception_selftest=pass") != std::string::npos ? 0 : 1;
}
