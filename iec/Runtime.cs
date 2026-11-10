using System;
using System.Iec61131Lib;
using Eclr;

namespace Rant
{
    // The entity kinds, apart from Runtime since a native class holds no constants.
    public class Kind
    {
        public const int Publisher = 1;
        public const int Subscriber = 2;
        public const int FunctionDefinition = 3;
        public const int RemoteFunction = 4;
        public const int VariableDefinition = 5;
        public const int RemoteVariable = 6;
        public const int TaskDefinition = 7;
        public const int RemoteTask = 8;

        // What Runtime.Serve reports beside the calls ended times eight.
        public const int ServeNew = 1;
        public const int ServeHeld = 2;
        public const int ServeCancel = 4;

        // What a layout node is beside its eCLR type code.
        public const int String = 1;
        public const int Array = 2;
    }

    // The C++ runtime behind the blocks: one node, its thread, and an entity per block. A block
    // keeps the handle it got back, and every call returns at once without waiting on the network.
    [Native]
    public class Runtime
    {
        // Sets the node's name and domain, reopening it when either changes. Returns the peers it sees,
        // or -1 with why in text when another Node block named it first.
        public static int Configure(ref int owner, ref IecString80 name, int domain, ref IecString80 text)
        {
            return 0;
        }

        // The entity of kind named name: handle itself while both still match, else a new handle.
        public static int Entity(int kind, int handle, ref IecString80 name)
        {
            return 0;
        }

        // Whether the entity still needs the layout of its value or request (which 0), response (1)
        // or progress (2).
        public static bool NeedsLayout(int handle, int which, UIntPtr type)
        {
            return false;
        }

        // One node of a layout, its parent's index or -1, and returns its own index.
        public static int Field(int handle, int which, int parent, UIntPtr name, UIntPtr typeSpace, UIntPtr typeName,
            int offset, int size, int code, int flags)
        {
            return 0;
        }

        // One bound of an array node: the eCLR reports LB and UB constants in the offset slot.
        public static void Bound(int handle, int which, int node, UIntPtr name, int value)
        {
        }

        // The layout is whole: the entity turns it into a Rant schema. data is the value's memory.
        public static void LayoutDone(int handle, int which, UIntPtr type, UIntPtr data)
        {
        }

        // Publisher: queues the value when it changed or send is set. Returns the values sent.
        public static int Publish(int handle, UIntPtr data, int length, bool send)
        {
            return 0;
        }

        // Subscriber: copies a value that arrived since the last call. Returns the values received
        // times two, plus one when this call copied one.
        public static int Take(int handle, UIntPtr data, int length)
        {
            return 0;
        }

        // VariableDefinition and RemoteVariable: sends the value when the program changed it, else
        // copies one from the mesh. Returns the values received times two, plus one when it copied one.
        public static int Share(int handle, UIntPtr data, int length)
        {
            return 0;
        }

        // FunctionDefinition and TaskDefinition: answers the call in hand when complete is set, sends
        // progress when it changed, else takes the next request. Returns Serve bits plus calls ended times 8.
        public static int Serve(int handle, UIntPtr request, int requestLength, UIntPtr progress, int progressLength,
            UIntPtr response, int responseLength, bool complete)
        {
            return 0;
        }

        // TaskDefinition: fails the call in hand with reason.
        public static void Reject(int handle, ref IecString80 reason)
        {
        }

        // RemoteFunction and RemoteTask: starts a call on execute, then reports 1 busy, 2 done with
        // response written, 3 failed, 0 idle. cancel asks the provider of a running task to stop.
        public static int Call(int handle, bool execute, bool cancel, UIntPtr request, int requestLength,
            UIntPtr progress, int progressLength, UIntPtr response, int responseLength)
        {
            return 0;
        }

        // Writes why the entity cannot work into text, and returns whether it cannot.
        public static bool Failed(int handle, ref IecString80 text)
        {
            return false;
        }

        // Starts the mirror, and starts it over when a setting changed. Returns the variables it
        // publishes.
        public static int Mirror(ref IecString80 prefix, bool locals, bool forcing, int stringCap)
        {
            return 0;
        }

        // Writes why the mirror publishes nothing into text, and returns whether it does not.
        public static bool MirrorFailed(ref IecString80 text)
        {
            return false;
        }
    }
}
