using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Msg1 to Msg10 build a message of that many fields for a Value or Request pin: each Field a
    // field name or path in a STRING, each Value what it gets. Fields left out keep their defaults.
    public class MsgPair
    {
        // Adds the index'th field name and its value to the message at m.
        public static unsafe void Add(byte* m, int index, Any field, Any value)
        {
            Runtime.MsgAdd((UIntPtr)m, index, (UIntPtr)field.pValue, (int)field.nLength, Packed(field.pRuntimeTypeHandle),
                (UIntPtr)value.pValue, (int)value.nLength, Packed(value.pRuntimeTypeHandle));
        }

        // The eCLR type code, the Kind flags above it, and an array's element size above those. An
        // array's code is its element's, 0 when that is no elementary value.
        static unsafe int Packed(UIntPtr type)
        {
            if (type == UIntPtr.Zero) return 0;
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

    [Function]
    public static class Msg1
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg1,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1)
        {
            fixed (byte* m = &Msg1.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
            }
        }
    }

    [Function]
    public static class Msg2
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg2,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2)
        {
            fixed (byte* m = &Msg2.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
            }
        }
    }

    [Function]
    public static class Msg3
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg3,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3)
        {
            fixed (byte* m = &Msg3.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
            }
        }
    }

    [Function]
    public static class Msg4
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg4,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4)
        {
            fixed (byte* m = &Msg4.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
                MsgPair.Add(m, 4, Field4, Value4);
            }
        }
    }

    [Function]
    public static class Msg5
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg5,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5)
        {
            fixed (byte* m = &Msg5.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
                MsgPair.Add(m, 4, Field4, Value4);
                MsgPair.Add(m, 5, Field5, Value5);
            }
        }
    }

    [Function]
    public static class Msg6
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg6,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6)
        {
            fixed (byte* m = &Msg6.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
                MsgPair.Add(m, 4, Field4, Value4);
                MsgPair.Add(m, 5, Field5, Value5);
                MsgPair.Add(m, 6, Field6, Value6);
            }
        }
    }

    [Function]
    public static class Msg7
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg7,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6,
            [Input, DataType("ANY")] ref Any Field7, [Input, DataType("ANY")] ref Any Value7)
        {
            fixed (byte* m = &Msg7.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
                MsgPair.Add(m, 4, Field4, Value4);
                MsgPair.Add(m, 5, Field5, Value5);
                MsgPair.Add(m, 6, Field6, Value6);
                MsgPair.Add(m, 7, Field7, Value7);
            }
        }
    }

    [Function]
    public static class Msg8
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg8,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6,
            [Input, DataType("ANY")] ref Any Field7, [Input, DataType("ANY")] ref Any Value7,
            [Input, DataType("ANY")] ref Any Field8, [Input, DataType("ANY")] ref Any Value8)
        {
            fixed (byte* m = &Msg8.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
                MsgPair.Add(m, 4, Field4, Value4);
                MsgPair.Add(m, 5, Field5, Value5);
                MsgPair.Add(m, 6, Field6, Value6);
                MsgPair.Add(m, 7, Field7, Value7);
                MsgPair.Add(m, 8, Field8, Value8);
            }
        }
    }

    [Function]
    public static class Msg9
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg9,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6,
            [Input, DataType("ANY")] ref Any Field7, [Input, DataType("ANY")] ref Any Value7,
            [Input, DataType("ANY")] ref Any Field8, [Input, DataType("ANY")] ref Any Value8,
            [Input, DataType("ANY")] ref Any Field9, [Input, DataType("ANY")] ref Any Value9)
        {
            fixed (byte* m = &Msg9.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
                MsgPair.Add(m, 4, Field4, Value4);
                MsgPair.Add(m, 5, Field5, Value5);
                MsgPair.Add(m, 6, Field6, Value6);
                MsgPair.Add(m, 7, Field7, Value7);
                MsgPair.Add(m, 8, Field8, Value8);
                MsgPair.Add(m, 9, Field9, Value9);
            }
        }
    }

    [Function]
    public static class Msg10
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message Msg10,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6,
            [Input, DataType("ANY")] ref Any Field7, [Input, DataType("ANY")] ref Any Value7,
            [Input, DataType("ANY")] ref Any Field8, [Input, DataType("ANY")] ref Any Value8,
            [Input, DataType("ANY")] ref Any Field9, [Input, DataType("ANY")] ref Any Value9,
            [Input, DataType("ANY")] ref Any Field10, [Input, DataType("ANY")] ref Any Value10)
        {
            fixed (byte* m = &Msg10.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                MsgPair.Add(m, 1, Field1, Value1);
                MsgPair.Add(m, 2, Field2, Value2);
                MsgPair.Add(m, 3, Field3, Value3);
                MsgPair.Add(m, 4, Field4, Value4);
                MsgPair.Add(m, 5, Field5, Value5);
                MsgPair.Add(m, 6, Field6, Value6);
                MsgPair.Add(m, 7, Field7, Value7);
                MsgPair.Add(m, 8, Field8, Value8);
                MsgPair.Add(m, 9, Field9, Value9);
                MsgPair.Add(m, 10, Field10, Value10);
            }
        }
    }
}
