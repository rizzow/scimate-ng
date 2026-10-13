// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/sci1.hpp"
#include "../core/src/script_internal.hpp"
#include <iostream>

namespace {
using namespace fixture;
void patch(Bytes& bytes, std::size_t at, unsigned value) {
    bytes.at(at) = static_cast<std::uint8_t>(value); bytes.at(at+1) = static_cast<std::uint8_t>(value >> 8);
}
Bytes msb(const std::vector<std::pair<unsigned,unsigned>>& codes) {
    Bytes bytes;
    unsigned bits = 0;
    for (auto item : codes) for (unsigned i = item.second; i > 0; --i) {
        if (bits%8 == 0) bytes.push_back(0);
        bytes.back() |= static_cast<std::uint8_t>(((item.first >> (i-1)) & 1u) << (7-bits%8));
        ++bits;
    }
    return bytes;
}
Resource resource(ResourceType type, unsigned number, Bytes payload) {
    return {{{type,static_cast<std::uint16_t>(number)},0,0},0,std::move(payload)};
}
Game sample() {
    Game game;
    Bytes bytes{7,0,8,0,1,0,0,0}; // Export 0 patched below.
    put(bytes,2,2); put(bytes,11,2);
    bytes.insert(bytes.end(), {0x78,0x39,0,0x43,0x2d,2,0x48}); // SaveGame kernel, remapped below.
    put(bytes,5,2); put(bytes,9,2);
    auto name = bytes.size(); bytes.insert(bytes.end(),{'D','E','M','O',0});
    put(bytes,1,2); put(bytes,24,2);
    put(bytes,0x1234,2); put(bytes,0,2); put(bytes,0,2); put(bytes,4,2);
    auto pointer = bytes.size(); patch(bytes,6,pointer);
    put(bytes,0,2); put(bytes,0xffff,2); put(bytes,0,2); put(bytes,name,2);
    put(bytes,0,2); put(bytes,0,2); // method count and separator
    put(bytes,8,2); put(bytes,8,2); put(bytes,1,2); put(bytes,pointer+6,2); put(bytes,0,2);
    game.resources.push_back(resource(ResourceType::script,0,std::move(bytes)));
    // Synthetic selector vocabulary; no commercial game data.
    game.resources.push_back(resource(ResourceType::vocab,997,{0,0,4,0,4,0,'n','a','m','e'}));
    // FOpen/FPuts/FGets/FClose and a remapped kernel in another script.
    Bytes code{0x43,0x29,4,0x43,0x2a,4,0x43,0x2b,6,0x43,0x2c,2,0x43,0x40,0,0x48};
    Bytes other; put(other,2,2); put(other,code.size()+4,2);
    other.insert(other.end(),code.begin(),code.end()); put(other,0,2);
    game.resources.push_back(resource(ResourceType::script,20,std::move(other)));
    return game;
}
void tests() {
    // SCI0 sound ABI is retained only after proving both call frames and the
    // resource layout; selector names alone do not establish compatibility.
    constexpr unsigned sound_args[]{2,2,1,2,1,2,2,1,1,2,2,1,1};
    for (unsigned subop = 0; subop <= 12; ++subop) {
        auto sound_game = sample();
        Bytes code{0x39,static_cast<std::uint8_t>(sound_args[subop]),0x39,static_cast<std::uint8_t>(subop)};
        if (sound_args[subop] == 2)
            code.push_back(subop == 4 || subop == 6 || subop == 8 ? 0x76 : 0x7c); // integer / object
        code.insert(code.end(),{0x43,0x31,static_cast<std::uint8_t>(2*sound_args[subop]),0x48});
        Bytes script; put(script,2,2); put(script,code.size()+4,2);
        script.insert(script.end(),code.begin(),code.end()); put(script,0,2);
        sound_game.resources[2].payload = script;
        Bytes sound(33,0); sound.insert(sound.end(),{0,0x90,60,100,10,0x80,60,0,0,0xfc});
        sound_game.resources.push_back(resource(ResourceType::sound,8,sound));
        const auto result = migrate_sci0_to_sci1(sound_game);
        const auto reopened = read_sci1_game(result.files,Sci1Format::sci1_late);
        bool found_sound = false, found_call = false;
        for (const auto& item : reopened.resources) {
            if (item.entry.id.type == ResourceType::sound) {
                require(item.payload == sound,"validated sound payload changed"); found_sound = true;
            }
            if (item.entry.id.type == ResourceType::script && item.entry.id.number == 20) {
                const auto decoded = scimate::script_detail::parse(item.payload);
                for (const auto& inst : decoded.instructions) if (inst.opcode == 33) {
                    require(inst.operands[0] == 0x2d,"DoSound kernel not remapped"); found_call = true;
                }
            }
        }
        require(found_sound && found_call,"sound migration lost resources/calls");
        auto invalid = sound_game;
        invalid.resources.back().payload.resize(10);
        fails([&] { migrate_sci0_to_sci1(invalid); },"sound 8");
        invalid = sound_game;
        invalid.resources[1].payload = {0,0,4,0,7,0,'n','o','d','e','P','t','r'};
        fails([&] { migrate_sci0_to_sci1(invalid); },"nodePtr");
        invalid = sound_game;
        invalid.resources[2].payload[7] = 13;
        fails([&] { migrate_sci0_to_sci1(invalid); },"suboperation");
        invalid = sound_game;
        invalid.resources[2].payload[6] = 0x36; // push accumulator: dynamic subop
        invalid.resources[2].payload.erase(invalid.resources[2].payload.begin()+7);
        patch(invalid.resources[2].payload,2,script.size()-3);
        fails([&] { migrate_sci0_to_sci1(invalid); },"suboperation");
    }
    for (auto format : {Sci1Format::sci1_late,Sci1Format::sci11}) {
        const std::vector<Resource> input{resource(ResourceType::heap,60000,{1,2,3}),resource(ResourceType::script,0,{0,0})};
        auto files = write_sci1_game(input,format);
        auto map = read_sci1_map(files.map,format);
        require(map.size() == 2 && map[0].id.type == ResourceType::script && map[1].id.type == ResourceType::heap, "SCI1 directory grouping failed");
        if (format == Sci1Format::sci11) require(map[1].offset%2 == 0,"SCI1.1 word offset is not aligned");
        auto reopened = read_sci1_game(files,format);
        require(reopened.resources[0].payload == input[1].payload && reopened.resources[1].payload == input[0].payload,"SCI1 payload mismatch");
        auto broken = files; broken.volumes[0].pop_back();
        fails([&] { read_sci1_game(broken,format); }, "truncated resource payload");
        broken = files; broken.volumes[0][1] = 7;
        fails([&] { read_sci1_game(broken,format); }, "ID differs");
        broken = files; broken.map[2] = 255;
        fails([&] { read_sci1_map(broken.map,format); }, "first resource table");
        broken = files; broken.map[3] = broken.map[0];
        fails([&] { read_sci1_map(broken.map,format); }, "duplicate");
        fails([&] { read_sci1_map(Bytes(files.map.begin(),files.map.begin()+2),format); },"truncated");
        require(read_sci1_game(write_sci1_game({},format),format).resources.empty(),"empty SCI1 game failed");
    }
    require(decode_sci1(1,huffman,2) == Bytes({'A','B'}),"SCI1 Huffman method assignment failed");
    require(decode_sci1(2,msb({{256,9},{65,9},{258,9},{257,9}}),3) == Bytes({'A','A','A'}),"SCI1 overlapping LZW reference failed");
    std::vector<std::pair<unsigned,unsigned>> codes{{256,9}};
    Bytes expected;
    // A known literal stream crosses SCI1's early-change width boundary:
    // 254 literals use nine bits, the following 46 use ten bits.
    for (unsigned i = 0; i < 300; ++i) { codes.emplace_back(i%256,i<254?9:10); expected.push_back(static_cast<std::uint8_t>(i)); }
    codes.emplace_back(257,10);
    require(decode_sci1(2,msb(codes),expected.size()) == expected,"SCI1 LZW width boundary failed");
    fails([] { decode_sci1(2,msb({{256,9},{259,9},{257,9}}),1); },"dictionary");
    fails([] { decode_sci1(2,msb({{256,9},{65,9}}),1); },"truncated");
    fails([] { decode_sci1(18,{},0); },"unsupported SCI1 compression");
    fails([] { write_sci1_game({resource(ResourceType::view,0,Bytes(65532))},Sci1Format::sci1_late); },"header limit");
    auto migrated = migrate_sci0_to_sci1(sample());
    auto output = read_sci1_game(migrated.files,Sci1Format::sci1_late);
    const auto& main = output.resources[0].payload;
    const auto parsed = scimate::script_detail::parse(main);
    require(parsed.exports.size() == 5,"file-I/O bridges not exported");
    require(parsed.objects[0].pointer == 48 && parsed.objects[0].values[3] == 31,"main absolute addresses not relocated");
    require(main[24] == 0x29,"main kernel not remapped");
    const auto& other = output.resources[1].payload;
    require(other[4] == 0x45 && other[5] == 1 && other[8] == 2 && other[11] == 3 && other[14] == 4,"file-I/O calls not bridged");
    require(other[17] == 0x3c,"kernel numbering shift failed");
    require(migrated.report.find("UNVERIFIED") != std::string::npos,"migration claims unproven runtime success");
    auto unsupported = sample(); unsupported.resources[0].payload[23] = 'X';
    require(!migrate_sci0_to_sci1(unsupported).files.map.empty(),"migration depends on game identity");
    unsupported = sample(); unsupported.resources[2].payload[5] = 0x55;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"DoAvoider");
    unsupported = sample(); unsupported.resources[2].payload[6] = 0;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"argument count");
    unsupported = sample(); unsupported.resources[2].payload[5] = 0x24;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"parser migration");
    unsupported = sample(); unsupported.resources[2].payload[5] = 0x31;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"DoSound");
    // Source SCI0 duplicate resolution matches ScummVM (first entry wins).
    auto duplicates = sample(); duplicates.resources.push_back(resource(ResourceType::script,20,{}));
    require(migrate_sci0_to_sci1(duplicates).files.map == migrated.files.map,"SCI0 duplicate resolution changed");
}
} // namespace
int main() {
    try { tests(); std::cout << "SCI1 format and migration tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
