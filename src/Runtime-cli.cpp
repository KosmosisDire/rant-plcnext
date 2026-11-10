// The native side of iec/Runtime.cs, forwarding to blocks/runtime and mirror/mirror.
#include "eclr.h"
#include "Rant.h"

#include "blocks/runtime.h"
#include "mirror/mirror.h"

#include <string>

namespace {

std::string Text(pcoslib::IecString80* s)
{
    return s ? std::string(s->Begin(), s->GetLength()) : std::string();
}

uint8_t* Bytes(UIntPtr p) { return reinterpret_cast<uint8_t*>(p); }
const char* Chars(UIntPtr p) { return reinterpret_cast<const char*>(p); }

// Writes why into text, cut to its 80 bytes, and returns whether there is a why.
bool Report(const std::string& why, pcoslib::IecString80* text)
{
    if (text) text->Assign(why.c_str(), why.size() < 80 ? static_cast<unsigned>(why.size()) : 80u);
    return !why.empty();
}

} // namespace

Int32 __PInvoke__ Rant::Runtime::Configure(pcoslib::IecString80* p0, Int32 p1)
{
    return blocks::Configure(Text(p0), p1);
}

Int32 __PInvoke__ Rant::Runtime::Entity(Int32 p0, Int32 p1, pcoslib::IecString80* p2)
{
    return blocks::Entity(p0, p1, Text(p2));
}

Boolean __PInvoke__ Rant::Runtime::NeedsLayout(Int32 p0, Int32 p1, UIntPtr p2)
{
    return blocks::NeedsLayout(p0, p1, p2);
}

Int32 __PInvoke__ Rant::Runtime::Field(Int32 p0, Int32 p1, Int32 p2, UIntPtr p3, UIntPtr p4, UIntPtr p5, Int32 p6,
                                       Int32 p7, Int32 p8, Int32 p9)
{
    return blocks::Field(p0, p1, p2, Chars(p3), Chars(p4), Chars(p5), p6, p7, p8, p9);
}

void __PInvoke__ Rant::Runtime::Bound(Int32 p0, Int32 p1, Int32 p2, UIntPtr p3, Int32 p4)
{
    blocks::Bound(p0, p1, p2, Chars(p3), p4);
}

void __PInvoke__ Rant::Runtime::LayoutDone(Int32 p0, Int32 p1, UIntPtr p2)
{
    blocks::LayoutDone(p0, p1, p2);
}

Int32 __PInvoke__ Rant::Runtime::Publish(Int32 p0, UIntPtr p1, Int32 p2, Boolean p3)
{
    return blocks::Publish(p0, Bytes(p1), p2, p3);
}

Int32 __PInvoke__ Rant::Runtime::Take(Int32 p0, UIntPtr p1, Int32 p2)
{
    return blocks::Take(p0, Bytes(p1), p2);
}

Int32 __PInvoke__ Rant::Runtime::Share(Int32 p0, UIntPtr p1, Int32 p2)
{
    return blocks::Share(p0, Bytes(p1), p2);
}

Int32 __PInvoke__ Rant::Runtime::Serve(Int32 p0, UIntPtr p1, Int32 p2, UIntPtr p3, Int32 p4, UIntPtr p5, Int32 p6,
                                       Boolean p7)
{
    return blocks::Serve(p0, Bytes(p1), p2, Bytes(p3), p4, Bytes(p5), p6, p7);
}

void __PInvoke__ Rant::Runtime::Reject(Int32 p0, pcoslib::IecString80* p1)
{
    blocks::Reject(p0, Text(p1));
}

Int32 __PInvoke__ Rant::Runtime::Call(Int32 p0, Boolean p1, Boolean p2, UIntPtr p3, Int32 p4, UIntPtr p5, Int32 p6,
                                      UIntPtr p7, Int32 p8)
{
    return blocks::Call(p0, p1, p2, Bytes(p3), p4, Bytes(p5), p6, Bytes(p7), p8);
}

Boolean __PInvoke__ Rant::Runtime::Failed(Int32 p0, pcoslib::IecString80* p1)
{
    return Report(blocks::Failure(p0), p1);
}

Int32 __PInvoke__ Rant::Runtime::Mirror(pcoslib::IecString80* p0, Boolean p1, Boolean p2, Int32 p3)
{
    plc::Config c;
    c.prefix = Text(p0);
    while (!c.prefix.empty() && c.prefix.front() == '/') c.prefix.erase(0, 1);
    while (!c.prefix.empty() && c.prefix.back() == '/') c.prefix.pop_back();
    c.publishLocals = p1;
    c.probeForcable = p2;
    c.structStringCap = static_cast<uint16_t>(p3 < 1 ? 1 : p3 > 4096 ? 4096 : p3);
    return plc::Mirror(c);
}

Boolean __PInvoke__ Rant::Runtime::MirrorFailed(pcoslib::IecString80* p0)
{
    return Report(plc::MirrorFailure(), p0);
}

Rant::Runtime::Runtime()
{
}

void __PInvoke__ Rant::Runtime::ctor()
{
    new (this) Rant::Runtime();
}
