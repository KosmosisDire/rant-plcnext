using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Calls the function Name with Request on a rising Execute. Done or Error holds the outcome,
    // with Response written, until Execute falls. A StructBuilder's message, or no Request at all,
    // goes out as the function's type with the fields it names, the rest at their defaults.
    [FunctionBlock]
    public class RemoteFunction
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
        [Input]
        public bool Execute;
        [Input, DataType("ANY")]
        public Any Request;
        [Input, DataType("STRING")]
        public IecString80 TypeName;
        // An input the block writes the answer through, so a call that wants none leaves it unconnected.
        [Input, DataType("ANY")]
        public Any Response;
        [Output]
        public bool Busy;
        [Output]
        public bool Done;
        [Output]
        public bool Error;
        [Output, DataType("STRING")]
        public IecString80 ErrorText;

        int handle;

        [Initialization]
        public void __Init()
        {
            Name.ctor();
            TypeName.ctor();
            ErrorText.ctor();
        }

        [Execution]
        public unsafe void __Process()
        {
            handle = Runtime.Entity(Kind.RemoteFunction, handle, ref Name);
            int state = 0;
            // A message, or none at all, goes out as the target's type with the fields it names.
            bool message = Layout.IsMessage(Request) || Request.pValue == null;
            bool request = message || Layout.Describe(handle, 0, Request);
            if (Layout.Optional(handle, 1, Response))
                state = Runtime.Call(handle, Execute, false, request ? (UIntPtr)Request.pValue : UIntPtr.Zero,
                    request ? (int)Request.nLength : 0, UIntPtr.Zero, 0, (UIntPtr)Response.pValue, (int)Response.nLength,
                    message, ref TypeName);
            Busy = state == 1;
            Done = state == 2;
            Error = Runtime.Failed(handle, ref ErrorText) || state == 3;
        }
    }
}
