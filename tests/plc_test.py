"""The PC side of the PLC library test: every block against the RantTest program on the PLC."""
import sys
import threading
import time

import rant
from rant.types import Double3, Pose, Quaternion

seen = {}
lock = threading.Lock()


def keep(name):
    def on_value(v):
        with lock:
            seen.setdefault(name, []).append(v)
    return on_value


node = rant.Node("plc_test_pc", domain=0)
for name, schema in [("RantTest/Cycles", "i32"), ("RantTest/Pose", Pose), ("RantTest/Text", "string"),
                     ("RantTest/PoseSeen", Pose), ("RantTest/TextSeen", "string"),
                     ("RantTest/RemoteResult", Double3), ("RantTest/PcModeSeen", "i32"),
                     ("RantTest/WaitResult", Double3), ("RantTest/WaitProgress", Double3),
                     ("RantTest/SlowError", "string"), ("RantTest/MirrorCount", "i32"),
                     ("RantTest/MirrorError", "string"), ("RantTest/Peers", "i32"),
                     ("RantTest/BadError", "bool"), ("RantTest/BadText", "string"),
                     ("RantTest/FailsText", "string"), ("RantTest/SleepText", "string"),
                     ("RantTest/SleepBusy", "bool")]:
    node.subscriber(name, schema, keep(name), reliable=True, catch_up=1)

# The schema the PLC's Sample block walks to: named by the IEC type, fields as the IEC names, a
# STRING[15] is string<15>. The enum and TIME widths are what the walk reported on the PLC.
SAMPLE = """Inner
{
    a: i16,
    b: f32,
}
Sample
{
    id: i32,
    label: string<15>,
    inner: Inner,
    grid: i16[2][3],
    phase: i16,
    span: i32,
}"""


def fails(r):
    raise Exception("the PC said no")


def sleeps(r):
    time.sleep(4)
    return Double3(x=r.x)


node.function_definition("RantTest/Fails", fails, Double3, Double3)
node.function_definition("RantTest/Sleep", sleeps, Double3, Double3)
doubled = []
node.function_definition("RantTest/Double", lambda r: (doubled.append(r.x), Double3(x=r.x * 2, y=r.y, z=r.z))[1],
                         Double3, Double3)
waited = []


def wait_task(req, ctx):
    waited.append(req.x)
    for i in range(1, 4):
        ctx.progress(Double3(x=float(i)))
        time.sleep(0.3)
    return Double3(x=req.x * 3)


node.task_definition("RantTest/Wait", wait_task, Double3, Double3, Double3)
slow_cancelled = []


def slow_task(req, ctx):
    end = time.time() + 10
    while time.time() < end:
        if ctx.cancelled:
            slow_cancelled.append(True)
            raise rant.CancelledError("stopped by the PLC")
        time.sleep(0.05)
    return Double3()


node.task_definition("RantTest/Slow", slow_task, Double3, Double3, Double3)
pose_in = node.publisher("RantTest/PoseIn", Pose, reliable=True, keep_last=1)
text_in = node.publisher("RantTest/TextIn", "string", reliable=True, keep_last=1)
echo = node.remote_function("RantTest/Echo", Double3, Double3)
held = node.remote_function("RantTest/Held", Double3, Double3)
sample_echo = node.remote_function("RantTest/SampleEcho", SAMPLE, SAMPLE)
release = node.remote_variable("RantTest/Release", "bool")
rename = node.remote_variable("RantTest/Rename", "bool")
run_sleep = node.remote_variable("RantTest/RunSleep", "bool")
node.subscriber("RantTest/Sample", SAMPLE, keep("RantTest/Sample"), reliable=True, catch_up=1)
setpoint = node.remote_variable("RantTest/Setpoint", "f64")
pc_mode = node.variable_definition("RantTest/PcMode", "i32", initial=3)
count = node.remote_task("RantTest/Count", Double3, Double3, Double3)
probe = node.remote_variable("Plc/mirrorProbe", "i32")
echo_var = node.remote_variable("Plc/mirrorEcho", "i32")

time.sleep(4)
ok = True


def check(what, cond, detail):
    global ok
    print(("PASS " if cond else "FAIL ") + what + ": " + str(detail), flush=True)
    ok = ok and cond


