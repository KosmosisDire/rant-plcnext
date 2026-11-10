# Usage

## Build

```
plcx build
```

- Builds the C# part and the C++ part for every controller `plcnext.proj` declares, and
  packs both into `bin/Rant.pcwlx`.
- Rant comes from `../rant` until a Rant release carries the IEC types.
  `-D CPM_rant_SOURCE:PATH=<tree>` builds against another tree.
- The C# part takes the standard types from `../rant/bindings/plcnext/StandardTypes.cs`.
- Rant Math comes from `../math-rant` (`-D CPM_math_SOURCE:PATH=<tree>`), and the C# part
  takes its functions from `bindings/plcnext/RantMath.cs` there (`MathSource`).
- plcx needs Visual Studio's MSBuild, or `PLCX_MSBUILD` naming it.

## Install

```
rant install plcnext --from .
```

- Puts `bin/Rant.pcwlx` in Engineer's user libraries, as `Rant/Rant.pcwlx` in the folder
  Engineer's settings name (`Documents/PLCnext Engineer/Libraries` by default).
- Run it again after each build. `rant remove plcnext` takes it out.

## Use it in a project

1. Add `Rant` from Engineer's user libraries, or copy `bin/Rant.pcwlx` into the project as
   `libs/Rant.pcwlx`.
2. In a plcx workspace, reference it in `pcwex.toml`, with the path
   `$(User)\Documents\PLCnext Engineer\Libraries\Rant\Rant.pcwlx` or `libs\Rant.pcwlx`.
3. Declare the blocks like any others (docs/blocks.md).
4. `plcx build` and `plcx deploy`. `plcx check` reads the library's types, blocks and
   functions from it (plcx feeaf81 or newer).

## Mirror every PLC variable

```
mirror : Mirror;
mirror();
```

Each published PLC variable is then a variable of this PLC's node, read write, forceable
where the PLC allows it. A global `IO_xLamp` is `Plc/IO/xLamp`, a program variable
`MainInstance.motor_speed` is `Plc/MainInstance/motor/speed`. docs/design.md lists which
variables are published and how their types map.

## Testing

`tests/RantTest.st` exercises every block on a PLC, and `tests/plc_test.py` is the PC side,
through Rant's Python binding. On a copy of an Engineer project:

1. Add `tests/RantTest.st` as `src/RantTest.st`, `tests/RantTestGlobals.st` as
   `src/globals/RantTest.st`, `tests/RantTestTypes.st` as `src/types/RantTest.st`, and `PROGRAM RantTestInstance WITH <task> : RantTest;` to
   `tasks.st`. For the math functions add `tests/RantMathTest.st` and
   `PROGRAM RantMathTestInstance WITH <task> : RantMathTest;` too.
2. Copy the new `bin/Rant.pcwlx` in, then build and deploy.
3. `python tests/plc_test.py` prints one PASS or FAIL line per check. `python tests/math_test.py`
   checks each math answer against Rant Math's Python binding, which takes its library from
   `RANT_MATH_LIBRARY`.

## Load testing

`tools/subwatch` subscribes to N (or all) variables of one node, the way an explorer browsing
everything does, and prints the round trip and the update rate.
