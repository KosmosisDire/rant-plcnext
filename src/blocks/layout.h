#pragma once
// An IEC value's type as the C# walk reports it (iec/Layout.cs), and how its bytes become a Rant
// message and back.

#include "plc/model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace blocks {

// Node::flags, as iec/Runtime.cs Kind spells them.
constexpr int FLAG_STRING = 1;
constexpr int FLAG_ARRAY  = 2;
constexpr int FLAG_ENUM   = 4;   // code is then the enum's underlying type

// One node of an IEC type in the order the walk visits it.
struct Node {
    int              parent = -1;
    std::string      name;           // the field name, empty at the root
    std::string      space, type;    // the eCLR namespace and type name
    uint32_t         offset = 0;     // absolute, in the PLC's memory
    uint32_t         size   = 0;
    int              code   = 0;     // the eCLR type code, see TypeCode in layout.cpp
    int              flags  = 0;
    std::vector<int32_t> bounds;     // an array's lower and upper bound per dimension
    std::string          boundKinds; // 'L' or 'U' for each entry of bounds, in the order they came
    std::vector<plc::EnumOption> options;   // an enum's options
    std::vector<int>     children;
};

// One value of an entity: a topic's value, or a call's request or response.
struct Layout {
    std::vector<Node> nodes;
    uintptr_t         type = 0;     // the eCLR type handle the nodes describe
    bool              done = false;
    plc::Member       root;         // the value as Rant sees it, once done
    std::string       schema;       // its schema text, once done
    std::string       error;        // why the type cannot reach the mesh, empty when it can
};

// Starts a walk over for type, dropping what an earlier type left.
void Restart(Layout& l, uintptr_t type);
// Records one node and returns its index, -1 for a parent that does not exist.
int AddNode(Layout& l, int parent, const char* name, const char* space, const char* type, uint32_t offset,
            uint32_t size, int code, int flags);
// Records one bound of an array node. The eCLR names them LB, UB, or LB0, UB0 per dimension.
void AddBound(Layout& l, int node, const char* name, int32_t value);
// Records one option of an enum node.
void AddOption(Layout& l, int node, const char* name, int32_t value);
// Turns the walked nodes into the member tree and schema, or sets error. sample is the value's
// memory, where a string's real capacity is.
void Finish(Layout& l, const uint8_t* sample);

// The PLC's bytes as a Rant message.
void Encode(const Layout& l, const uint8_t* plc, std::vector<uint8_t>& wire);
// A Rant message into the PLC's bytes. False when the message does not fit the layout.
bool Decode(const Layout& l, const uint8_t* wire, size_t len, uint8_t* plc);

} // namespace blocks
