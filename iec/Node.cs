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
    // host name, in domain 0.
    [FunctionBlock]
    public class Node
    {
        [Input, DataType("STRING")]
        public IecString80 Name;
        [Input, DataType("INT")]
        public short Domain;
        [Output, DataType("DINT")]
        public int Peers;

        [Initialization]
        public void __Init()
        {
            Name.ctor();
        }

        [Execution]
        public void __Process()
        {
            Peers = Runtime.Configure(ref Name, Domain);
        }
    }
}
