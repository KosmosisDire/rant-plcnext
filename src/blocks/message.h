#pragma once
// Messages Msg builds: a type name, then field names and the values the program gave them. They
// live in a ring of fixed buffers, so a PLC cycle allocates nothing, and a block's Message input
// reads one by the handle Msg returned.

#include "rant.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace blocks::msg {

// One input of Msg: its memory and its type as iec/Msg.cs packs it, the eCLR type code in the
// low byte, the Kind flags in the next, and an array's element size above them.
struct Item {
    const uint8_t* data = nullptr;
    uint32_t       length = 0;
    uint32_t       type = 0;
};

// One step of Msg, which Engineer folds left: the type name and the first field name, or a
// message's handle and its next input. Returns the message's handle, 0 when no buffer is left.
uint32_t Step(Item a, Item b);

// A message copied out of its buffer.
struct Message {
    std::string          type;      // empty for the target's own
    std::string          fault;     // how the program built it wrong, empty when it did not
    std::vector<uint8_t> entries;   // read with Apply
};

// Copies the message of handle into out. False when handle is no message or its buffer was
// used again since, as a handle kept past the cycle that built it finds.
bool Read(uint32_t handle, Message& out);

// A message of schema s holding each field m names, the rest at their defaults. False with why
// when a name is not a field of s or its value does not fit the field.
bool Apply(const Message& m, const rant::detail::RantSchema* s, std::vector<uint8_t>& out, std::string& why);

// The field names m sets, each once, in the order it first sets them.
void Names(const Message& m, std::vector<std::string>& out);

// Schema text of s cut down to the fields names reach, under the same type names, so a variable
// written with it changes only those fields. Empty with why when a name cannot be cut out.
std::string SubsetText(const rant::detail::RantSchema* s, const std::vector<std::string>& names, std::string& why);

} // namespace blocks::msg
