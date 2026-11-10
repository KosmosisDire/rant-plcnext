PROGRAM RantMathTest
VAR
    a : Double3;
    sum : Double3;
    axis_b : Double3;
    scaled : Double3;
    f3 : Float3;
    i3 : Int3;
    to_axis : QuaternionToAxisAngle;
    inverse : Matrix3x3Inverse;
    cast : SphereRaycast;
    project : CameraProject;
    b : Double3;
    v2 : Double2;
    v4 : Double4;
    q : Quaternion;
    axis : Double3;
    angle : LREAL;
    m3 : Matrix3x3;
    m3_ok : BOOL;
    m4 : Matrix4x4;
    pose_a : Pose;
    pose_b : Pose;
    pose_c : Pose;
    p2a : Pose2D;
    p2b : Pose2D;
    p2c : Pose2D;
    unity_axes : Axes;
    unity_point : Double3;
    sphere : Sphere;
    ray : Ray;
    hit : RayHit;
    hit_ok : BOOL;
    box : AlignedBox;
    ca : Color;
    cb : Color;
    cm : Color;
    geo_o : GeoPoint;
    geo_p : GeoPoint;
    enu : Double3;
    tw : Twist;
    tw_moved : Twist;
    ia : Inertia;
    ib : Inertia;
    isum : Inertia;
    cam : CameraIntrinsics;
    pixel : Double2;
    cam_ok : BOOL;
    ob : OrientedBox2D;
    sp : SurfacePoint2D;
    answers : ARRAY[0..79] OF LREAL;
    pub_answers : Publisher;
    pub_m3 : Publisher;
    pub_m4 : Publisher;
END_VAR

a.x := LREAL#1.0; a.y := LREAL#-2.0; a.z := LREAL#3.5;
b.x := LREAL#0.25; b.y := LREAL#4.0; b.z := LREAL#-1.0;
v2.x := LREAL#3.0; v2.y := LREAL#4.0;
v4.x := LREAL#1.0; v4.y := LREAL#2.0; v4.z := LREAL#3.0; v4.w := LREAL#4.0;

