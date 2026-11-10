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

(* Every global under the default prefix Plc. The PC writes mirrorProbe and reads mirrorEcho. *)
mirror();
mirrorEcho := mirrorProbe + DINT#1;
mirror_count := mirror.Variables;
mirror_error := mirror.ErrorText;
seen_mirror_count(Topic := STRING#'RantTest/MirrorCount', Value := mirror_count);
seen_mirror_error(Topic := STRING#'RantTest/MirrorError', Value := mirror_error);
END_PROGRAM
