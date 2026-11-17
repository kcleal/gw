#include "deps/protocol/viewer/v1/types.hpp"
#include <cassert>
#include <limits>

int main() {
    using namespace GwViewerProtocol::V1;
    const auto size=Viewport{601,301,1.5}.framebuffer();
    assert(size.width==902&&size.height==452);
    assert(counter("18446744073709551615")==UINT64_MAX);
    for(const auto* value:{"0","01","-1","18446744073709551616","1.5"}) {
        bool rejected=false;
        try {counter(value);} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
    for(const auto& value:{Viewport{8192,8192,2},Viewport{0,100,1},Viewport{900,500,std::numeric_limits<double>::infinity()}}) {
        bool rejected=false;
        try {value.framebuffer();} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
}
