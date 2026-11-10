using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Names the node every Rant block of this PLC shares. Without one the node is plc_ and the
    // host name, in domain 0. A second Node block naming it otherwise sets Error.
    [FunctionBlock]
    public class Node
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
        [Input, DataType("INT")]
        public short Domain;
        [Output, DataType("DINT")]
        public int Peers;
        [Output]
        public bool Error;
        [Output, DataType("STRING")]
        public IecString80 ErrorText;

        int owner;

        [Initialization]
        public void __Init()
        {
            Name.ctor();
            ErrorText.ctor();
        }

        [Execution]
        public void __Process()
        {
            int peers = Runtime.Configure(ref owner, ref Name, Domain, ref ErrorText);
            Error = peers < 0;
            Peers = Error ? 0 : peers;
        }
    }
}
