PROGRAM RantTest
VAR_EXTERNAL
    mirrorProbe : DINT;
    mirrorEcho : DINT;
END_VAR
VAR
    cycles : DINT;
    pose_out : Pose;
    text_out : STRING;
    pose_in : Pose;
    text_in : STRING;
    echo_request : Double3;
    echo_response : Double3;
    remote_request : Double3;
    remote_response : Double3;
    remote_go : BOOL;
    pub_cycles : Publisher;
    pub_pose : Publisher;
    pub_text : Publisher;
    sub_pose : Subscriber;
    sub_text : Subscriber;
    seen_pose : Publisher;
    seen_text : Publisher;
    echo : FunctionDefinition;
    remote : RemoteFunction;
    seen_remote : Publisher;
    setpoint : LREAL := LREAL#1.5;
    setpoint_def : VariableDefinition;
    pc_mode : DINT;
    pc_mode_ref : RemoteVariable;
    seen_mode : Publisher;
    count_request : Double3;
    count_progress : Double3;
    count_response : Double3;
    count_task : TaskDefinition;
    wait_request : Double3;
    wait_progress : Double3;
    wait_response : Double3;
    wait_go : BOOL;
    wait_task : RemoteTask;
    seen_wait : Publisher;
    seen_wait_progress : Publisher;
    slow_request : Double3;
    slow_progress : Double3;
    slow_response : Double3;
    slow_go : BOOL;
    slow_ticks : DINT;
    slow_task : RemoteTask;
    seen_slow : Publisher;
    slow_error : STRING;
    node : Node;
    name_sel : STRING;
    rename : BOOL;
    rename_def : VariableDefinition;
    node_peers : DINT;
    seen_peers : Publisher;
    other_node : Node;
    other_text : STRING;
    seen_other : Publisher;
    const_value : DINT := DINT#4242;
    const_pub : Publisher;
    release : BOOL;
    release_def : VariableDefinition;
    held : FunctionDefinition;
    held_request : Double3;
    held_response : Double3;
    bad_pub : Publisher;
    bad_error : BOOL;
    bad_text : STRING;
    seen_bad : Publisher;
    seen_bad_text : Publisher;
    fails : RemoteFunction;
    fails_go : BOOL;
    fails_request : Double3;
    fails_response : Double3;
    fails_text : STRING;
    seen_fails : Publisher;
    sleeper : RemoteFunction;
    sleep_go : BOOL;
    sleep_def : VariableDefinition;
    sleep_request : Double3;
    sleep_response : Double3;
    sleep_text : STRING;
    sleep_busy : BOOL;
    seen_sleep : Publisher;
    seen_sleep_busy : Publisher;
    sample_out : Sample;
    sample_pub : Publisher;
    sample_req : Sample;
    sample_rsp : Sample;
    sample_fn : FunctionDefinition;
    mirror : Mirror;
    mirror_count : DINT;
    mirror_error : STRING;
    seen_mirror_count : Publisher;
    seen_mirror_error : Publisher;
END_VAR

