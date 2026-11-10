#ifndef __RANT_H__
#define __RANT_H__
// PHOENIX CONTACT Software eCLR Native Library Builder version 3.0.1043.0
// Rant.h native assembly header template.
// Use this file to implement and/or extend your native c++ classes.
// Insert additional private methods and member only outside of the @Begin/@End comment pair

#include "EclrInterfaces.h"

#include "Rant-version.h"

// public scope class for library implementation
// use Rant::init() and Rant::loadLibrary() during startup 
// to make the library accessible by the managed application.

class Rant
{
    // @Begin automatically generated code, do not modify !
public:
    enum LibVersion { Major=0, Minor=1, Build=0, Revision=0 };
    //library user interface
    static bool init();
    static bool isCompatible(const EclrVersion& requestedVersion);
    static bool load(ClrAppDomain* pAppDomain);
    static bool unload(ClrAppDomain* pAppDomain);
    //automatic generated loader stub
    static bool loadLibrary();
    static bool checkLayout();

    static unsigned s_modulHndl;
    // @End automatically generated code

    class Runtime : public Object
    {
        // @Begin automatically generated code, do not modify inside @Begin/@End comment pair !
    public:
        static Int32 Configure(pcoslib::IecString80* p0, Int32 p1);
        static Int32 Entity(Int32 p0, Int32 p1, pcoslib::IecString80* p2);
        static Boolean NeedsLayout(Int32 p0, Int32 p1, UIntPtr p2);
        static Int32 Field(Int32 p0, Int32 p1, Int32 p2, UIntPtr p3, UIntPtr p4, UIntPtr p5, Int32 p6, Int32 p7, Int32 p8, Int32 p9);
        static void Bound(Int32 p0, Int32 p1, Int32 p2, UIntPtr p3, Int32 p4);
        static void LayoutDone(Int32 p0, Int32 p1, UIntPtr p2);
        static Int32 Publish(Int32 p0, UIntPtr p1, Int32 p2, Boolean p3);
        static Int32 Take(Int32 p0, UIntPtr p1, Int32 p2);
        static Int32 Share(Int32 p0, UIntPtr p1, Int32 p2);
        static Int32 Serve(Int32 p0, UIntPtr p1, Int32 p2, UIntPtr p3, Int32 p4, UIntPtr p5, Int32 p6, Boolean p7);
        static void Reject(Int32 p0, pcoslib::IecString80* p1);
        static Int32 Call(Int32 p0, Boolean p1, Boolean p2, UIntPtr p3, Int32 p4, UIntPtr p5, Int32 p6, UIntPtr p7, Int32 p8);
        static Boolean Failed(Int32 p0, pcoslib::IecString80* p1);
        static Int32 Mirror(pcoslib::IecString80* p0, Boolean p1, Boolean p2, Int32 p3);
        static Boolean MirrorFailed(pcoslib::IecString80* p0);
        Runtime();
        void ctor();
        // @End automatically generated code
        // insert additional private methods and member here ! 
    };


