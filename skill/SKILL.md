---
name: plcnext
description: Writing PLCnext Engineer programs in Structured Text with the library Rant: function blocks that publish, subscribe, offer and call functions and tasks, share variables, log and raise status on the Rant mesh, Rant's standard types as IEC types, and Rant Math's IEC functions. Use when a PLC program should talk to Rant nodes or use Rant's types or math.
---

# Rant PLCnext

The PLCnext Engineer library `Rant` puts a PLC program on the Rant mesh. Its blocks are
ordinary IEC function blocks, every one returns at once and never waits on the network.
Controllers on firmware 2026.0 and newer (AXC F 2152, AXC F 3152). Full docs:
https://github.com/KosmosisDire/rant-plcnext/tree/main/docs (`blocks.md` first).

- Install it with `rant install plcnext`. It lands in Engineer's user libraries as
  `Rant/Rant.pcwlx`. Add `Rant` to a project from there, or in a plcx workspace reference
  `$(User)\Documents\PLCnext Engineer\Libraries\Rant\Rant.pcwlx` in `pcwex.toml`.
- Build and deploy the project as usual, with `plcx build` and `plcx deploy` in a plcx workspace.

## Rules

- One node per PLC, shared by every block. Put one `Node` block in one program to name it,
  `node(Name := 'plc_cell');`. A second Node block naming it otherwise sets `Error`.
- A block's `Name` or `Topic` is fixed once the node opens. Use one block per topic, never one
  block switched between topics.
- Value pins (`Value`, `Request`, `Response`, `Progress`) take a variable, never a literal.
- Every block has `Error : BOOL` and `ErrorText : STRING`. They clear when the cause does.
- Names: nodes snake_case (`plc_cell`), topics and other names PascalCase segments
  (`Cell/CupFull`), at most 64 bytes.
- A remote call or write made while the node is down sets `Error` and is never sent late.
- A RemoteVariable takes the owner's value first: write it only once `Received` was set.

## Blocks

| block | does | inputs | outputs |
|---|---|---|---|
| `Node` | names the node | `Name`, `Domain : INT` | |
| `Publisher` | sends `Value` when it changes, or every cycle `Send` is set | `Topic`, `Value`, `Send` | `Count` |
| `Subscriber` | writes the newest value into `Value` | `Topic`, `Value` | `Received`, `Count` |
| `FunctionDefinition` | offers a function, answers on the next call or while `Hold` stays set | `Name`, `Request`, `Response`, `Hold` | `Called`, `Count` |
| `RemoteFunction` | calls on a rising `Execute` | `Name`, `Execute`, `Request`, `Response` | `Busy`, `Done` |
| `VariableDefinition` | owns a variable: program changes go out, mesh writes land | `Name`, `Value` | `Received`, `Count` |
| `RemoteVariable` | another node's variable | `Name`, `Value` | `Received`, `Count` |
| `TaskDefinition` | offers a task, one call at a time | `Name`, `Request`, `Progress`, `Response`, `Complete`, `Fail`, `Reason` | `Started`, `Running`, `Cancelled`, `Count` |
| `RemoteTask` | runs a task on a rising `Execute` | `Name`, `Execute`, `Cancel`, `Request`, `Progress`, `Response` | `Busy`, `Done` |
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
- Scalars are their bare types, `STRING` is a string, `TIME` is milliseconds as `i32`, an enum
  is its number.
- An array of scalars is a list. A 2D array in your code is an array of arrays:
  `ARRAY[0..1] OF ARRAY[0..2] OF INT`, index it `grid[1][2]`. Arrays of structs or strings do not
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