sum := Double3Add(a, b);
answers[0] := sum.x; answers[1] := sum.y; answers[2] := sum.z;
v2 := Double2Rotate(v2, LREAL#0.5);
answers[3] := v2.x; answers[4] := v2.y;
v4 := Double4Normalize(v4);
answers[5] := v4.x; answers[6] := v4.y; answers[7] := v4.z; answers[8] := v4.w;
f3 := Float3FromDouble3(a);
answers[9] := REAL_TO_LREAL(f3.x); answers[10] := REAL_TO_LREAL(f3.y); answers[11] := REAL_TO_LREAL(f3.z);
i3 := Int3Floor(a);
answers[12] := DINT_TO_LREAL(i3.x); answers[13] := DINT_TO_LREAL(i3.y); answers[14] := DINT_TO_LREAL(i3.z);

q := QuaternionFromEuler(LREAL#0.1, LREAL#0.2, LREAL#0.3, EulerOrder#MovingZyx);
answers[15] := q.x; answers[16] := q.y; answers[17] := q.z; answers[18] := q.w;
to_axis(Q := q);
axis := to_axis.Axis;
angle := to_axis.Angle;
answers[19] := axis.x; answers[20] := axis.y; answers[21] := axis.z; answers[22] := angle;
m3 := Matrix3x3FromQuaternion(q);
inverse(M := m3);
m3 := inverse.Result;
m3_ok := inverse.Ok;
answers[23] := SEL(m3_ok, LREAL#0.0, LREAL#1.0);

pose_a.position := Double3Add(a, b);
pose_a.orientation := q;
pose_b.position.x := LREAL#-1.0; pose_b.position.y := LREAL#0.5; pose_b.position.z := LREAL#2.0;
axis_b := Double3Normalize(b);
pose_b.orientation := QuaternionFromAxisAngle(axis_b, LREAL#0.7);
pose_c := PoseCompose(pose_a, pose_b);
answers[24] := pose_c.position.x; answers[25] := pose_c.position.y; answers[26] := pose_c.position.z;
answers[27] := pose_c.orientation.x; answers[28] := pose_c.orientation.y;
answers[29] := pose_c.orientation.z; answers[30] := pose_c.orientation.w;
m4 := Matrix4x4FromPose(pose_c);

p2a.position.x := LREAL#1.0; p2a.position.y := LREAL#2.0; p2a.angle := LREAL#0.3;
p2b.position.x := LREAL#-0.5; p2b.position.y := LREAL#1.0; p2b.angle := LREAL#1.2;
p2c := Pose2DCompose(p2a, p2b);
answers[31] := p2c.position.x; answers[32] := p2c.position.y; answers[33] := p2c.angle;

unity_axes := AxesOf(AxesPreset#Unity);
answers[34] := SINT_TO_LREAL(unity_axes.x); answers[35] := SINT_TO_LREAL(unity_axes.y);
answers[36] := SINT_TO_LREAL(unity_axes.z); answers[37] := unity_axes.scale;
unity_point := AxesPointTo(unity_axes, a);
answers[38] := unity_point.x; answers[39] := unity_point.y; answers[40] := unity_point.z;

sphere.center.z := LREAL#5.0; sphere.radius := LREAL#1.0;
ray.direction.z := LREAL#1.0;
cast(S := sphere, Ray := ray);
hit := cast.Hit;
hit_ok := cast.Found;
answers[41] := SEL(hit_ok, LREAL#0.0, LREAL#1.0);
answers[42] := hit.point.x; answers[43] := hit.point.y; answers[44] := hit.point.z;
answers[45] := hit.normal.x; answers[46] := hit.normal.y; answers[47] := hit.normal.z;
answers[48] := hit.distance;

box.min.x := LREAL#-1.0; box.min.y := LREAL#-1.0; box.min.z := LREAL#-1.0;
box.max.x := LREAL#2.0; box.max.y := LREAL#2.0; box.max.z := LREAL#4.0;
scaled := Double3Scale(b, LREAL#0.25);
answers[49] := SEL(AlignedBoxContains(box, scaled), LREAL#0.0, LREAL#1.0);

ca.r := USINT#255; ca.a := USINT#255;
cb.b := USINT#255; cb.a := USINT#128;
cm := ColorMix(ca, cb, LREAL#0.25);
answers[50] := USINT_TO_LREAL(cm.r); answers[51] := USINT_TO_LREAL(cm.g);
answers[52] := USINT_TO_LREAL(cm.b); answers[53] := USINT_TO_LREAL(cm.a);

geo_o.lat := LREAL#52.0; geo_o.lon := LREAL#4.0; geo_o.alt := LREAL#10.0;
geo_p.lat := LREAL#52.001; geo_p.lon := LREAL#4.002; geo_p.alt := LREAL#15.0;
enu := GeoPointToEnu(geo_o, geo_p);
answers[54] := enu.x; answers[55] := enu.y; answers[56] := enu.z;

tw.linear.x := LREAL#1.0; tw.angular.z := LREAL#0.5;
tw_moved := TwistTransform(pose_a, tw);
answers[57] := tw_moved.linear.x; answers[58] := tw_moved.linear.y; answers[59] := tw_moved.linear.z;
answers[60] := tw_moved.angular.x; answers[61] := tw_moved.angular.y; answers[62] := tw_moved.angular.z;

ia.mass := LREAL#2.0; ia.center.x := LREAL#1.0;
ia.ixx := LREAL#0.1; ia.iyy := LREAL#0.1; ia.izz := LREAL#0.1;
ib.mass := LREAL#3.0; ib.center.y := LREAL#1.0;
ib.ixx := LREAL#0.2; ib.iyy := LREAL#0.2; ib.izz := LREAL#0.2;
isum := InertiaAdd(ia, ib);
answers[63] := isum.mass; answers[64] := isum.center.x; answers[65] := isum.center.y;
answers[66] := isum.center.z; answers[67] := isum.ixx;

cam.width := UDINT#640; cam.height := UDINT#480;
cam.fx := LREAL#500.0; cam.fy := LREAL#500.0; cam.cx := LREAL#320.0; cam.cy := LREAL#240.0;
project(Camera := cam, Point := Double3Scale(a, LREAL#0.5));
pixel := project.Pixel;
cam_ok := project.Ok;
answers[68] := SEL(cam_ok, LREAL#0.0, LREAL#1.0);
answers[69] := pixel.x; answers[70] := pixel.y;

ob.pose.position.x := LREAL#1.0; ob.pose.position.y := LREAL#1.0; ob.pose.angle := LREAL#0.4;
ob.size.x := LREAL#2.0; ob.size.y := LREAL#1.0;
sp := OrientedBox2DClosest(ob, v2);
answers[71] := sp.point.x; answers[72] := sp.point.y; answers[73] := sp.normal.x; answers[74] := sp.normal.y;

answers[75] := AngleWrap(LREAL#7.0);
answers[76] := SEL(Double3IsNear(a, b, LREAL#0.1), LREAL#0.0, LREAL#1.0);

pub_answers(Topic := STRING#'RantMathTest/Answers', Value := answers);
pub_m3(Topic := STRING#'RantMathTest/Inverse', Value := m3);
pub_m4(Topic := STRING#'RantMathTest/Matrix', Value := m4);
END_PROGRAM
