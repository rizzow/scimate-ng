#include "scimate/sci11_script.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejected(const scimate::Bytes& script, const scimate::Bytes& heap) {
    bool failed = false;
    try { scimate::validate_sci11_script_pair(script, heap); }
    catch (const std::exception&) { failed = true; }
    require(failed, "invalid pair accepted");
}
}

int main() {
    try {
        // One exported procedure returning immediately, with no objects or locals.
        // SCI0 code section, export table, and terminator; entirely synthetic.
        const scimate::Bytes source{2,0,5,0,0x48,7,0,8,0,1,0,4,0,0,0};
        const auto pair = scimate::split_sci0_script(source);
        scimate::validate_sci11_script_pair(pair.script, pair.heap);
        require(pair.code_addresses.at(4) == 10, "exported procedure was not relocated");
        require(pair.script.at(8) == 10 && pair.script.at(9) == 0, "export target mismatch");
        rejected({}, {});
        auto damaged = pair.script;
        damaged.at(8) = 11; // Not an instruction boundary.
        rejected(damaged, pair.heap);
        auto truncated = pair.heap;
        truncated.pop_back();
        rejected(pair.script, truncated);
        std::cout << "SCI1.1 script/heap checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
