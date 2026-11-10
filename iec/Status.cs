using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Raises the node's status condition Key while Active, at Level with Text, and clears it after.
    // A Clearable one may be cleared from another node: it stays down until Active goes FALSE, and
    // Cleared is set for the one cycle the clear arrives.
    [FunctionBlock]
    public class Status
    {
        [Input, DataType("STRING")]
        public IecString80 Key;
        [Input]
        public bool Active;
        [Input]
        public Level Level;
        [Input, DataType("STRING")]
        public IecString80 Text;
        [Input]
        public bool Clearable;
        [Output]
        public bool Cleared;
        [Output]
        public bool Error;
        [Output, DataType("STRING")]
        public IecString80 ErrorText;

        int handle;

        [Initialization]
        public void __Init()
        {
            Key.ctor();
            Text.ctor();
            ErrorText.ctor();
            Level = Level.Warn;
        }

        [Execution]
        public void __Process()
        {
            handle = Runtime.Entity(Kind.Status, handle, ref Key);
            Cleared = Runtime.Condition(handle, Active, (int)Level, Clearable, ref Text);
            Error = Runtime.Failed(handle, ref ErrorText);
        }
    }
}
