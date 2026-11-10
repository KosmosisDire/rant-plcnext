using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // The variable Name another node owns. Its value lands in Value, with Received set for that
    // cycle, and a change the program makes to Value is written to the owner.
    [FunctionBlock]
    public class RemoteVariable
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
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
            Name.ctor();
            ErrorText.ctor();
        }

        [Execution]
        public unsafe void __Process()
        {
            handle = Runtime.Entity(Kind.RemoteVariable, handle, ref Name);
            Received = false;
            if (Layout.Describe(handle, 0, Value))
            {
                int shared = Runtime.Share(handle, (UIntPtr)Value.pValue, (int)Value.nLength);
                Received = (shared & 1) != 0;
                Count = shared >> 1;
            }
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
