# Design

How the library works inside, for whoever maintains it. docs/blocks.md says how to use the
blocks, docs/usage.md how to build and test.

## Layout

| file | does |
|---|---|
| `iec/` | the blocks in C#: their pins, and the walk of an ANY pin's type (`Layout.cs`) |
| `src/Rant-cli.cpp`, `src/Runtime-cli.cpp` | the eCLR hooks and the native side of `iec/Runtime.cs` |
| `src/blocks/runtime.*` | the shared node, its thread, and an entity per block |
| `src/blocks/layout.*` | a block's value between IEC memory and the wire |
| `src/mirror/mirror.*` | the Mirror block's thread: browse, create the variables, then the event loop |
| `src/mirror/sampler.*` | the slow and fast GDS subscriptions and the sample budget |
| `src/mirror/sync.*` | keeps the PLC and the mesh agreeing on values and forces |
| `src/mirror/config.h` | the mirror's settings: the block's inputs and the tuning below |
| `src/plc/browse.*` | walks `Arp.Plc.Eclr` and builds one `GdsVar` per published variable |
| `src/plc/model.*` | `GdsVar` and `Member`: how a PLC value maps onto a Rant value |
| `src/plc/codec.*` | bytes to and from `RscVariant` |
| `src/plc/tasks.*` | program to ESM task to cycle time, from the project's ESM configuration |
| `src/core/log.*` | the two log sinks |
| `src/RantMath-cli.cpp` | the native side of the math functions, which Rant Math generates |

Includes are relative to `src/`. `plc/` and `core/` know nothing of the files above them.

## One node

Every block and the mirror share one node, which `blocks/runtime` opens on a thread of its
own (`rant_blocks`) half a second after the first block runs. A block's call only copies
under a lock and never waits on the network.

- The node needs a topic slot per entity, so it opens again with more when the blocks or the
  mirror's variables outgrow it. A browsing mirror holds the first open back for up to 20 s,
  so the node opens once with room for its variables.
- The mirror holds the node through `MirrorNode`. When the node opens again, the old one is
  closed under it, its handles answer `NoTopic`, and the mirror creates its variables anew.
- A thread made from a PLC task inherits its real time priority and its core, so each
  thread drops both first (`Demote`).
- The eCLR unloads the library with the program, which stops the mirror and then closes the
  node.

## Math

Rant Math's `tools/bindgen` writes `bindings/plcnext/RantMath.cs`, which the C# part compiles,
and `rant_math_natives.inc`, which `src/RantMath-cli.cpp` includes. Each function is managed C#
that hands the address of each value to a static of the native class `RantMath`, and the C++
copies the values out of IEC memory, calls the C library and copies the answer back.

- The native builder refuses a struct by value, so every value goes by address.
- Engineer's code generator fails on a library function whose struct input or answer is by
  value, so the C# takes a struct input by `ref` and gives a struct answer through an output
  `ref` named after the function, as Engineer's own functions are compiled. Scalars stay by value.
- An IEC function has one answer and no other output, so a call with more is a function block.
- eCLR lays a struct out as C does, `Axes` (three SINT then an LREAL) included, so the C++ copies
  bytes as they are. The copies are byte copies since IEC memory keeps no C alignment.

## Which variables are published

Every variable of `Arp.Plc.Eclr` the browse service lists, except:

- the runtime's diagnostic mirrors (`AXIO_DIAG_*`, `PND_S1_*`, `RTC` and the rest),
  compiler internal names (a leading `_`), the `BLANK` placeholder and the string table
- function block instances: their members are the block's internals
- a program's local variables, unless `Locals` is set

The runtime reports scope by structure, not by a flag: a global is a root of the component
and a program's variables are its children. A program variable is exposed when it is an IN
or OUT port or ticked for OPC UA, HMI or Proficloud in PLCnext Engineer
(`StandardAttributes`). Anything else is a local, the program's own working state. Seen on
the bench: locals report attributes 0, HMI ticked locals `Ehmi`, and globals carry a custom
`GlobalAttribute` rather than a scope flag. Each skipped local is logged once at start.

## Names

- A variable's name is its GDS name without `Arp.Plc.Eclr/`, with `.` and `_` turned into
  `/`: `MainInstance.motor_speed` is `MainInstance/motor/speed`.
- The block's `Prefix` and `/` go in front. The whole name, with the `@set` channel suffix,
  must fit `RANT_TOPIC_NAME_MAX` (64 bytes).
