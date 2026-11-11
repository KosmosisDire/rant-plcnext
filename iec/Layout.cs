using System;
using System.Iec61131Lib;
using Eclr;

namespace Rant
{
    // Walks the type of an ANY pin with the eCLR's reflection and hands each node to the runtime,
    // once per type, since only managed code may read the type metadata.
    public class Layout
    {
        public static unsafe bool Describe(int handle, int which, Any value)
        {
            UIntPtr type = value.pRuntimeTypeHandle;
            if (handle == 0) return false;
            if (!Runtime.NeedsLayout(handle, which, type)) return true;
            Walk(handle, which, -1, null, type, 0);
            Runtime.LayoutDone(handle, which, type, (UIntPtr)value.pValue);
            return true;
        }

        static unsafe void Walk(int handle, int which, int parent, byte* name, UIntPtr type, int offset)
        {
            int code = TypeInfo.GetTypeCode(type);
            int flags = 0;
            if (Helper.IsEnum(type))
            {
                flags = Kind.Enum;
                code = Helper.GetEnumUnderlyingType(type);
            }
            else if (Eclr.Pcos.TypeInfo.IsIecString(type)) flags = Kind.String;
            else if (Eclr.Pcos.TypeInfo.IsIecArray(type)) flags = Kind.Array;
            int self = Runtime.Field(handle, which, parent, (UIntPtr)name, (UIntPtr)TypeInfo.GetNameSpace(type),
                (UIntPtr)TypeInfo.GetName(type), offset, TypeInfo.GetSize(type), code, flags);
            if (flags == Kind.Enum)
            {
                Options(handle, which, self, type, code);
                return;
            }
            // A string's header and an elementary type's constants are no fields of the value.
            if (flags == Kind.String || code != 17) return;
            int count = TypeInfo.GetFieldAmount(type);
            for (int i = 0; i < count; i++)
            {
                UIntPtr field = TypeInfo.GetFieldInfo(type, i);
                byte* fieldName = FieldInfo.GetName(field);
                if (flags == Kind.Array && !IsAnchor(fieldName))
                    Runtime.Bound(handle, which, self, (UIntPtr)fieldName, FieldInfo.GetOffset(field));
                else
                    Walk(handle, which, self, fieldName, FieldInfo.GetTypeInfo(field), offset + FieldInfo.GetOffset(field));
            }
        }

        // An enum's options are its literal fields. The eCLR tells no literal's value, so each is found by
        // asking the enum for a value's name: the one after the last first, since most count up from 0.
        static unsafe void Options(int handle, int which, int self, UIntPtr type, int code)
        {
            Type enumType = TypeInfo.GetTypeFromHandle(type);
            int next = 0;
            int count = TypeInfo.GetFieldAmount(type);
            for (int i = 0; i < count; i++)
            {
                UIntPtr field = TypeInfo.GetFieldInfo(type, i);
                if (!FieldInfo.IsLiteral(field)) continue;
                byte* name = FieldInfo.GetName(field);
                int value = next;
                if (!Names(enumType, code, value, name))
                {
                    value = 0;
                    while (value < ProbeLimit && !Names(enumType, code, value, name)) value++;
                    if (value == ProbeLimit) continue;
                }
                Runtime.Option(handle, which, self, (UIntPtr)name, value);
                next = value + 1;
            }
        }

        const int ProbeLimit = 1024;

        // GetName takes a value only boxed as the enum's own underlying type.
        static unsafe bool Names(Type enumType, int code, int value, byte* name)
        {
            object boxed;
            switch (code)
            {
                case 4: boxed = (sbyte)value; break;
                case 5: boxed = (byte)value; break;
                case 6: boxed = (short)value; break;
                case 7: boxed = (ushort)value; break;
                case 9: boxed = (uint)value; break;
                default: boxed = value; break;
            }
            string found = System.Enum.GetName(enumType, boxed);
            if (found == null) return false;
            int i = 0;
            for (; i < found.Length; i++)
                if (name[i] != (byte)found[i]) return false;
            return name[i] == 0;
        }

        static unsafe bool IsAnchor(byte* name)
        {
            return name[0] == 'A' && name[1] == 'n' && name[2] == 'c' && name[3] == 'h' && name[4] == 'o' &&
                name[5] == 'r' && name[6] == 0;
        }
    }
}
