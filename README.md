# Rant PLCnext

The PLCnext library `Rant`: IEC blocks that put a PLC program on the Rant mesh, with Rant's
standard types as IEC types.

```
PROGRAM Mesh
VAR_EXTERNAL
    cup_full : BOOL;
END_VAR
VAR
    node : Node;
    cup : Publisher;
END_VAR
node(Name := 'plc_hc10');
cup(Topic := 'Golf/CupFull', Value := cup_full);
END_PROGRAM
```

- Blocks for topics, functions, variables and tasks, a Mirror block, and Rant Math's functions.
- `rant add plcnext` adds it to an Engineer project. Build it with `plcx build`.
- docs/blocks.md lists the blocks.
