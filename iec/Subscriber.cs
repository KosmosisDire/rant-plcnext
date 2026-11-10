using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Writes the newest value of Topic into Value. Received is set for the cycle one arrives.
    [FunctionBlock]
    public class Subscriber
    {
        [Input, DataType("STRING")]
        public IecString80 Topic;
        [InOut, DataType("ANY")]
        public Any Value;
        [Output]
        public bool Received;
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
            handle = Runtime.Entity(Kind.Subscriber, handle, ref Topic);
            Received = false;
            if (Layout.Describe(handle, 0, Value))
            {
                int taken = Runtime.Take(handle, (UIntPtr)Value.pValue, (int)Value.nLength);
                Received = (taken & 1) != 0;
                Count = taken >> 1;
            }
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
