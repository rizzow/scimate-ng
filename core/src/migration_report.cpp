// SPDX-License-Identifier: GPL-2.0-or-later
#include "migration_internal.hpp"
#include "sci_vm.hpp"
#include <set>
#include <sstream>
namespace scimate::migration_detail {
std::string migration_report(std::size_t resource_count, const MigrationStats& stats,
                              const std::vector<std::string>& selected_fallbacks) {
    const auto& [scripts,remapped,bridged,sound_calls,parser_calls,opaque_exports,unusual_file_frames,legacy_branches] = stats;
    std::ostringstream report;
    report << "SCIMATE experimental generic SCI0 -> SCI1 migration\n"
           << "Resources: " << resource_count << "; duplicate SCI0 IDs resolved using first map entry (ScummVM semantics).\n"
           << "Scripts scanned: " << scripts << "; remapped kernel calls: " << remapped << "; file-I/O bridge calls: " << bridged << ".\n"
           << (parser_calls ? "Target: SCI1 EGA packed-ID map, eight-byte volume headers, uncompressed payloads.\n" : "Target: SCI1 late directory map, nine-byte volume headers, uncompressed payloads.\n")
           << "Original EGA graphics, game-object name and relative LOFS semantics retained.\n"
           << "Parser calls retained: " << parser_calls << "; "
           << (parser_calls ? "word vocabulary 0 converted to 900; grammar 900 -> 901, suffixes 901 -> 902; obsolete SCI0 kernel names omitted.\n" : "no parser resource conversion required.\n")
           << "Main export expansion relocates absolute addresses; four FileIO bridges appended.\n"
           << "FileIO bridges forward actual parameters including &rest; unusual encoded frames retained: " << unusual_file_frames << " (runtime signature validation remains required).\n"
           << "Validated SCI0 late DoSound calls: " << sound_calls << "; kernel remapped to 0x2d.\n"
           << (sound_calls ? "Sound resources retained after SCI0 late decoding; ScummVM late-sound dispatch verified by vocabulary 997.\n" : "No DoSound compatibility profile required.\n")
           << "Script layouts, main branches, methods and direct calls validated; opaque uncalled export slots preserved: " << opaque_exports << ".\n"
           << "Legacy non-main branch targets retained unchanged: " << legacy_branches.size() << "; runtime reachability UNVERIFIED.\n"
           << "Dynamic export accesses remain unverified; no game-specific assumptions, assets or detection hashes fabricated.\n"
           << "UNVERIFIED: runtime detection, graphics/sound compatibility, save/restore and gameplay require manual ScummVM validation.\n"
           << "This is a ScummVM-targeted compatibility candidate, not a claim of original Sierra interpreter compatibility or a general SCI1.1 migration.\n";
    for (const auto& branch : legacy_branches) report << "UNVERIFIED legacy branch: " << branch << ".\n";
    for (const auto& fallback : selected_fallbacks) report << "Selected fallback: " << fallback << ".\n";
    return report.str();
}
std::vector<std::string> auxiliary_references(const Game& game) {
    std::set<std::string> names;
    for (const auto& resource : game.resources) if (resource.entry.id.type == ResourceType::script) {
        const auto script = parse_migration_script(resource);
        for (const auto& section : script.sections) if (section.type == sci_vm::section::strings) {
            std::size_t cursor = section.start+sci_vm::section_header_bytes;
            while (cursor < section.end) {
                const auto start = cursor;
                while (cursor < section.end && resource.payload[cursor]) ++cursor;
                const std::string name(resource.payload.begin()+start,resource.payload.begin()+cursor);
                ++cursor;
                if (name.empty() || name.size() > 255 || name.find('.') == std::string::npos) continue;
                bool filename = true;
                for (const unsigned char ch : name)
                    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')) filename = false;
                if (filename && name != "." && name != "..") names.insert(name);
            }
        }
    }
    return {names.begin(),names.end()};
}
std::string object_layout_diagnostics(const ScriptMetadata& scripts) {
    std::map<unsigned,std::size_t> classes;
    for (const auto& entry : scripts) for (const auto& object : entry.second.objects)
        if (object.is_class) classes.emplace(object.values.at(0),object.values.size());
    std::ostringstream report;
    for (const auto& entry : scripts) for (const auto& object : entry.second.objects) if (!object.is_class) {
        const auto base = classes.find(object.values.at(0));
        if (base != classes.end() && base->second != object.values.size())
            report << "Source object layout mismatch: script " << entry.first << " at byte " << object.pointer <<
                ", instance properties " << object.values.size() << ", base properties " << base->second << "; preserved.\n";
    }
    return report.str();
}
}