- The mapping is not one to one (`Default_pusher` and `Default.pusher` collide): the first
  claim wins and the loser is logged.

## Types

| PLC | on the mesh |
|---|---|
| a scalar | its bare type: `bool`, `i16`, `f32` |
| an array of scalars | a fixed array, one size per dimension: `i16[7]`, `f32[3][3]`, row major |
| a string | an unbounded `string` |
| TIME, LTIME, LTOD | `Duration`, microseconds |
| LDATE, LDT | `Timestamp`, microseconds since 1970 |
| a data struct | a struct named by its PLC type, its members as the fields |

- A bare type keeps the shape as the identity, so the same type from any node in any
  language has the same bytes and hash.
- A struct takes its PLC type name (`SafetyStatus`). When the node already holds that name
  with another shape, a standard type say, it falls back to a hash of the shape
  (`S9f13c0a21e4b7d05`) and the refusal is logged. A nested struct is defined ahead of its
  user by the same rule, since Rant refuses an unnamed struct. A PLC struct named and shaped
  like a standard type (`Double3`, `Quaternion`) becomes that type, nested or not.
- Rant spells fields snake_case, so `motorSpeed` and `motor_speed` would be one field. A
  member whose spelling is taken gets a number (`motor_speed_2`).
- A string member is a capped slot (`StringCap`) so the struct stays one fixed block. A
  longer value is cut and logged once.
- The time units come from the PLCnext data type tables: TIME is 32 bit milliseconds, LTIME,
  LDATE, LDT and LTOD are 64 bit nanoseconds, LDATE and LDT from 1970, LTOD from midnight.
  Below a microsecond is cut. `CopyTo` refuses these RSC types, so the codec reads and writes
  them raw. Not yet seen on a controller.
- A struct nests at most `RANT_SCHEMA_MAX_DEPTH` deep. A deeper member is skipped, not the
  whole struct.

## The codec

- Scalars are raw little endian bytes, arrays the packed elements, a string `[u32
  length][bytes]`, a struct its members packed at their `Member::offset` in declaration order.
- PLCnext's RSC contract: every element of an array or struct variant must be pulled off the
  stream, or every value after it decodes as garbage. So a member the mirror cannot publish
  (an FB instance, an array of structs, an internal name) is `Skipped`: absent from the
  schema, still drained.
- The member tree and the RSC struct reader follow the same declaration order, so the Nth
  member is the Nth field. A field count or width mismatch rejects the whole value.
- PLCnext cannot write a struct whole, so a remote write becomes one item per leaf, addressed
  by its GDS path, sent in one batched `Write`. Each member decodes from its own offset, so a
  bad member drops alone.

## The mirror thread

The Mirror block starts one thread (`rant_mirror` in `top -H`) on its first call, and a
change of an input makes it start over with a new browse. It takes the GDS services from the
`ServiceManager`. After start up each activity keeps its own deadline, and between deadlines
the thread sleeps until the next one or a write from the mesh.

| every | activity |
|---|---|
| 20 ms (`loopMs`) | drain the fast ring, only while something is watched |
| the slow period over the chunks | read one slow chunk |
| 200 ms | reflect PLC side forces |
| 500 ms | move variables on and off the fast ring, check the node is still the same |
| on a write from the mesh | write or force it into the PLC |

A deadline missed entirely is skipped, not replayed as a burst.

Once its loop runs the thread is `SCHED_FIFO` at priority 5 (`schedPrio`), so it wakes on
time on a busy controller. The PLC's tasks run at 50 and up. The runtime holds
`CAP_SYS_NICE`, and the kernel runs with RT throttling off, so a real time thread that never
blocked would starve its core. The loop always returns to a blocking wait and every activity
is bounded, and the browse and variable creation run time shared, before the priority is
raised.

The tuning names in brackets are fields of `src/mirror/config.h`, which hold their defaults.

## Sampling

Two kinds of GDS subscription feed the thread:

- **Slow**: every variable, the HighPerformance kind, a double buffer holding only the latest
  sample. Read in chunks of 32 values (`slowChunk`), one subscription each, spread evenly
  over the slow period of 100 ms (`slowRateUs`), so no single read holds the loop for long.
- **Fast**: only the variables some remote is matched to right now, the Recording kind, a
  ring that keeps every sample between drains. A watched variable changing at task rate
  publishes every change though the mirror drains it far slower. Membership is reconciled
  every 500 ms, since each change costs a full `Resubscribe`.

