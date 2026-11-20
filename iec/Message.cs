using System;
using System.Iec61131Lib;
using System.Runtime.InteropServices;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // A message's bytes, laid out by src/blocks/message.cpp, whose MESSAGE_BYTES is this size.
    [Array(1)]
    [ArrayDimension(0, 0, 1107)]
    [DataType("BYTE")]
    [StructLayout(LayoutKind.Explicit, Size = 1108)]
    public struct MessageBytes
    {
        [FieldOffset(0)]
        [DataType("BYTE")]
        public byte Anchor;
        public const int LB = 0;
        public const int UB = 1107;
    }

    // What a Msg block returns: field names and their values, for a Value or Request pin.
    [Structure]
    public struct Message
    {
        public MessageBytes data;
    }
}