    class RantMath : public Object
    {
        // @Begin automatically generated code, do not modify inside @Begin/@End comment pair !
    public:
        static void Double2Add(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Subtract(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Scale(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Multiply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Dot(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Cross(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Length(UIntPtr p0, UIntPtr p1);
        static void Double2LengthSquared(UIntPtr p0, UIntPtr p1);
        static void Double2Distance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Double2IsNear(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Normalize(UIntPtr p0, UIntPtr p1);
        static void Double2Lerp(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void Double2Min(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Max(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Angle(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Heading(UIntPtr p0, UIntPtr p1);
        static void Double2FromHeading(UIntPtr p0, UIntPtr p1);
        static void Double2Rotate(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Perpendicular(UIntPtr p0, UIntPtr p1);
        static void Double2Project(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double2Reflect(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Add(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Subtract(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Scale(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Multiply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Dot(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Cross(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Length(UIntPtr p0, UIntPtr p1);
        static void Double3LengthSquared(UIntPtr p0, UIntPtr p1);
        static void Double3Distance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Double3IsNear(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Normalize(UIntPtr p0, UIntPtr p1);
        static void Double3Lerp(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void Double3Min(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Max(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Angle(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Project(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Reflect(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double3Perpendicular(UIntPtr p0, UIntPtr p1);
        static void Double4Add(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double4Subtract(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double4Scale(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double4Dot(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Double4Length(UIntPtr p0, UIntPtr p1);
        static void Double4Normalize(UIntPtr p0, UIntPtr p1);
        static void Double4Lerp(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void Double2FromFloat2(UIntPtr p0, UIntPtr p1);
        static void Double3FromFloat3(UIntPtr p0, UIntPtr p1);
        static void Double4FromFloat4(UIntPtr p0, UIntPtr p1);
        static void Float2FromDouble2(UIntPtr p0, UIntPtr p1);
        static void Float3FromDouble3(UIntPtr p0, UIntPtr p1);
        static void Float4FromDouble4(UIntPtr p0, UIntPtr p1);
        static void Double2FromInt2(UIntPtr p0, UIntPtr p1);
        static void Double3FromInt3(UIntPtr p0, UIntPtr p1);
        static void Int2Floor(UIntPtr p0, UIntPtr p1);
        static void Int3Floor(UIntPtr p0, UIntPtr p1);
        static void QuaternionFromEuler(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3, UIntPtr p4);
        static void QuaternionFromAxisAngle(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void QuaternionToAxisAngle(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void QuaternionFromRotationVector(UIntPtr p0, UIntPtr p1);
        static void QuaternionToRotationVector(UIntPtr p0, UIntPtr p1);
        static void QuaternionFromMatrix3x3(UIntPtr p0, UIntPtr p1);
        static void Matrix3x3FromQuaternion(UIntPtr p0, UIntPtr p1);
        static void QuaternionBetween(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void QuaternionLook(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void QuaternionCompose(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void QuaternionInverse(UIntPtr p0, UIntPtr p1);
        static void QuaternionNormalize(UIntPtr p0, UIntPtr p1);
        static void QuaternionRotate(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void QuaternionSlerp(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void QuaternionAngle(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean QuaternionIsNear(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void QuaternionIntegrate(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void QuaternionYaw(UIntPtr p0, UIntPtr p1);
        static void AngleWrap(UIntPtr p0, UIntPtr p1);
        static void AngleDifference(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Matrix3x3Identity(UIntPtr p0);
        static void Matrix3x3Multiply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Matrix3x3Transpose(UIntPtr p0, UIntPtr p1);
        static void Matrix3x3Determinant(UIntPtr p0, UIntPtr p1);
        static Boolean Matrix3x3Inverse(UIntPtr p0, UIntPtr p1);
        static void Matrix3x3Apply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Matrix4x4Identity(UIntPtr p0);
        static void Matrix4x4Multiply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Matrix4x4Transpose(UIntPtr p0, UIntPtr p1);
        static void Matrix4x4Determinant(UIntPtr p0, UIntPtr p1);
        static Boolean Matrix4x4Inverse(UIntPtr p0, UIntPtr p1);
        static void Matrix4x4Apply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Matrix4x4ApplyDirection(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PoseCompose(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PoseInverse(UIntPtr p0, UIntPtr p1);
        static void PoseBetween(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PoseApply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PoseApplyInverse(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PoseInterpolate(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static Boolean PoseFromPoints(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static Boolean PoseLookAt(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void Matrix4x4FromPose(UIntPtr p0, UIntPtr p1);
        static void PoseFromMatrix4x4(UIntPtr p0, UIntPtr p1);
        static void Pose2DFromPose(UIntPtr p0, UIntPtr p1);
        static void PoseFromPose2D(UIntPtr p0, UIntPtr p1);
        static void Pose2DCompose(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Pose2DInverse(UIntPtr p0, UIntPtr p1);
        static void Pose2DBetween(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Pose2DApply(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Pose2DApplyInverse(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Pose2DInterpolate(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void TwistTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void WrenchTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PoseIntegrate(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static Boolean TwistBetween(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void Twist2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Wrench2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Pose2DIntegrate(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static Boolean Twist2DBetween(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void Covariance3Rotate(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Covariance6Rotate(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Covariance3Ellipsoid(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void InertiaTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void InertiaAdd(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean InertiaBox(UIntPtr p0, UIntPtr p1);
        static void AxesOf(UIntPtr p0, UIntPtr p1);
        static Boolean AxesMake(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3, UIntPtr p4);
        static void AxesPointTo(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesPointFrom(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesDirectionTo(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesDirectionFrom(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesQuaternionTo(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesQuaternionFrom(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesPoseTo(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesPoseFrom(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesTwistTo(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesTwistFrom(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesWrenchTo(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesWrenchFrom(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesAccelTo(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesAccelFrom(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesCovariance6To(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AxesCovariance6From(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean PlaneFromPoints(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void PlaneDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PlaneClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean PlaneRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void PlaneTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean PlaneIntersect(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean PlaneIntersect3(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void SegmentLength(UIntPtr p0, UIntPtr p1);
        static void SegmentClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SegmentDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SegmentClosestSegment(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SegmentTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SegmentBounds(UIntPtr p0, UIntPtr p1);
        static void RayAt(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void RayClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void RayDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void RayClosestRay(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void RayTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Plane2DFromPoints(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Plane2DDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Plane2DClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Plane2DRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Plane2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Plane2DIntersect(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Segment2DLength(UIntPtr p0, UIntPtr p1);
        static void Segment2DClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Segment2DDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Segment2DClosestSegment(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Segment2DIntersect(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Segment2DRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Segment2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Segment2DBounds(UIntPtr p0, UIntPtr p1);
        static void Ray2DAt(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Ray2DClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Ray2DDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Ray2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBox2DDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBox2DClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean AlignedBox2DContains(UIntPtr p0, UIntPtr p1);
        static Boolean AlignedBox2DRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBox2DCentroid(UIntPtr p0, UIntPtr p1);
        static void AlignedBox2DArea(UIntPtr p0, UIntPtr p1);
        static void AlignedBox2DPerimeter(UIntPtr p0, UIntPtr p1);
        static void AlignedBox2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBox2DMerge(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean AlignedBox2DOverlap(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBox2DGrow(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBox2DIou(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBox2DDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBox2DClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean OrientedBox2DContains(UIntPtr p0, UIntPtr p1);
        static Boolean OrientedBox2DRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBox2DBounds(UIntPtr p0, UIntPtr p1);
        static void OrientedBox2DArea(UIntPtr p0, UIntPtr p1);
        static void OrientedBox2DPerimeter(UIntPtr p0, UIntPtr p1);
        static void OrientedBox2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBox2DFromAlignedBox2D(UIntPtr p0, UIntPtr p1);
        static void OrientedBox2DIou(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CircleDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CircleClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean CircleContains(UIntPtr p0, UIntPtr p1);
        static Boolean CircleRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CircleBounds(UIntPtr p0, UIntPtr p1);
        static void CircleArea(UIntPtr p0, UIntPtr p1);
        static void CirclePerimeter(UIntPtr p0, UIntPtr p1);
        static void CircleTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Capsule2DDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Capsule2DClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean Capsule2DContains(UIntPtr p0, UIntPtr p1);
        static Boolean Capsule2DRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void Capsule2DBounds(UIntPtr p0, UIntPtr p1);
        static void Capsule2DArea(UIntPtr p0, UIntPtr p1);
        static void Capsule2DPerimeter(UIntPtr p0, UIntPtr p1);
        static void Capsule2DTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBoxDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBoxClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean AlignedBoxContains(UIntPtr p0, UIntPtr p1);
        static Boolean AlignedBoxRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBoxCentroid(UIntPtr p0, UIntPtr p1);
        static void AlignedBoxVolume(UIntPtr p0, UIntPtr p1);
        static void AlignedBoxArea(UIntPtr p0, UIntPtr p1);
        static void AlignedBoxInertia(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBoxTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBoxMerge(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean AlignedBoxOverlap(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBoxGrow(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void AlignedBoxIou(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBoxDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBoxClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean OrientedBoxContains(UIntPtr p0, UIntPtr p1);
        static Boolean OrientedBoxRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBoxBounds(UIntPtr p0, UIntPtr p1);
        static void OrientedBoxVolume(UIntPtr p0, UIntPtr p1);
        static void OrientedBoxArea(UIntPtr p0, UIntPtr p1);
        static void OrientedBoxInertia(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBoxTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void OrientedBoxFromAlignedBox(UIntPtr p0, UIntPtr p1);
        static void OrientedBoxIou(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SphereDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SphereClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean SphereContains(UIntPtr p0, UIntPtr p1);
        static Boolean SphereRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SphereBounds(UIntPtr p0, UIntPtr p1);
        static void SphereVolume(UIntPtr p0, UIntPtr p1);
        static void SphereArea(UIntPtr p0, UIntPtr p1);
        static void SphereInertia(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void SphereTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CapsuleDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CapsuleClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean CapsuleContains(UIntPtr p0, UIntPtr p1);
        static Boolean CapsuleRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CapsuleBounds(UIntPtr p0, UIntPtr p1);
        static void CapsuleCentroid(UIntPtr p0, UIntPtr p1);
        static void CapsuleVolume(UIntPtr p0, UIntPtr p1);
        static void CapsuleArea(UIntPtr p0, UIntPtr p1);
        static void CapsuleInertia(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CapsuleTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CylinderDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CylinderClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean CylinderContains(UIntPtr p0, UIntPtr p1);
        static Boolean CylinderRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CylinderBounds(UIntPtr p0, UIntPtr p1);
        static void CylinderCentroid(UIntPtr p0, UIntPtr p1);
        static void CylinderVolume(UIntPtr p0, UIntPtr p1);
        static void CylinderArea(UIntPtr p0, UIntPtr p1);
        static void CylinderInertia(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void CylinderTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void ConeDistance(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void ConeClosest(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean ConeContains(UIntPtr p0, UIntPtr p1);
        static Boolean ConeRaycast(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void ConeBounds(UIntPtr p0, UIntPtr p1);
        static void ConeCentroid(UIntPtr p0, UIntPtr p1);
        static void ConeVolume(UIntPtr p0, UIntPtr p1);
        static void ConeArea(UIntPtr p0, UIntPtr p1);
        static void ConeInertia(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void ConeTransform(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void ColorMix(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void ColorToLinear(UIntPtr p0, UIntPtr p1);
        static void ColorFromLinear(UIntPtr p0, UIntPtr p1);
        static void ColormapAt(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean CameraProject(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean CameraRay(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static Boolean CameraUnproject(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static Boolean CameraScale(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static Boolean CameraCrop(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3, UIntPtr p4, UIntPtr p5);
        static Boolean CameraFov(UIntPtr p0, UIntPtr p1);
        static Boolean CameraFromFov(UIntPtr p0, UIntPtr p1, UIntPtr p2, UIntPtr p3);
        static void GeoPointToEcef(UIntPtr p0, UIntPtr p1);
        static void GeoPointFromEcef(UIntPtr p0, UIntPtr p1);
        static void GeoPointToEnu(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        static void GeoPointFromEnu(UIntPtr p0, UIntPtr p1, UIntPtr p2);
        RantMath();
        void ctor();
        // @End automatically generated code
        // insert additional private methods and member here ! 
    };


}; // class Rant

#endif //#ifndef __RANT_H__
