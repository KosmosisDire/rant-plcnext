using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Types;

namespace Rant
{
    // How much a log line or a status condition matters, with Rant's own numbers.
    [Enumeration]
    public enum Level : byte
    {
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
    }
}
