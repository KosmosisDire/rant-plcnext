using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Builds a message for a block's Message input: the type name, '' for the target's own, then
    // each field name and its value. Engineer runs Msg(a, b, c) as Msg(Msg(a, b), c), so every
    // call adds one input to the message the call before it returned.
    [Function, Extensible]
    public static class Msg
    {
        [Execution]
        public static unsafe void __Process([Output, DataType("UDINT")] ref uint Msg,
                                            [Input, DataType("ANY")] ref Any IN1, [Input, DataType("ANY")] ref Any IN2)
        {
            Msg = (uint)Runtime.MsgStep((UIntPtr)IN1.pValue, (int)IN1.nLength, Packed(IN1.pRuntimeTypeHandle),
                                        (UIntPtr)IN2.pValue, (int)IN2.nLength, Packed(IN2.pRuntimeTypeHandle));
        }

        // The eCLR type code, the Kind flags above it, and an array's element size above those. An
        // array's code is its element's, 0 when that is no elementary value.
        static unsafe int Packed(UIntPtr type)
        {
            if (Helper.IsEnum(type)) return Helper.GetEnumUnderlyingType(type) | Kind.Enum << 8;
            if (Eclr.Pcos.TypeInfo.IsIecString(type)) return Kind.String << 8;
            if (!Eclr.Pcos.TypeInfo.IsIecArray(type)) return TypeInfo.GetTypeCode(type);
            int count = TypeInfo.GetFieldAmount(type);
            for (int i = 0; i < count; i++)
            {
                UIntPtr field = TypeInfo.GetFieldInfo(type, i);
                if (!Layout.IsAnchor(FieldInfo.GetName(field))) continue;
                UIntPtr element = FieldInfo.GetTypeInfo(field);
                int code = TypeInfo.GetTypeCode(element);
                bool elementary = code != 17 && !Helper.IsEnum(element) && !Eclr.Pcos.TypeInfo.IsIecString(element)
                    && !Eclr.Pcos.TypeInfo.IsIecArray(element);
                return (elementary ? code : 0) | Kind.Array << 8 | TypeInfo.GetSize(element) << 16;
            }
            return Kind.Array << 8;
        }
    }
}