The two cannot merge: a recording entry costs about 0.6 us per sample on the ESM task
itself, and about 2000 entries at 1 ms tripped hc10's watchdog.

### Task rates

A subscription is task triggered: each variable is copied by its own ESM task at the end of
the task's cycle. A requested rate is rounded down, per task, to a multiple of that task's
cycle, and never faster. So one rate already samples every variable at its own task's rate,
and the request only needs to be no slower than the fastest task, the floor (`fastRateUs`
overrides it).

The runtime does not expose program to task over RSC, so the mirror reads the project's ESM
configuration (`/opt/plcnext/projects/current/Plc/Esm/*.esm.config`): `CyclicTask` gives
each task's cycle in ns, `TaskProgramRelation` each program's task, and a program's variable
is named `<program>.<var>`. A global, or a program in no cyclic task, is charged at the
fastest cycle, where the GDS attaches globals when a rate is given.

Both subscriptions pass an explicit rate: with rate 0 the GDS samples globals from its
housekeeping task, a silent 20 Hz cap.

### Cost and the budget

The unit of work is pulling one RSC value off the stream: a variant, then bytes, then a
compare against the last published value. A scalar or string is one value, an array one per
element, a struct one per member, skipped ones included (`SampleWeight`).

Measured on the AXC F 2152 (Cortex A9, `-O2`): about 0.2 to 0.5 ms per read call plus 32 to
35 us per value, and about 85 us for a `set` that reaches the wire. The AXC F 3152 is about 8
times cheaper. A subscription costs `values x samples per second` of decoding whether or not
anything changed, and only a real change reaches the mesh.

The sample budget (`sampleBudget`) caps `values x samples per second` for each subscription
by slowing its rate. Each variable is charged at its own task's interval, so a 100 ms task's
variable costs a hundredth of a 1 ms one, and the budget bites only when the real load needs
it. The rate is rounded up to a multiple of the floor, since the GDS rounds it down to a
multiple of each cycle, and the solve is checked again because that rounding makes the cost
step shaped. With the default 3000 values per second (about 10% of a Cortex A9 core per
subscription at saturation) and a 1 ms task, 3 watched scalars get every cycle, 16 every
6 ms, 200 every 67 ms.

### The ring and slow subscribers

- The fast ring holds two drain periods of floor rate samples plus slack, so a drain a whole
  period late loses nothing (`fastRing` overrides).
- Every variable's repair window is as deep as the ring, since a whole ring of one variable
  can go out in one drain.
- The backpressure wait is one pass: the thread never waits on a slow subscriber, which
  loses intermediate samples (Rant reports the loss) and still converges on the latest value.

## Keeping the two sides agreeing

The mirror's state lives on its thread, so nothing of it locks. A variable's `on_write`
handler runs on the node's thread and only queues the write, which the mirror thread applies
before anything else on its next wake.

PLC to mesh, `PublishSample`:

- A sample a variable declines is still drained (the RSC contract above).
- A forced variable's value belongs to the force poll: the sample is the program's raw
  value, which forcing does not change.
- A sample taken at or before the mirror's last write back is a pre write read that would
  echo the old value for a cycle, so it is dropped.
- A slow sample older than what the fast drain already published is dropped, so the value
  never moves back.
- Only a real change is published. That also swallows a value a remote just wrote.

PLC side forces, `PollForces`: the force service is the only source of a forced value, since
the subscription and `ReadSingle` both report the program's raw value. A scalar is one force
item, so its value is exact. An array or struct spreads over many items and falls back to a
live read: flagged forced, showing the program values.

Mesh to PLC, `OnRemoteWrite`: from `on_write`, not `on_change`, since it fires on every
applied write and so sees a re force to the same value. A force forces every leaf, the end
of a force unforces every leaf, and a plain write is one batched `Write`.

## The explorer's round trip

Rant samples a peer's round trip only from reliable writes to it. A browsing explorer
writes nothing to the PLC but its periodic `@rant/meta` request, whose ACK leaves once the
node built the snapshot. So the number is network plus wake up plus the time to build the
snapshot: from the PC to hc10, 2.2 ms for the `node` section alone, 14.7 ms for everything,
rising with the variable count. `tools/subwatch` takes a section mask to tell them apart.

## Logs

- `log::Info` and `log::Error` go to the node's `@rant/log` topic. Lines from before the
  node opens (the browse) are queued and flushed when it does.
- `log::ArpInfo` and `log::ArpError` go to Arp.log, for start up and teardown milestones, so
  a node that never opens still leaves a trace.
