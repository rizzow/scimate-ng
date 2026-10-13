// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Numeric identities belong here; transformations use their semantic names.
namespace scimate::sci_vm {
namespace opcode {
inline constexpr unsigned branch_true = 23, branch_false = 24, jump = 25;
inline constexpr unsigned load_immediate = 26, push_accumulator = 27, push_immediate = 28;
inline constexpr unsigned call_local = 32, call_kernel = 33, call_base = 34, call_external = 35;
inline constexpr unsigned return_ = 36, send = 37, load_class = 40, send_self = 42, send_super = 43;
inline constexpr unsigned rest = 44, load_address = 45, self_id = 46;
inline constexpr unsigned property_to_accumulator = 49, property_to_stack = 51;
inline constexpr unsigned increment_property_to_stack = 55, decrement_property_to_stack = 56;
inline constexpr unsigned load_offset_to_accumulator = 57, load_offset_to_stack = 58;
inline constexpr unsigned push_zero = 59, push_one = 60, push_two = 61, push_self = 62;
inline constexpr unsigned load_global_to_accumulator = 64, load_parameter_to_accumulator = 67;
inline constexpr unsigned load_global_to_stack = 68, load_parameter_to_stack = 71;
constexpr bool is_branch(unsigned value) { return value >= branch_true && value <= jump; }
}
namespace sci0_kernel {
inline constexpr unsigned parse = 0x24, said = 0x25, set_synonyms = 0x26;
inline constexpr unsigned file_open = 0x29, file_puts = 0x2a, file_gets = 0x2b, file_close = 0x2c;
inline constexpr unsigned do_sound = 0x31, do_avoider = 0x55, last = 0x71;
inline constexpr unsigned removed_file_functions = 4;
}
namespace sci1_kernel { inline constexpr unsigned file_io = 0x74; }
namespace file_io { inline constexpr unsigned open = 0, close = 1, read_string = 5, write_string = 6; }
namespace section { inline constexpr unsigned code = 2, exports = 7, relocations = 8; }
inline constexpr unsigned word_bytes = 2, section_header_bytes = 4, max_script_address = 65535;
constexpr unsigned encoded_opcode(unsigned value, bool byte_operand = false) {
    return (value << 1) | (byte_operand ? 1u : 0u);
}
}
