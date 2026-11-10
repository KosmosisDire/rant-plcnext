# Blocks

Every block shares one node per PLC, and a block call never waits on the network.

- Put one `Node` block in a program to name the node. Without it the node is `plc_` and the host
  name.
- An empty `Name` or `Topic` sets `Error` with a text.
- `Error` and `ErrorText` clear when the cause does. A call that fails sets them until the next
  call.
- Value pins (`Value`, `Request`, `Response`) take a variable, not a literal.
- A name is at most 64 bytes.
- When the node opens again, a call in hand ends with the text `the node reopened`.
- A call or a variable write made while the node is down sets `Error` and is never sent late. One
  made before the node first opens is sent once it does.

## Node

Names the shared node, in domain 0 unless `Domain` says otherwise. A change of either opens it again.
The first Node block to run names it. Another one naming it otherwise sets `Error`.

- `Name : STRING`, `Domain : INT`
- `Error : BOOL`, `ErrorText : STRING`

## Publisher

Publishes `Value` on `Topic` whenever it changes, and every cycle `Send` is set. A subscriber
that joins later gets the last value, also of a value that never changes.

- `Topic : STRING`, `Value : ANY` (in out), `Send : BOOL`
- `Count : DINT` sent, `Error : BOOL`, `ErrorText : STRING`

## Subscriber

Writes the newest value of `Topic` into `Value`.

- `Topic : STRING`, `Value : ANY` (in out)
- `Received : BOOL` for the cycle a value arrives, `Count : DINT`, `Error`, `ErrorText`

## FunctionDefinition

Offers the function `Name`. `Called` is set for the cycle a call's `Request` arrives. The next
call of the block answers with `Response`, or a later one while `Hold` is set. One more call
waits beside the one in hand, and a caller past that is told the function is busy.

- `Name : STRING`, `Request : ANY`, `Response : ANY` (both in out), `Hold : BOOL`
- `Called : BOOL`, `Count : DINT` answered, `Error`, `ErrorText`

```
chirp(Name := 'Golf/Chirp', Request := nothing, Response := nothing);
pulse(IN := chirp.Called, PT := T#300ms);
```

## RemoteFunction

Calls the function `Name` with `Request` on a rising `Execute`. `Done` or `Error` holds the
outcome, with `Response` written, until `Execute` falls.

- `Name : STRING`, `Execute : BOOL`, `Request : ANY`, `Response : ANY` (both in out)
- `Busy`, `Done`, `Error : BOOL`, `ErrorText : STRING`

## VariableDefinition

Owns the variable `Name`. A change the program makes to `Value` goes to the mesh, and a write
from the mesh lands in `Value`.

- `Name : STRING`, `Value : ANY` (in out)
- `Received : BOOL` for the cycle a write lands, `Count : DINT`, `Error`, `ErrorText`

## RemoteVariable

The variable `Name` another node owns. Its value lands in `Value`, and a change the program
makes to `Value` is written to the owner. A write while no owner is found sets `Error` and is not
sent later, so write once `Received` was set.

- `Name : STRING`, `Value : ANY` (in out)
- `Received : BOOL` for the cycle a value lands, `Count : DINT`, `Error`, `ErrorText`

## TaskDefinition

Offers the task `Name`, one call at a time. `Started` is set for the cycle a call's `Request`
arrives and `Running` until a later cycle ends it: `Complete` answers with `Response`, `Fail`
fails it with `Reason`. `Progress` goes to the caller whenever it changes. `Cancelled` says the
caller wants it to stop, and `Complete` then answers it as cancelled.

- `Name : STRING`, `Request`, `Progress`, `Response : ANY` (in out)
- `Complete`, `Fail : BOOL`, `Reason : STRING`
- `Started`, `Running`, `Cancelled : BOOL`, `Count : DINT` ended, `Error`, `ErrorText`

