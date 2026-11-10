using System;
using System.Iec61131Lib;
using Eclr;
using Iec61131.Engineering.Prototypes.Common;
using Iec61131.Engineering.Prototypes.Methods;
using Iec61131.Engineering.Prototypes.Types;
using Iec61131.Engineering.Prototypes.Variables;

namespace Rant
{
    // Sends Text as one log line of the node at Level, on each rising Execute.
    [FunctionBlock]
    public class LogLine
    {
        [Input]
        public bool Execute;
        [Input]
        public Level Level;
        [Input, DataType("STRING")]
        public IecString80 Text;
        [Output, DataType("DINT")]
        public int Count;
        [Output]
        public bool Error;
        [Output, DataType("STRING")]
        public IecString80 ErrorText;

        bool lastExecute;
        bool waiting;

        [Initialization]
        public void __Init()
        {
            Text.ctor();
            ErrorText.ctor();
            Level = Level.Info;
        }

        [Execution]
        public void __Process()
        {
            if (Execute && !lastExecute) waiting = true;
            lastExecute = Execute;
            if (!waiting) return;
            // 0 while the queue is busy, so the line goes next cycle.
            int queued = Runtime.Log((int)Level, ref Text, ref ErrorText);
            if (queued == 0) return;
            waiting = false;
            Error = queued < 0;
            if (!Error) Count++;
        }
    }
}