pose_in.send(Pose(position=Double3(x=1.5, y=-2.0, z=3.25), orientation=Quaternion(x=0, y=0, z=0.6, w=0.8)))
text_in.send("hello from the PC")
reply = echo.call(Double3(x=1.0, y=2.0, z=3.0), timeout=5.0).value

setpoint.wait(5)
check("VariableDefinition, the PLC's value", setpoint.get() == 1.5, setpoint.get())
setpoint.set(7.25)
time.sleep(1)
check("VariableDefinition, a write from the mesh holds", setpoint.get() == 7.25, setpoint.get())
setpoint.set(100.0)
time.sleep(1)
check("VariableDefinition, the PLC's answer to a write", setpoint.get() == 101.0, setpoint.get())

with lock:
    modes = list(seen.get("RantTest/PcModeSeen", []))
check("RemoteVariable, the owner's value", 3 in modes, modes)
pc_mode.set(5)
time.sleep(1.5)
with lock:
    modes = list(seen.get("RantTest/PcModeSeen", []))
check("RemoteVariable, a write from the PLC", pc_mode.get() == 6 and modes[-1:] == [6], f"{pc_mode.get()}, seen {modes}")

progress = []
r = count.call(Double3(x=20.0), on_progress=lambda v: progress.append(v.x if v else None), timeout=10.0)
xs = [x for x in progress if x is not None]
check("TaskDefinition, completes with progress", r.status == rant.CallStatus.OK and r.value.x == 20.0 and len(xs) >= 3
      and xs == sorted(xs), f"{r.status} {r.value} progress {xs}")

long_result = []
long_id = count.call_async(Double3(x=1000.0), long_result.append)
time.sleep(0.5)
busy = count.call(Double3(x=5.0), timeout=5.0)
check("TaskDefinition, busy while one runs", busy.status == rant.CallStatus.APP_ERROR and "busy" in busy.message,
      f"{busy.status} {busy.message}")
count.cancel(long_id)
time.sleep(1.5)
check("TaskDefinition, a cancel ends the call", bool(long_result) and long_result[0].status == rant.CallStatus.CANCELLED,
      f"{long_result[0].status if long_result else None}")
neg = count.call(Double3(x=-1.0), timeout=5.0)
check("TaskDefinition, Fail with a reason", neg.status == rant.CallStatus.APP_ERROR and neg.message == "negative target",
      f"{neg.status} {neg.message}")

probe.wait(5)
echo_var.wait(5)
check("Mirror, a global's value", probe.get() is not None and echo_var.get() == probe.get() + 1,
      f"probe {probe.get()}, echo {echo_var.get()}")
probe.set(41)
time.sleep(1.5)
check("Mirror, a write from the mesh reaches the PLC", echo_var.get() == 42, echo_var.get())

time.sleep(8)
with lock:
    cycles = seen.get("RantTest/Cycles", [])
    check("Cycles, a DINT on change", len(cycles) > 50 and cycles[-1] > cycles[0], f"{len(cycles)} values, last {cycles[-1] if cycles else None}")
    poses = seen.get("RantTest/Pose", [])
    check("Pose, a standard struct", bool(poses) and poses[-1].orientation.w == 1.0 and poses[-1].position.x > 0,
          poses[-1] if poses else None)
    texts = seen.get("RantTest/Text", [])
    check("Text, a STRING", bool(texts) and texts[-1] == "hello from the PLC", texts[-1] if texts else None)
    back = seen.get("RantTest/PoseSeen", [])
    check("Subscriber of a Pose, sent back", bool(back) and back[-1].position.z == 3.25 and back[-1].orientation.z == 0.6,
          back[-1] if back else None)
    tback = seen.get("RantTest/TextSeen", [])
    check("Subscriber of a STRING, sent back", bool(tback) and tback[-1] == "hello from the PC", tback[-1] if tback else None)
    check("FunctionDefinition answered by the PLC", reply is not None and reply.x == 2.0 and reply.z == 6.0, reply)
    remote = seen.get("RantTest/RemoteResult", [])
    check("RemoteFunction from the PLC", bool(doubled) and bool(remote) and remote[-1].x == 2 * doubled[-1],
          f"{len(doubled)} calls, last result {remote[-1] if remote else None}")
    results = seen.get("RantTest/WaitResult", [])
    check("RemoteTask from the PLC", bool(waited) and any(r.x == 3 * w for r in results for w in waited),
          f"calls {waited}, results {[r.x for r in results]}")
    wprog = [p.x for p in seen.get("RantTest/WaitProgress", [])]
    check("RemoteTask progress", {1.0, 2.0, 3.0} <= set(wprog), wprog)
    slow = seen.get("RantTest/SlowError", [])
    check("RemoteTask Cancel", bool(slow_cancelled) and any("stopped by the PLC" in s for s in slow),
          f"{len(slow_cancelled)} cancelled, texts {slow}")
    mcount = seen.get("RantTest/MirrorCount", [])
    check("Mirror, Variables counts the globals", bool(mcount) and mcount[-1] >= 30, mcount[-1:] if mcount else None)
    merr = seen.get("RantTest/MirrorError", [])
    check("Mirror, no error", not merr or merr[-1] == "", merr[-1:] if merr else None)
