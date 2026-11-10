using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Publishes Value on Topic whenever it changes, and every cycle Send is set.
    [FunctionBlock]
    public class Publisher
    {
        [Input, DataType("STRING")]
        public IecString80 Topic;
        [InOut, DataType("ANY")]
        public Any Value;
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
            ErrorText.ctor();
        }

        [Execution]
        public unsafe void __Process()
        {
            handle = Runtime.Entity(Kind.Publisher, handle, ref Topic);
            if (Layout.Describe(handle, 0, Value))
                Count = Runtime.Publish(handle, (UIntPtr)Value.pValue, (int)Value.nLength, Send);
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
