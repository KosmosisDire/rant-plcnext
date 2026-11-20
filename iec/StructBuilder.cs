using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // StructBuilder1 to StructBuilder10 build a struct of that many fields for a Value or Request
    // pin: each Field a field name or path in a STRING, each Value what it gets. Fields left out
    // keep their defaults.
    public class StructPair
    {
        // Adds the index'th field name and its value to the message at m. A value that is another
        // builder's message or a struct variable adds each of its fields under the field name.
        public static unsafe void Add(byte* m, int index, Any field, Any value)
        {
            UIntPtr type = value.pRuntimeTypeHandle;
            if (value.pValue != null && type != UIntPtr.Zero)
            {
                if (Layout.IsMessage(value))
                {
                    Runtime.MsgNest((UIntPtr)m, index, (UIntPtr)field.pValue, (int)field.nLength,
                        Packed(field.pRuntimeTypeHandle), (UIntPtr)value.pValue, (int)value.nLength);
                    return;
                }
                if (IsStruct(type))
                {
                    Walk(m, index, field, (byte*)value.pValue, type, 0);
                    return;
                }
            }
            Runtime.MsgAdd((UIntPtr)m, index, (UIntPtr)field.pValue, (int)field.nLength, Packed(field.pRuntimeTypeHandle),
                (UIntPtr)value.pValue, (int)value.nLength, Packed(type));
        }

        // Adds each member of the struct at value under its path, and the members of a nested struct.
        static unsafe void Walk(byte* m, int index, Any field, byte* value, UIntPtr type, int depth)
        {
            int count = TypeInfo.GetFieldAmount(type);
            for (int i = 0; i < count; i++)
            {
                UIntPtr member = TypeInfo.GetFieldInfo(type, i);
                UIntPtr memberType = FieldInfo.GetTypeInfo(member);
                byte* at = value + FieldInfo.GetOffset(member);
                Runtime.MsgPath(depth, (UIntPtr)FieldInfo.GetName(member));
                if (IsStruct(memberType))
                    Walk(m, index, field, at, memberType, depth + 1);
                else
                    Runtime.MsgLeaf((UIntPtr)m, index, (UIntPtr)field.pValue, (int)field.nLength,
                        Packed(field.pRuntimeTypeHandle), depth + 1, (UIntPtr)at, TypeInfo.GetSize(memberType),
                        Packed(memberType));
            }
        }

        static bool IsStruct(UIntPtr type)
        {
            return TypeInfo.GetTypeCode(type) == 17 && !Helper.IsEnum(type) && !Eclr.Pcos.TypeInfo.IsIecString(type)
                && !Eclr.Pcos.TypeInfo.IsIecArray(type);
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
    public static class StructBuilder1
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder1,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1)
        {
            fixed (byte* m = &StructBuilder1.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
            }
        }
    }

    [Function]
    public static class StructBuilder2
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder2,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2)
        {
            fixed (byte* m = &StructBuilder2.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
            }
        }
    }

    [Function]
    public static class StructBuilder3
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder3,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3)
        {
            fixed (byte* m = &StructBuilder3.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
            }
        }
    }

    [Function]
    public static class StructBuilder4
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder4,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4)
        {
            fixed (byte* m = &StructBuilder4.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
                StructPair.Add(m, 4, Field4, Value4);
            }
        }
    }

    [Function]
    public static class StructBuilder5
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder5,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5)
        {
            fixed (byte* m = &StructBuilder5.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
                StructPair.Add(m, 4, Field4, Value4);
                StructPair.Add(m, 5, Field5, Value5);
            }
        }
    }

    [Function]
    public static class StructBuilder6
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder6,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6)
        {
            fixed (byte* m = &StructBuilder6.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
                StructPair.Add(m, 4, Field4, Value4);
                StructPair.Add(m, 5, Field5, Value5);
                StructPair.Add(m, 6, Field6, Value6);
            }
        }
    }

    [Function]
    public static class StructBuilder7
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder7,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6,
            [Input, DataType("ANY")] ref Any Field7, [Input, DataType("ANY")] ref Any Value7)
        {
            fixed (byte* m = &StructBuilder7.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
                StructPair.Add(m, 4, Field4, Value4);
                StructPair.Add(m, 5, Field5, Value5);
                StructPair.Add(m, 6, Field6, Value6);
                StructPair.Add(m, 7, Field7, Value7);
            }
        }
    }

    [Function]
    public static class StructBuilder8
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder8,
            [Input, DataType("ANY")] ref Any Field1, [Input, DataType("ANY")] ref Any Value1,
            [Input, DataType("ANY")] ref Any Field2, [Input, DataType("ANY")] ref Any Value2,
            [Input, DataType("ANY")] ref Any Field3, [Input, DataType("ANY")] ref Any Value3,
            [Input, DataType("ANY")] ref Any Field4, [Input, DataType("ANY")] ref Any Value4,
            [Input, DataType("ANY")] ref Any Field5, [Input, DataType("ANY")] ref Any Value5,
            [Input, DataType("ANY")] ref Any Field6, [Input, DataType("ANY")] ref Any Value6,
            [Input, DataType("ANY")] ref Any Field7, [Input, DataType("ANY")] ref Any Value7,
            [Input, DataType("ANY")] ref Any Field8, [Input, DataType("ANY")] ref Any Value8)
        {
            fixed (byte* m = &StructBuilder8.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
                StructPair.Add(m, 4, Field4, Value4);
                StructPair.Add(m, 5, Field5, Value5);
                StructPair.Add(m, 6, Field6, Value6);
                StructPair.Add(m, 7, Field7, Value7);
                StructPair.Add(m, 8, Field8, Value8);
            }
        }
    }

    [Function]
    public static class StructBuilder9
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder9,
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
            fixed (byte* m = &StructBuilder9.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
                StructPair.Add(m, 4, Field4, Value4);
                StructPair.Add(m, 5, Field5, Value5);
                StructPair.Add(m, 6, Field6, Value6);
                StructPair.Add(m, 7, Field7, Value7);
                StructPair.Add(m, 8, Field8, Value8);
                StructPair.Add(m, 9, Field9, Value9);
            }
        }
    }

    [Function]
    public static class StructBuilder10
    {
        [Execution]
        public static unsafe void __Process([Output] ref Message StructBuilder10,
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
            fixed (byte* m = &StructBuilder10.data.Anchor)
            {
                Runtime.MsgBegin((UIntPtr)m);
                StructPair.Add(m, 1, Field1, Value1);
                StructPair.Add(m, 2, Field2, Value2);
                StructPair.Add(m, 3, Field3, Value3);
                StructPair.Add(m, 4, Field4, Value4);
                StructPair.Add(m, 5, Field5, Value5);
                StructPair.Add(m, 6, Field6, Value6);
                StructPair.Add(m, 7, Field7, Value7);
                StructPair.Add(m, 8, Field8, Value8);
                StructPair.Add(m, 9, Field9, Value9);
                StructPair.Add(m, 10, Field10, Value10);
            }
        }
    }
}
