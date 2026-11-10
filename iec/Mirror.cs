using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Publishes every PLC variable as a variable of this PLC's node under Prefix, read write and
    // forceable where the PLC allows it. A change of an input starts the mirror over.
    [FunctionBlock]
    public class Mirror
    {
        [Input, DataType("STRING")]
        public IecString80 Prefix;
        [Input]
        public bool Locals;
        [Input]
        public bool Forcing;
        [Input, DataType("INT")]
        public short StringCap;
        [Output, DataType("DINT")]
        public int Variables;
        [Output]
        public bool Error;
        [Output, DataType("STRING")]
        public IecString80 ErrorText;

        [Initialization]
        public void __Init()
        {
            Prefix.ctor();
            Prefix.Init("Plc");
            Forcing = true;
            StringCap = 80;
            ErrorText.ctor();
        }

        [Execution]
        public void __Process()
        {
            Variables = Runtime.Mirror(ref Prefix, Locals, Forcing, StringCap);
            Error = Runtime.MirrorFailed(ref ErrorText);
        }
    }
}
