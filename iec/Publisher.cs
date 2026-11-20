using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Publishes Value on Topic whenever it changes, and every cycle Send is set. A Msg block's
    // message goes out as the readers' type, or the one TypeName names when they have several.
    [FunctionBlock]
    public class Publisher
    {
        [Input, DataType("STRING")]
        public IecString80 Topic;
        [Input, DataType("ANY")]
        public Any Value;
        [Input, DataType("STRING")]
        public IecString80 TypeName;
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
            Topic.ctor();
            TypeName.ctor();
            ErrorText.ctor();
        }

        [Execution]
        public unsafe void __Process()
        {
            handle = Runtime.Entity(Kind.Publisher, handle, ref Topic);
            bool message = Layout.IsMessage(Value);
            bool value = message || Layout.Describe(handle, 0, Value);
            if (handle != 0)
                Count = Runtime.Publish(handle, value ? (UIntPtr)Value.pValue : UIntPtr.Zero, value ? (int)Value.nLength : 0,
                    Send, message, ref TypeName);
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
