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
- `tools/library_description.py` writes the folders Engineer's sidebar shows: a folder per
  block family, and Rant Math's calls by the folders its `bindings/plcnext/RantMath.json` names.
  The build runs it, so Python must be on PATH.

## Use it in a project

```
rant add plcnext
```

- In a plcx workspace it puts the latest release in `libs/plcnext-<version>/Rant.pcwlx` and
  references it in `pcwex.toml`.
- Beside an Engineer project's `.pcwex` it puts it in Engineer's user libraries as
  `Rant/Rant.pcwlx`. Add `Rant` to the project from there.
- `rant update` moves both to the latest release.

Then declare the blocks like any others (docs/blocks.md), and `plcx build` and `plcx deploy`.
`plcx check` reads the library's types, blocks and functions from it (plcx feeaf81 or newer).

A build of this repo goes in by hand: copy `bin/Rant.pcwlx` into the project's `libs/` and
reference it in `pcwex.toml` as `libs\Rant.pcwlx`. Engineer finds a library only by a file named
after its title, `Rant.pcwlx`.

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
   The test names the node itself, so leave out the project's own Node block.
2. Copy the new `bin/Rant.pcwlx` in, then build and deploy.
3. `python tests/plc_test.py` prints one PASS or FAIL line per check. `python tests/math_test.py`
   checks each math answer against Rant Math's Python binding, which takes its library from
   `RANT_MATH_LIBRARY`.

## Load testing

`tools/subwatch` subscribes to N (or all) variables of one node, the way an explorer browsing
everything does, and prints the round trip and the update rate.
