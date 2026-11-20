using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Writes the value of the variable Name, which another node owns, into Value. Received is set
    // for the cycle it changes.
    [FunctionBlock]
    public class VariableReader
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
            handle = Runtime.Entity(Kind.VariableReader, handle, ref Name);
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
