using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Runs the task Name with Request on a rising Execute, which takes a Msg block's message as
    // RemoteFunction does. Progress is written as it comes while Busy, and Cancel asks the provider
    // to stop. Done or Error holds the outcome, with Response written, until Execute falls.
    [FunctionBlock]
    public class RemoteTask
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
        [Input]
        public bool Execute;
        [Input]
        public bool Cancel;
        [Input, DataType("ANY")]
        public Any Request;
        [Input, DataType("STRING")]
        public IecString80 TypeName;
        [InOut, DataType("ANY")]
        public Any Progress;
        [InOut, DataType("ANY")]
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
            handle = Runtime.Entity(Kind.RemoteTask, handle, ref Name);
            int state = 0;
            // A message, or none at all, goes out as the target's type with the fields it names.
            bool message = Layout.IsMessage(Request) || Request.pValue == null;
            bool request = message || Layout.Describe(handle, 0, Request);
            if (Layout.Describe(handle, 1, Response) && Layout.Describe(handle, 2, Progress))
                state = Runtime.Call(handle, Execute, Cancel, request ? (UIntPtr)Request.pValue : UIntPtr.Zero,
                    request ? (int)Request.nLength : 0, (UIntPtr)Progress.pValue, (int)Progress.nLength,
                    (UIntPtr)Response.pValue, (int)Response.nLength, message,
                    ref TypeName);
            Busy = state == 1;
            Done = state == 2;
            Error = Runtime.Failed(handle, ref ErrorText) || state == 3;
        }
    }
}
