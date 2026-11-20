---
name: plcnext
description: Writing PLCnext Engineer programs in Structured Text with the library Rant: function blocks that publish, subscribe, offer and call functions and tasks, share variables, log and raise status on the Rant mesh, messages built by field name with Msg, Rant's standard types as IEC types, and Rant Math's IEC functions. Use when a PLC program should talk to Rant nodes or use Rant's types or math.
---

# Rant PLCnext

The PLCnext Engineer library `Rant` puts a PLC program on the Rant mesh. Its blocks are
ordinary IEC function blocks, every one returns at once and never waits on the network.
Controllers on firmware 2026.0 and newer (AXC F 2152, AXC F 3152). Full docs:
https://github.com/KosmosisDire/rant-plcnext/tree/main/docs (`blocks.md` first).

- `rant add plcnext` in a plcx workspace puts it in `libs/` and references it in `pcwex.toml`.
  `rant install plcnext` puts it in Engineer's user libraries for a plain `.pcwex`: add `Rant`
  to the project from there. `rant update` moves either to the latest release.
- Build and deploy the project as usual, with `plcx build` and `plcx deploy` in a plcx workspace.

## Rules

- One node per PLC, shared by every block. Put one `Node` block in one program to name it,
  `node(Name := 'plc_cell');`. A second Node block naming it otherwise sets `Error`.
- A block's `Name` or `Topic` is fixed once the node opens. Use one block per topic, never one
  block switched between topics.
- A value pin the block writes into is in out and takes a variable: the `Value` of a Subscriber,
  VariableDefinition and VariableReader, a definition's `Request`, a remote call's `Progress` and
  `Response`. A pin the block only reads is an input and takes a literal too: the `Value` of a
  Publisher and VariableWriter, a remote call's `Request`, a definition's `Progress` and `Response`.
- Every block has `Error : BOOL` and `ErrorText : STRING`. They clear when the cause does.
- Names: nodes snake_case (`plc_cell`), topics and other names PascalCase segments
  (`Cell/CupFull`), at most 64 bytes.
- A remote call or write made while the node is down sets `Error` and is never sent late.
- Another node's variable takes two blocks: a VariableReader to read it, a VariableWriter to
  write it. A writer's first value is not written, so it never overwrites the owner at start.
- A Subscriber's `Topic`, or a VariableReader's or VariableWriter's `Name`, may end in a field
  path, so `Value` is just that field: `'Gp12/RobotState.servo_power'` into a BOOL. A writer then
  writes only that field. No other block takes a path.

## Blocks

| block | does | inputs | outputs |
|---|---|---|---|
| `Node` | names the node | `Name`, `Domain : INT` | |
| `Publisher` | sends `Value` or `Message` when it changes, or every cycle `Send` is set | `Topic`, `Value`, `Message`, `Send` | `Count` |
| `Subscriber` | writes the newest value into `Value` | `Topic`, `Value` | `Received`, `Count` |
| `FunctionDefinition` | offers a function, answers on the next call or while `Hold` stays set | `Name`, `Request`, `Response`, `Hold` | `Called`, `Count` |
| `RemoteFunction` | calls on a rising `Execute` | `Name`, `Execute`, `Request` or `Message`, `Response` | `Busy`, `Done` |
| `VariableDefinition` | owns a variable: program changes go out, mesh writes land | `Name`, `Value` | `Received`, `Count` |
| `VariableReader` | writes another node's variable into `Value` | `Name`, `Value` | `Received`, `Count` |
| `VariableWriter` | writes `Value` or `Message` to another node's variable when it changes, or every cycle `Send` is set | `Name`, `Value`, `Message`, `Send` | `Count` |
| `TaskDefinition` | offers a task, one call at a time | `Name`, `Request`, `Progress`, `Response`, `Complete`, `Fail`, `Reason` | `Started`, `Running`, `Cancelled`, `Count` |
| `RemoteTask` | runs a task on a rising `Execute` | `Name`, `Execute`, `Cancel`, `Request` or `Message`, `Progress`, `Response` | `Busy`, `Done` |
| `LogLine` | one log line on a rising `Execute` | `Execute`, `Level`, `Text` | `Count` |
| `Status` | raises the condition `Key` while `Active` | `Key`, `Active`, `Level`, `Text`, `Clearable` | `Cleared` |
| `Mirror` | publishes every PLC variable under `Prefix` | `Prefix`, `Locals`, `Forcing`, `StringCap` | `Variables` |

`Done` or `Error` of a remote call holds until `Execute` falls. A FunctionDefinition holds one
waiting call beside the one in hand, and a further caller is told it is busy.