# Node: Name and Peers, the PC is a peer of the PLC's node.
with lock:
    peers = list(seen.get("RantTest/Peers", []))
check("Node, Peers counts the PC", bool(peers) and peers[-1] >= 1, peers[-3:])

# Error and ErrorText: an empty topic, and a remote function the PC fails.
with lock:
    bad = list(seen.get("RantTest/BadError", []))
    bad_text = list(seen.get("RantTest/BadText", []))
    fails_text = list(seen.get("RantTest/FailsText", []))
check("Publisher with no topic sets Error and ErrorText", bool(bad) and bad[-1] is True and bool(bad_text[-1:]) and bad_text[-1] != "",
      f"{bad[-1:]} {bad_text[-1:]}")
check("RemoteFunction failure reaches ErrorText", any("the PC said no" in t for t in fails_text), fails_text[-3:])

# Types: a user struct with a nested struct, a string, a 2D array, an enum and a TIME.
with lock:
    samples = list(seen.get("RantTest/Sample", []))
last = samples[-1] if samples else None
check("User struct, nested struct and string", last is not None and last.id == 7 and last.label == "abc"
      and last.inner.a == -3 and last.inner.b == 1.5, last)
check("User struct, 2D array, enum and TIME", last is not None and last.grid[1][2] == 9 and last.phase == 1
      and last.span == 1500, last)
back = sample_echo.call(last, timeout=5.0) if last is not None else None
check("User struct as a Request and Response", back is not None and back.value.id == 8 and back.value.label == "abc"
      and back.value.grid[1][2] == 9, back.value if back is not None else None)

# Hold: the call stays open until Release.
held_result = []
held.call_async(Double3(x=1.0), held_result.append)
time.sleep(1.5)
check("FunctionDefinition, Hold keeps the call open", not held_result, held_result)
release.set(True)
time.sleep(1.5)
check("FunctionDefinition, Release answers it", bool(held_result) and held_result[0].value.x == 2.0,
      held_result[0].value if held_result else None)

# A reopen with calls in flight: a call the PLC serves, and a call the PLC makes.
release.set(False)
time.sleep(0.5)
held_cut = []
held.call_async(Double3(x=5.0), held_cut.append)
run_sleep.set(True)
time.sleep(1.0)
rename.set(True)
time.sleep(5)
with lock:
    sleep_text = list(seen.get("RantTest/SleepText", []))
    sleep_busy = list(seen.get("RantTest/SleepBusy", []))
check("Reopen ends a call the PLC serves", bool(held_cut) and held_cut[0].status != rant.CallStatus.OK,
      held_cut[0].message if held_cut else "still open")
check("Reopen fails a call the PLC makes", any("node reopened" in t for t in sleep_text) and sleep_busy[-1:] == [False],
      f"{sleep_text[-3:]} busy {sleep_busy[-3:]}")
run_sleep.set(False)
const_late = []
node.subscriber("RantTest/Const", "i32", const_late.append, reliable=True, catch_up=1)
time.sleep(2)
check("Publisher resends its constant after a reopen", 4242 in const_late, const_late[-3:])
rename.set(False)
node.close()
sys.exit(0 if ok else 1)