```
move(Name := 'Cell/Move', Request := target, Progress := at, Response := at,
     Complete := in_place OR move.Cancelled);
```

## RemoteTask

Runs the task `Name` with `Request` on a rising `Execute`. `Progress` is written as it comes
while `Busy`, and `Cancel` asks the provider to stop. `Done` or `Error` holds the outcome, with
`Response` written, until `Execute` falls.

- `Name : STRING`, `Execute`, `Cancel : BOOL`, `Request`, `Progress`, `Response : ANY` (in out)
- `Busy`, `Done`, `Error : BOOL`, `ErrorText : STRING`

## LogLine

Sends `Text` as a log line of the node at `Level`, on each rising `Execute`.

- `Execute : BOOL`, `Level : Level` (`Level#Info` by default), `Text : STRING`
- `Count : DINT` sent, `Error`, `ErrorText`

## Status

Raises the node's status condition `Key` while `Active`, at `Level` with `Text`, and clears it
after. A `Clearable` one may be cleared from another node, such as an HMI: it stays down until
`Active` goes FALSE, and `Cleared` is set for the cycle the clear arrives.

```
drive_fault(Key := 'drive2', Active := drive_tripped, Level := Level#Error,
            Text := 'drive 2 tripped', Clearable := TRUE);
```

- `Key : STRING`, `Active : BOOL`, `Level : Level` (`Level#Warn` by default), `Text : STRING`, `Clearable : BOOL`
- `Cleared : BOOL`, `Error`, `ErrorText`

`Level` is `Debug`, `Info`, `Warn` or `Error`.

## Mirror

Publishes every PLC variable as a variable of the node under `Prefix`, read write, and
forceable where the PLC allows it. It runs from its first call, and a change of an input
starts it over. docs/design.md lists which variables it publishes.

- `Prefix : STRING` (`Plc`), empty for the mesh root
- `Locals : BOOL` publishes a program's locals too
- `Forcing : BOOL` (`TRUE`) lets the mesh force variables
- `StringCap : INT` (`80`) bytes of a string inside a struct
- `Variables : DINT` published, `Error`, `ErrorText`

```
mirror();
```

## Values

- A struct of the library, `Pose` or `Twist`, is that standard type. Rant Math's own types are
  not standard: they go by their IEC names like yours.
- `Empty` is Rant's empty type. IEC has no empty struct, so it holds one BOOL nobody sends.
- Any other struct is a Rant struct named by its IEC type, its fields renamed snake_case. A
  struct inside a struct is a struct, and a STRING inside one keeps its declared capacity.
- A scalar is its bare type, a STRING is `string`, an array of scalars `elem[N]`, one size per
  dimension: `ARRAY[0..1] OF ARRAY[0..2] OF INT` is `i16[3][2]`, two arrays of three. `Matrix3x3` and the library's other
  arrays are their standard types.
- An array of structs or of strings does not reach the mesh yet: the block reports it. So does
  an array whose bounds cannot be read.

## Math

Rant Math's calls on fixed size values, as IEC functions named as in Rant Math's C#: vectors,
rotations, poses, matrices, lines, solids, motion, axes, color, camera and geo.

```
sum := Double3Add(a, b);
q := QuaternionFromEuler(LREAL#0.1, LREAL#0.2, LREAL#0.3, EulerOrder#MovingZyx);
pose_c := PoseCompose(pose_a, pose_b);
```

- A call that can fail, may find nothing, takes no input or gives several answers is a function
  block, with `Ok`, or `Found` where it may find nothing:

```
cast(S := sphere, Ray := ray);
IF cast.Found THEN
    hit := cast.Hit;
END_IF;
```

- Engineer calls a function by position only, and cannot pass a struct one function returns
  straight into another: put it in a variable first.
- Rant Math's own types come along: `EulerOrder`, `AxesPreset`, `Colormap`, `Axes`, `RayHit`,
  `SurfacePoint` and their 2D forms.