```
PROGRAM Cell
VAR_EXTERNAL
    cup_full : BOOL;
    drive_tripped : BOOL;
END_VAR
VAR
    node : Node;
    cup : Publisher;
    target : Pose;
    goal : Subscriber;
    chirp : FunctionDefinition;
    nothing : Empty;
    trip_log : LogLine;
    drive_fault : Status;
END_VAR
node(Name := 'plc_cell');
cup(Topic := 'Cell/CupFull', Value := cup_full);
goal(Topic := 'Cell/Target', Value := target);
chirp(Name := 'Cell/Chirp', Request := nothing, Response := nothing);
trip_log(Execute := drive_tripped, Level := Level#Warn, Text := 'drive 2 tripped');
drive_fault(Key := 'drive2', Active := drive_tripped, Level := Level#Error,
            Text := 'drive 2 tripped', Clearable := TRUE);
END_PROGRAM
```

Calling another node and serving a task:

```
fetch(Name := 'Vision/Locate', Execute := start, Request := region, Response := found_pose);
IF fetch.Done THEN
    target := found_pose;
END_IF;

move(Name := 'Cell/Move', Request := goal_pose, Progress := at, Response := at,
     Complete := in_place OR move.Cancelled);
```

## Messages without declared types

`Msg` builds a message from field names and values, so a program sends a type it never
declares. Wire its result into `Message` in the same cycle.

```
cmd(Topic := 'Conveyor/Cmd', Message := Msg('', 'speed', speed, 'enabled', run, 'mode', 'Auto'));
call(Name := 'Gp12/SetServo', Execute := go, Message := Msg('', 'on', TRUE), Response := rsp);
```

- The first input is the type name, `''` for the target's own: the owner's type of a function,
  task or variable, or the one type a topic's readers have. A VariableWriter writes only the
  fields the message names. A topic with several types needs the name. A
  standard type such as `'Pose'` works by name anywhere.
- Then a field name and its value, in turn. A path reaches inside: `'position.x'`, `'pts[1]'`.
  Fields left out keep their defaults.
- A number goes into a wider field, an integer only into one it fits, never a REAL into an
  integer. An enum field takes its option's name or number.

## Types

- The library holds Rant's standard types as IEC structs with their Rant field names:
  `Double2/3/4`, `Float2/3/4`, `Int2/3/4`, `Quaternion`, `Pose`, `Pose2D`, `Transform`, `Twist`,
  `Wrench`, `Accel` and their 2D forms, `Matrix3x3`, `Matrix4x4`, `Covariance3/6`, `Color`,
  `GeoPoint`, `Odometry`, `Imu`, `GnssFix`, shapes such as `Sphere`, `Ray`, `Segment`, `Plane`,
  `AlignedBox`, `OrientedBox`, `Circle`, and `Empty` for no value. Fields read as usual:
  `target.position.x`, `target.orientation.w`.
- A standard struct on a pin goes to the mesh as that standard type, so any Rant node reads it.
- Your own struct goes as a Rant struct named by its IEC type, fields snake_case. Structs nest,
  and a STRING inside one keeps its declared capacity.
- Scalars are their bare types, `STRING` is a string, `TIME` is milliseconds as `i32`.
- An enum is a Rant enum named by its IEC type, backed by its base type, INT unless it names one:
  `Job : USINT (None := 0, Testgolf := 1);` is `Job = enum<u8> { None=0, Testgolf=1 }`. Rant
  matches an enum by its name and width only, so to talk to a node's enum, give yours the same
  name and base type. The options may be the ones the PLC uses.
- An array of scalars is a list. A 2D array in your code is an array of arrays:
  `ARRAY[0..1] OF ARRAY[0..2] OF INT`, index it `grid[1][2]`. Arrays of structs, strings or enums do not
  reach the mesh yet.
- `Level` is `Level#Debug`, `Level#Info`, `Level#Warn` or `Level#Error`.

## Math

Rant Math's calls on fixed size values are IEC functions, named as in Rant Math's C#:

```
sum := Double3Add(a, b);
q := QuaternionFromEuler(LREAL#0.1, LREAL#0.2, LREAL#0.3, EulerOrder#MovingZyx);
pose_c := PoseCompose(pose_a, pose_b);
inverse := PoseInverse(pose_c);
```

- A call that can fail, may find nothing, takes no input or gives several answers is a function
  block with `Ok`, or `Found` where it may find nothing:

```
cast(S := sphere, Ray := ray);
IF cast.Found THEN
    hit := cast.Hit;
END_IF;
```

- Engineer calls a function by position only, `PoseInverse(p)`, never `PoseInverse(P := p)`.
- A struct one function returns cannot go straight into another: put it in a variable first.
- Rant Math's own types come along: `EulerOrder`, `AxesPreset`, `Colormap`, `Axes`, `RayHit`,
  `SurfacePoint` and their 2D forms. They are no standard types, so on a pin they go by their
  IEC names like your own.
- Bodies are x forward, y left, z up. Meters, radians, seconds.

## Mirror

`mirror : Mirror;` and `mirror();` once in a program publishes every global and program variable
as a variable of the node, read and write: a global `IO_xLamp` is `Plc/IO/xLamp`, a program
variable `MainInstance.motor_speed` is `Plc/MainInstance/motor/speed`. Use it to see and set
the whole PLC from the mesh without a block per variable.
