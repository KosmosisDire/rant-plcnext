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

`plcx build` makes the library for every controller `plcnext.proj` declares, and
`rant install plcnext --from .` puts it in PLCnext Engineer's user libraries. docs/blocks.md
lists the blocks, the Mirror block among them, which publishes every PLC variable, and Rant
Math's functions on the standard types.
