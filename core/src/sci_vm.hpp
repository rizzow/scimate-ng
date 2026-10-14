// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Numeric identities belong here; transformations use their semantic names.
namespace scimate::sci_vm {
namespace section { inline constexpr unsigned strings = 5; }
namespace opcode {
inline constexpr unsigned add = 1, subtract = 2, multiply = 3, divide = 4;
inline constexpr unsigned bitwise_and = 9, equal = 13, less_signed = 17, less_unsigned = 21;
inline constexpr unsigned bitwise_or = 10;
inline constexpr unsigned load_local_to_accumulator = 65, load_local_to_stack = 69;
inline constexpr unsigned load_indexed_local_to_accumulator = 73, load_indexed_local_to_stack = 77;
inline constexpr unsigned load_indexed_global_to_accumulator = 72;
inline constexpr unsigned store_accumulator_to_local = 81, store_accumulator_to_parameter = 83;
inline constexpr unsigned store_stack_to_indexed_local = 93;
inline constexpr unsigned increment_temporary_to_accumulator = 98;
inline constexpr unsigned bitwise_not = 0, negate = 11, logical_not = 12;
inline constexpr unsigned allocate_temporaries = 31, load_temporary_to_accumulator = 66;
inline constexpr unsigned load_temporary_to_stack = 70, store_accumulator_to_temporary = 82;
inline constexpr unsigned first_variable_store = 80, temporary_storage = 2;
inline constexpr unsigned variable_storage_mask = 3, indexed_variable_flag = 8;
inline constexpr unsigned address_storage_shift = 1, indexed_address_flag = 16;
constexpr bool writes_temporary(unsigned value) {
    return value >= first_variable_store && (value & variable_storage_mask) == temporary_storage;
}
constexpr bool is_accumulator_unary(unsigned value) {
    return value == bitwise_not || value == negate || value == logical_not;
}
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
inline constexpr unsigned menu_select = 0x21;
inline constexpr unsigned draw_picture = 0x08;
inline constexpr unsigned parse = 0x24, said = 0x25, set_synonyms = 0x26;
inline constexpr unsigned file_open = 0x29, file_puts = 0x2a, file_gets = 0x2b, file_close = 0x2c;
inline constexpr unsigned do_sound = 0x31, do_avoider = 0x55, last = 0x71;
inline constexpr unsigned removed_file_functions = 4;
}
namespace sci1_kernel {
inline constexpr unsigned menu_select = 0x21;
inline constexpr unsigned draw_picture = 0x08, display = 0x1b;
inline constexpr unsigned is_object = 0x06;
inline constexpr unsigned script_id = 0x02;
inline constexpr unsigned memory = 0x72, new_list = 0x2e, dispose_list = 0x2f;
inline constexpr unsigned new_node = 0x30, first_node = 0x31, next_node = 0x34, node_value = 0x36, add_to_end = 0x39;
inline constexpr unsigned delete_key = 0x3b;
inline constexpr unsigned file_io = 0x74, string_compare = 0x45, string_length = 0x46;
inline constexpr unsigned string_copy = 0x47, string_at = 0x62;
}
namespace memory_operation { inline constexpr unsigned allocate = 2, free = 3, copy = 4, peek = 5, poke = 6; }
namespace file_io { inline constexpr unsigned open = 0, close = 1, read_string = 5, write_string = 6; }
namespace section { inline constexpr unsigned code = 2, exports = 7, relocations = 8; }
namespace section { inline constexpr unsigned locals = 10; }
inline constexpr unsigned word_bytes = 2, section_header_bytes = 4, max_script_address = 65535;
inline constexpr unsigned maximum_packed_resource_id = 2047;
constexpr unsigned encoded_opcode(unsigned value, bool byte_operand = false) {
    return (value << 1) | (byte_operand ? 1u : 0u);
}
}
