# subwatch

Subscribes to N (or all) variables of one Rant node, the way an explorer browsing everything
does, and prints the round trip, the update rate and the node's log lines once a second.

```
subwatch <peer-name> <count|all> <seconds> [multicast-if] [peer-address-prefix] [meta-sections]
subwatch plc all 60
subwatch plc 0 20 "" "" 1      # the round trip with a node only meta request
```

- `peer-address-prefix` picks one of two nodes that share a name.
- `meta-sections` is the `@rant/meta` mask asked each second, `0` = everything, as the
  explorer does, `1` = the `node` section alone. The difference is the time the peer spends
  building the snapshot.

## Build

Against the Rant the library builds with, `../rant` by default. With mingw:

```
D=../../../rant/dist
g++ -O2 -std=c++20 -I$D -c $D/rant.cpp -o rant.o
g++ -O2 -std=c++20 -I$D subwatch.cpp rant.o -lws2_32 -lbcrypt -lwinmm -o subwatch.exe
```

For an AXC F 3152, so the measurement runs on the lab LAN:

```
SDK=C:/plcnext/sdk/3152/2026.0
$SDK/sysroots/x86_64-w64-mingw32/usr/bin/x86_64-pxc-linux/x86_64-pxc-linux-g++.exe \
  --sysroot=$SDK/sysroots/corei7-64-pxc-linux -O2 -std=c++20 -pthread \
  -I$D subwatch.cpp $D/rant.cpp -lrt -o subwatch_x86
plcx upload subwatch_x86 /tmp/subwatch      # then: plcx exec "/tmp/subwatch plc all 30"
```
