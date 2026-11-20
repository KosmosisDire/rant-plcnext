#pragma once
// Messages the Msg blocks build: field names and the values the program gave them, written into
// the Message struct the block returns, so a PLC cycle allocates nothing. A block's Value or
// Request pin reads one, and its TypeName pin names the type it goes out as.

#include "rant.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace blocks::msg {

// One input of a Msg block: its memory and its type as iec/Msg.cs packs it, the eCLR type code in
// the low byte, the Kind flags in the next, and an array's element size above them.
struct Item {
    const uint8_t* data = nullptr;
    uint32_t       length = 0;
    uint32_t       type = 0;
};

// The size of iec/Message.cs's struct: a header, the first fault, then the entries.
constexpr size_t MESSAGE_BYTES = 1108;

// Whether a pin's type, by its eCLR namespace and name, is iec/Message.cs's struct.
bool IsMessageType(const char* space, const char* type);

// Empties the message at m, then adds the index'th field name and its value to it.
void Begin(uint8_t* m);
void Add(uint8_t* m, int index, Item field, Item value);

// A message copied out of its struct.
struct Message {
    std::string          type;      // TypeName's, empty for the target's own
    std::string          fault;     // how the program built it wrong, empty when it did not
    std::vector<uint8_t> entries;   // read with Apply
};

// Copies the message at m of length bytes into out, as the type named type. m null is a message
// with no fields, so the type goes out at its defaults.
void Read(const uint8_t* m, size_t length, const char* type, size_t typeLength, Message& out);

// A message of schema s holding each field m names, the rest at their defaults. False with why
// when a name is not a field of s or its value does not fit the field.
bool Apply(const Message& m, const rant::detail::RantSchema* s, std::vector<uint8_t>& out, std::string& why);

// The field names m sets, each once, in the order it first sets them.
void Names(const Message& m, std::vector<std::string>& out);

// Schema text of s cut down to the fields names reach, under the same type names, so a variable
// written with it changes only those fields. Empty with why when a name cannot be cut out.
std::string SubsetText(const rant::detail::RantSchema* s, const std::vector<std::string>& names, std::string& why);

} // namespace blocks::msg