cycles := cycles + DINT#1;
pose_out.position.x := DINT_TO_LREAL(cycles);
pose_out.orientation.w := LREAL#1.0;
text_out := STRING#'hello from the PLC';
pub_cycles(Topic := STRING#'RantTest/Cycles', Value := cycles);
pub_pose(Topic := STRING#'RantTest/Pose', Value := pose_out);
pub_text(Topic := STRING#'RantTest/Text', Value := text_out);

sub_pose(Topic := STRING#'RantTest/PoseIn', Value := pose_in);
sub_text(Topic := STRING#'RantTest/TextIn', Value := text_in);
seen_pose(Topic := STRING#'RantTest/PoseSeen', Value := pose_in);
seen_text(Topic := STRING#'RantTest/TextSeen', Value := text_in);

echo(Name := STRING#'RantTest/Echo', Request := echo_request, Response := echo_response);
IF echo.Called THEN
    echo_response.x := echo_request.x * LREAL#2.0;
    echo_response.y := echo_request.y * LREAL#2.0;
    echo_response.z := echo_request.z * LREAL#2.0;
END_IF;

remote_go := (cycles MOD DINT#50) < DINT#25;
remote_request.x := DINT_TO_LREAL(cycles);
remote(Name := STRING#'RantTest/Double', Execute := remote_go, Request := remote_request, Response := remote_response);
seen_remote(Topic := STRING#'RantTest/RemoteResult', Value := remote_response);

(* A write from the mesh above 50 makes the PLC add one, which must reach the mesh. *)
setpoint_def(Name := STRING#'RantTest/Setpoint', Value := setpoint);
IF setpoint_def.Received AND setpoint > LREAL#50.0 THEN
    setpoint := setpoint + LREAL#1.0;
END_IF;

(* The PC owns PcMode. When it sets 5 the PLC answers with 6. *)
pc_mode_ref(Name := STRING#'RantTest/PcMode', Value := pc_mode);
IF pc_mode_ref.Received AND pc_mode = DINT#5 THEN
    pc_mode := DINT#6;
END_IF;
seen_mode(Topic := STRING#'RantTest/PcModeSeen', Value := pc_mode);

(* Counts progress up to the request's x, one a cycle, and fails a negative target. *)
count_task(Name := STRING#'RantTest/Count', Request := count_request, Progress := count_progress,
    Response := count_response, Complete := count_progress.x >= count_request.x OR count_task.Cancelled,
    Fail := count_request.x < LREAL#0.0, Reason := STRING#'negative target');
IF count_task.Started THEN
    count_progress.x := LREAL#0.0;
ELSIF count_task.Running THEN
    count_progress.x := count_progress.x + LREAL#1.0;
END_IF;
count_response.x := count_progress.x;

wait_go := (cycles MOD DINT#60) < DINT#30;
wait_request.x := DINT_TO_LREAL(cycles);
wait_task(Name := STRING#'RantTest/Wait', Execute := wait_go, Request := wait_request, Progress := wait_progress,
    Response := wait_response);
seen_wait(Topic := STRING#'RantTest/WaitResult', Value := wait_response);
seen_wait_progress(Topic := STRING#'RantTest/WaitProgress', Value := wait_progress);

(* Slow never ends by itself, so the PLC cancels it after a second. *)
slow_go := (cycles MOD DINT#100) < DINT#50;
IF slow_task.Busy THEN
    slow_ticks := slow_ticks + DINT#1;
ELSE
    slow_ticks := DINT#0;
END_IF;
slow_task(Name := STRING#'RantTest/Slow', Execute := slow_go, Cancel := slow_ticks > DINT#10, Request := slow_request,
    Progress := slow_progress, Response := slow_response);
slow_error := slow_task.ErrorText;
seen_slow(Topic := STRING#'RantTest/SlowError', Value := slow_error);

(* Node: the PC variable Rename moves the node from plc_test to plc_test2, which reopens it. *)
rename_def(Name := STRING#'RantTest/Rename', Value := rename);
IF rename THEN
    name_sel := STRING#'plc_test2';
ELSE
    name_sel := STRING#'plc_test';
END_IF;
node(Name := name_sel, Domain := INT#0);
node_peers := node.Peers;
seen_peers(Topic := STRING#'RantTest/Peers', Value := node_peers);
(* A second Node block naming the node otherwise leaves it alone and sets Error. *)
other_node(Name := STRING#'plc_other', Domain := INT#0);
other_text := other_node.ErrorText;
seen_other(Topic := STRING#'RantTest/OtherNode', Value := other_text);

(* A constant value must reach a subscriber that joins later, also after a reopen. *)
const_pub(Topic := STRING#'RantTest/Const', Value := const_value);

(* Hold keeps a call unanswered until the PC sets Release. *)
release_def(Name := STRING#'RantTest/Release', Value := release);
held(Name := STRING#'RantTest/Held', Request := held_request, Response := held_response, Hold := NOT release);
IF held.Called THEN
    held_response.x := held_request.x + LREAL#1.0;
END_IF;

(* An empty topic sets Error and ErrorText. *)
bad_pub(Topic := STRING#'', Value := cycles);
bad_error := bad_pub.Error;
bad_text := bad_pub.ErrorText;
seen_bad(Topic := STRING#'RantTest/BadError', Value := bad_error);
seen_bad_text(Topic := STRING#'RantTest/BadText', Value := bad_text);

(* A remote function the PC fails: Error and its text hold until Execute falls. *)
fails_go := (cycles MOD DINT#100) < DINT#50;
fails(Name := STRING#'RantTest/Fails', Execute := fails_go, Request := fails_request, Response := fails_response);
fails_text := fails.ErrorText;
seen_fails(Topic := STRING#'RantTest/FailsText', Value := fails_text);

(* A remote function that takes seconds, which a reopen of the node must end with an error. *)
sleep_def(Name := STRING#'RantTest/RunSleep', Value := sleep_go);
sleeper(Name := STRING#'RantTest/Sleep', Execute := sleep_go, Request := sleep_request, Response := sleep_response);
sleep_text := sleeper.ErrorText;
sleep_busy := sleeper.Busy;
seen_sleep(Topic := STRING#'RantTest/SleepText', Value := sleep_text);
seen_sleep_busy(Topic := STRING#'RantTest/SleepBusy', Value := sleep_busy);

(* A user struct with a nested struct, a string, a two dimensional array, an enum and a TIME. *)
sample_out.id := DINT#7;
sample_out.label := STRING#'abc';
sample_out.inner.a := INT#-3;
sample_out.inner.b := REAL#1.5;
sample_out.grid[1][2] := INT#9;
sample_out.phase := Phase#Active;
sample_out.span := T#1500ms;
sample_pub(Topic := STRING#'RantTest/Sample', Value := sample_out);
sample_fn(Name := STRING#'RantTest/SampleEcho', Request := sample_req, Response := sample_rsp);
IF sample_fn.Called THEN
    sample_rsp := sample_req;
    sample_rsp.id := sample_req.id + DINT#1;
END_IF;

(* Every global under the default prefix Plc. The PC writes mirrorProbe and reads mirrorEcho. *)
mirror();
mirrorEcho := mirrorProbe + DINT#1;
mirror_count := mirror.Variables;
mirror_error := mirror.ErrorText;
seen_mirror_count(Topic := STRING#'RantTest/MirrorCount', Value := mirror_count);
seen_mirror_error(Topic := STRING#'RantTest/MirrorError', Value := mirror_error);
END_PROGRAM
