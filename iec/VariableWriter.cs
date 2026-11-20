using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Writes Value, or the fields of the message Msg built into Message, to the variable Name,
    // which another node owns, whenever it changes and every cycle Send is set. The first value
    // is where the program starts and is not written.
    [FunctionBlock]
    public class VariableWriter
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
        [Input, DataType("ANY")]
        public Any Value;
        [Input, DataType("UDINT")]
        public uint Message;
        [Input]
        public bool Send;
        [Output, DataType("DINT")]
        public int Count;
        [Output]
        public bool Error;
        [Output, DataType("STRING")]
        public IecString80 ErrorText;

        int handle;

        [Initialization]
        public void __Init()
        {
            Name.ctor();
            ErrorText.ctor();
        }

        [Execution]
        public unsafe void __Process()
        {
            handle = Runtime.Entity(Kind.VariableWriter, handle, ref Name);
            bool value = Message == 0 && Layout.Describe(handle, 0, Value);
            if (handle != 0)
                Count = Runtime.Publish(handle, value ? (UIntPtr)Value.pValue : UIntPtr.Zero, value ? (int)Value.nLength : 0,
                    Send, (int)Message);
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
