"""The PC side of the math test: RantMathTest's answers on the PLC against Rant Math's Python binding."""
import sys
import threading
import time

import rant
import rant_math as m
from rant.types import (AlignedBox, CameraIntrinsics, Color, Double2, Double3, Double4, GeoPoint, Inertia,
                        Matrix3x3, Matrix4x4, OrientedBox2D, Pose, Pose2D, Ray, Sphere, Twist)

latest = {}
lock = threading.Lock()


def keep(name):
    def on_value(v):
        with lock:
            latest[name] = v
    return on_value


node = rant.Node("math_test_pc", domain=0)
for name, schema in [("RantMathTest/Answers", "f64[80]"), ("RantMathTest/Inverse", Matrix3x3),
                     ("RantMathTest/Matrix", Matrix4x4)]:
    node.subscriber(name, schema, keep(name), reliable=True, catch_up=1)


def xyz(v):
    return [v.x, v.y, v.z]


a = Double3(x=1.0, y=-2.0, z=3.5)
b = Double3(x=0.25, y=4.0, z=-1.0)
want = [0.0] * 80
want[0:3] = xyz(m.double3_add(a, b))
v2 = m.double2_rotate(Double2(x=3.0, y=4.0), 0.5)
want[3:5] = [v2.x, v2.y]
v4 = m.double4_normalize(Double4(x=1.0, y=2.0, z=3.0, w=4.0))
want[5:9] = [v4.x, v4.y, v4.z, v4.w]
want[9:12] = xyz(m.float3_from_double3(a))
want[12:15] = xyz(m.int3_floor(a))
q = m.quaternion_from_euler(0.1, 0.2, 0.3, m.EulerOrder.MovingZyx)
want[15:19] = [q.x, q.y, q.z, q.w]
axis, angle = m.quaternion_to_axis_angle(q)
want[19:23] = xyz(axis) + [angle]
want[23] = 1.0
pose_a = Pose(position=m.double3_add(a, b), orientation=q)
pose_b = Pose(position=Double3(x=-1.0, y=0.5, z=2.0),
              orientation=m.quaternion_from_axis_angle(m.double3_normalize(b), 0.7))
pose_c = m.pose_compose(pose_a, pose_b)
o = pose_c.orientation
want[24:31] = xyz(pose_c.position) + [o.x, o.y, o.z, o.w]
p2 = m.pose2d_compose(Pose2D(position=Double2(x=1.0, y=2.0), angle=0.3),
                      Pose2D(position=Double2(x=-0.5, y=1.0), angle=1.2))
want[31:34] = [p2.position.x, p2.position.y, p2.angle]
unity = m.axes_of(m.AxesPreset.Unity)
want[34:38] = [unity.x, unity.y, unity.z, unity.scale]
want[38:41] = xyz(m.axes_point_to(unity, a))
hit = m.sphere_raycast(Sphere(center=Double3(x=0.0, y=0.0, z=5.0), radius=1.0),
                       Ray(origin=Double3(), direction=Double3(x=0.0, y=0.0, z=1.0)))
want[41:49] = [1.0] + xyz(hit.point) + xyz(hit.normal) + [hit.distance]
box = AlignedBox(min=Double3(x=-1.0, y=-1.0, z=-1.0), max=Double3(x=2.0, y=2.0, z=4.0))
want[49] = 1.0 if m.aligned_box_contains(box, m.double3_scale(b, 0.25)) else 0.0
c = m.color_mix(Color(r=255, g=0, b=0, a=255), Color(r=0, g=0, b=255, a=128), 0.25)
want[50:54] = [c.r, c.g, c.b, c.a]
want[54:57] = xyz(m.geo_point_to_enu(GeoPoint(lat=52.0, lon=4.0, alt=10.0), GeoPoint(lat=52.001, lon=4.002, alt=15.0)))
tw = m.twist_transform(pose_a, Twist(linear=Double3(x=1.0), angular=Double3(z=0.5)))
want[57:63] = xyz(tw.linear) + xyz(tw.angular)
total = m.inertia_add(Inertia(mass=2.0, center=Double3(x=1.0), ixx=0.1, iyy=0.1, izz=0.1),
                      Inertia(mass=3.0, center=Double3(y=1.0), ixx=0.2, iyy=0.2, izz=0.2))
want[63:68] = [total.mass] + xyz(total.center) + [total.ixx]
cam = CameraIntrinsics(width=640, height=480, fx=500.0, fy=500.0, cx=320.0, cy=240.0)
pixel = m.camera_project(cam, m.double3_scale(a, 0.5))
want[68:71] = [1.0, pixel.x, pixel.y]
sp = m.oriented_box2d_closest(OrientedBox2D(pose=Pose2D(position=Double2(x=1.0, y=1.0), angle=0.4),
                                            size=Double2(x=2.0, y=1.0)), v2)
want[71:75] = [sp.point.x, sp.point.y, sp.normal.x, sp.normal.y]
want[75] = m.angle_wrap(7.0)
want[76] = 1.0 if m.double3_is_near(a, b, 0.1) else 0.0
inverse = m.matrix3x3_inverse(m.matrix3x3_from_quaternion(q))
matrix = m.matrix4x4_from_pose(pose_c)

# Which call each answer comes from, to name a failure.
NAMES = [(0, "Double3Add"), (3, "Double2Rotate"), (5, "Double4Normalize"), (9, "Float3FromDouble3"),
         (12, "Int3Floor"), (15, "QuaternionFromEuler"), (19, "QuaternionToAxisAngle"), (23, "Matrix3x3Inverse"),
         (24, "PoseCompose"), (31, "Pose2DCompose"), (34, "AxesOf"), (38, "AxesPointTo"), (41, "SphereRaycast"),
         (49, "AlignedBoxContains"), (50, "ColorMix"), (54, "GeoPointToEnu"), (57, "TwistTransform"),
         (63, "InertiaAdd"), (68, "CameraProject"), (71, "OrientedBox2DClosest"), (75, "AngleWrap"),
         (76, "Double3IsNear"), (77, "")]

deadline = time.time() + 15
while time.time() < deadline:
    with lock:
        if len(latest) == 3:
            break
    time.sleep(0.2)
ok = True


def check(what, got, expected, tol):
    global ok
    bad = [(k, g, e) for k, (g, e) in enumerate(zip(got, expected)) if abs(g - e) > tol]
    good = len(got) == len(expected) and not bad
    print(("PASS " if good else "FAIL ") + what + ("" if good else ": " + str(bad[:3])), flush=True)
    ok = ok and good


with lock:
    got = latest.get("RantMathTest/Answers")
    got_inverse = latest.get("RantMathTest/Inverse")
    got_matrix = latest.get("RantMathTest/Matrix")
if got is None or got_inverse is None or got_matrix is None:
    print("FAIL nothing from RantMathTest:", sorted(latest))
    sys.exit(1)
got = list(got)
for (first, name), (end, _) in zip(NAMES, NAMES[1:]):
    tol = 1e-6 if name == "Float3FromDouble3" else 1e-9
    check(name, got[first:end], want[first:end], tol)
flat = lambda rows: [x for row in rows for x in row]
check("Matrix3x3FromQuaternion then Matrix3x3Inverse", flat(got_inverse), flat(inverse), 1e-5)
check("Matrix4x4FromPose", flat(got_matrix), flat(matrix), 1e-5)
sys.exit(0 if ok else 1)
