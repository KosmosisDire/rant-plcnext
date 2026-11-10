using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Offers the task Name, one call at a time. Started is set for the cycle a call's Request
    // arrives and Running until a later cycle ends it: Complete with Response, or Fail with Reason.
    // Progress goes to the caller whenever it changes, and Cancelled says the caller wants it to stop.
    [FunctionBlock]
    public class TaskDefinition
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
        [InOut, DataType("ANY")]
        public Any Request;
        [InOut, DataType("ANY")]
        public Any Progress;
        [InOut, DataType("ANY")]
        public Any Response;
        [Input]
        public bool Complete;
        [Input]
        public bool Fail;
        [Input, DataType("STRING")]
        public IecString80 Reason;
        [Output]
        public bool Started;
        [Output]
        public bool Running;
        [Output]
        public bool Cancelled;
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
            Reason.ctor();
            ErrorText.ctor();
        }

        [Execution]
        public unsafe void __Process()
        {
            handle = Runtime.Entity(Kind.TaskDefinition, handle, ref Name);
            Started = false;
            if (Layout.Describe(handle, 0, Request) && Layout.Describe(handle, 1, Response) &&
                Layout.Describe(handle, 2, Progress))
            {
                if (Fail && Running) Runtime.Reject(handle, ref Reason);
                int served = Runtime.Serve(handle, (UIntPtr)Request.pValue, (int)Request.nLength,
                    (UIntPtr)Progress.pValue, (int)Progress.nLength, (UIntPtr)Response.pValue, (int)Response.nLength,
                    Complete);
                Started = (served & Kind.ServeNew) != 0;
                Running = (served & Kind.ServeHeld) != 0;
                Cancelled = (served & Kind.ServeCancel) != 0;
                Count = served >> 3;
            }
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
