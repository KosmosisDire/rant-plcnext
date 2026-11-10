using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Offers the function Name. Called is set for the cycle a call's Request arrives, and the
    // next call of the block answers it with Response, or a later one while Hold is set.
    [FunctionBlock]
    public class FunctionDefinition
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
        [InOut, DataType("ANY")]
        public Any Request;
        [InOut, DataType("ANY")]
        public Any Response;
        [Input]
        public bool Hold;
        [Output]
        public bool Called;
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
            handle = Runtime.Entity(Kind.FunctionDefinition, handle, ref Name);
            Called = false;
            if (Layout.Describe(handle, 0, Request) && Layout.Describe(handle, 1, Response))
            {
                int served = Runtime.Serve(handle, (UIntPtr)Request.pValue, (int)Request.nLength, UIntPtr.Zero, 0,
                    (UIntPtr)Response.pValue, (int)Response.nLength, !Hold);
                Called = (served & Kind.ServeNew) != 0;
                Count = served >> 3;
            }
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
