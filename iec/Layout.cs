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
            Runtime.LayoutDone(handle, which, type);
            return true;
        }

        static unsafe void Walk(int handle, int which, int parent, byte* name, UIntPtr type, int offset)
        {
            int code = TypeInfo.GetTypeCode(type);
            int flags = 0;
            if (Eclr.Pcos.TypeInfo.IsIecString(type)) flags = Kind.String;
            else if (Eclr.Pcos.TypeInfo.IsIecArray(type)) flags = Kind.Array;
            int self = Runtime.Field(handle, which, parent, (UIntPtr)name, (UIntPtr)TypeInfo.GetNameSpace(type),
                (UIntPtr)TypeInfo.GetName(type), offset, TypeInfo.GetSize(type), code, flags);
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

        static unsafe bool IsAnchor(byte* name)
        {
            return name[0] == 'A' && name[1] == 'n' && name[2] == 'c' && name[3] == 'h' && name[4] == 'o' &&
                name[5] == 'r' && name[6] == 0;
        }
    }
}
