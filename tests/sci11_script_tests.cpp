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
        // One exported procedure loading a word and returning, with no objects or locals.
        // SCI0 code section, export table, and terminator; entirely synthetic.
        const scimate::Bytes source{2,0,8,0,0x34,1,0,0x48,7,0,8,0,1,0,4,0,0,0};
        const auto pair = scimate::split_sci0_script(source);
        scimate::validate_sci11_script_pair(pair.script, pair.heap);
        require(pair.code_addresses.at(4) == 10, "exported procedure was not relocated");
        require(pair.script.at(8) == 10 && pair.script.at(9) == 0, "export target mismatch");
        rejected({}, {});
        auto damaged = pair.script;
        damaged.at(8) = 11; // Not an instruction boundary.
        rejected(damaged, pair.heap);
        damaged = pair.script;
        damaged.at(13) = 0; // Replaces ret with bnot, falling into the relocation table.
        rejected(damaged, pair.heap);
        const auto padded = scimate::split_sci0_script({2,0,5,0,0x48,7,0,8,0,1,0,4,0,0,0});
        scimate::validate_sci11_script_pair(padded.script,padded.heap);
        damaged = padded.script;
        damaged.at(8) = 11; // Alignment byte is a boundary but cannot be a valid procedure.
        rejected(damaged,padded.heap);
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
