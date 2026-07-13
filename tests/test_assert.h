#pragma once

#include <cstdlib>
#include <iostream>

namespace sttnet::test {

[[noreturn]] inline void checkFailed(const char *expression,const char *file,const int line)
{
    std::cerr<<file<<':'<<line<<": check failed: "<<expression<<'\n';
    std::abort();
}

} // namespace sttnet::test

// Unlike assert(), this always evaluates expression, including Release/NDEBUG builds.
#define STTNET_CHECK(expression) \
    ((expression)?static_cast<void>(0):sttnet::test::checkFailed(#expression,__FILE__,__LINE__))
