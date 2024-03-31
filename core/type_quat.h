#ifndef _TYPE_QUAT_H_
#define _TYPE_QUAT_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct quat;
#include "type_vec3.h"

struct quat{
    float x;
    float y;
    float z;
    float w;

    quat(){};
    quat(float x, float y, float z,float w) : x(x), y(y), z(z),w(w){}
    quat(const vec3& axis, float angle);            // rot axis & angle (radian)


    //Functions
    void    print();
    void    identity();
    void    set_rotation(const vec3& axis, float angle);

    fmat4   tofmat4() const;

    vec3    operator*(const vec3& rhs) const; // rotate a vector
    quat    operator*(const quat& rhs) const; // multiplication

    static quat getquat(const vec3& v1, const vec3& v2);
    static quat getquat(const vec3& target, const vec3& position, const vec3& worldup);
};

inline quat::quat(const vec3& axis, float angle){
    set_rotation(axis, angle);
}

inline void quat::identity(){
    x = 0;
    y = 0;
    z = 0;
    w = 1;
}

inline void quat::set_rotation(const vec3& axis, float angle){
    // use only half angle because of double multiplication, qpq*,
    // q at the front and its conjugate at the back
    angle = 0.5 * angle;
    vec3 v = axis;
    //v = vertex_unit(&v);                  // convert to unit vector
    float sine = sinf(angle);       // angle is radian
    w = cosf(angle);
    x = v.x * sine;
    y = v.y * sine;
    z = v.z * sine;
}

inline quat quat::operator*(const quat& rhs) const{
    // qq' = [s,v] * [s',v'] = [(ss' - v . v'), v x v' + sv' + s'v]
    //NOTE: quat multiplication is not commutative
    vec3 v1(x, y, z);                            // vector part of q
    vec3 v2(rhs.x, rhs.y, rhs.z);                // vector part of q'

    vec3 cross = v1.cross(v2);                   // v x v' (cross product)
    float dot = v1.dot(v2);                      // v . v' (inner product)
    vec3 v3 = cross + (w * v2) + (rhs.w * v1);   // v x v' + sv' + s'v
    return quat(v3.x, v3.y, v3.z,w * rhs.w - dot);
}

// Overloaded operator for the multiplication with a vectoleft.
/// This methods rotates a point given the rotation of a quaternion.
inline vec3 quat::operator*(const vec3& rhs) const {
    /* The following code is equivalent to this
     * Quaternion p(point.x, point.y, point.z, 0.0);
     * return (((*this) * p) * getConjugate()).getVectorV();
    */
    const float prodX = w * rhs.x + y * rhs.z - z * rhs.y;
    const float prodY = w * rhs.y + z * rhs.x - x * rhs.z;
    const float prodZ = w * rhs.z + x * rhs.y - y * rhs.x;
    const float prodW = -x * rhs.x - y * rhs.y - z * rhs.z;
    return vec3(w * prodX - prodY * z + prodZ * y - prodW * x,
                   w * prodY - prodZ * x + prodX * z - prodW * y,
                   w * prodZ - prodX * y + prodY * x - prodW * z);
}

inline fmat4 quat::tofmat4() const{
    // NOTE: assume the quat is unit length
    // compute common values
    float x2  = x + x;
    float y2  = y + y;
    float z2  = z + z;
    float xx2 = x * x2;
    float xy2 = x * y2;
    float xz2 = x * z2;
    float yy2 = y * y2;
    float yz2 = y * z2;
    float zz2 = z * z2;
    float sx2 = w * x2;
    float sy2 = w * y2;
    float sz2 = w * z2;

    // build 4x4 matrix (column-major) and return
    return fmat4{vec4(1 - (yy2 + zz2),  xy2 + sz2,        xz2 - sy2,        0), // column 0
                  vec4( xy2 - sz2,        1 - (xx2 + zz2),  yz2 + sx2,        0), // column 1
                  vec4( xz2 + sy2,        yz2 - sx2,        1 - (xx2 + yy2),  0), // column 2
                  vec4( 0,                0,                0,                1)};// column 3

    // for non-unit quat
    // ss+xx-yy-zz, 2xy+2sz,     2xz-2sy,     0
    // 2xy-2sz,     ss-xx+yy-zz, 2yz-2sx,     0
    // 2xz+2sy,     2yz+2sx,     ss-xx-yy+zz, 0
    // 0,           0,           0,           1
}

// find quat for rotating from v1 to v2
inline quat quat::getquat(const vec3& v1, const vec3& v2){
    const float HALF_PI = acos(-1) * 0.5f;

    // if two vectors are equal return the vector with 0 rotation
    if(v1.equal(v2)){
        return quat(v1, 0);
    }else if(v1.equal(-v2)){
        // if two vectors are opposite return a perpendicular vector with 180 angle
        vec3 v;
        if(v1.x > -FT_EPSILON && v1.x < FT_EPSILON)       // if x ~= 0
            v.set(1, 0, 0);
        else if(v1.y > -FT_EPSILON && v1.y < FT_EPSILON)  // if y ~= 0
            v.set(0, 1, 0);
        else                                        // if z ~= 0
            v.set(0, 0, 1);
        return quat(v, HALF_PI);
    }

    vec3 u1 = v1;                    // convert to normal vector
    vec3 u2 = v2;
    u1.normalize();
    u2.normalize();

    vec3 v = u1.cross(u2);           // compute rotation axis
    v.normalize();

    float dot = u1.dot(u2);
    dot = clamp(dot,0.0,1.0);
    float angle = acosf(dot);    // rotation angle: This was being naugty getting fed a 1.0000001f = nan

    return quat(v, angle * 0.5f); // half angle
}


//Constructs a quaternion from a lookat and position. Supplied worldup should point in the half sphere that is up.
inline quat quat::getquat(const vec3& target, const vec3& position, const vec3& worldup){
    vec3 lookat = (position - target).normalize();
    vec3 left = worldup.cross(lookat).normalize();
    vec3 u = lookat.cross(left);

    quat q;
    double trace = left.x + u.y + lookat.z;
    if (trace > 0.0) {
        double s = 0.5 / sqrt(trace + 1.0);
        q.w = 0.25 / s;
        q.x = (u.z - lookat.y) * s;
        q.y = (lookat.x - left.z) * s;
        q.z = (left.y - u.x) * s;
    } else {
    if (left.x > u.y && left.x > lookat.z) {
            double s = 2.0 * sqrt(1.0 + left.x - u.y - lookat.z);
            q.w = (u.z - lookat.y) / s;
            q.x = 0.25 * s;
            q.y = (u.x + left.y) / s;
            q.z = (lookat.x + left.z) / s;
        } else if (u.y > lookat.z) {
            double s = 2.0 * sqrt(1.0 + u.y - left.x - lookat.z);
            q.w = (lookat.x - left.z) / s;
            q.x = (u.x + left.y) / s;
            q.y = 0.25 * s;
            q.z = (lookat.y + u.z) / s;
        } else {
            double s = 2.0 * sqrt(1.0 + lookat.z - left.x - u.y);
            q.w = (left.y - u.x) / s;
            q.x = (lookat.x + left.z) / s;
            q.y = (lookat.y + u.z) / s;
            q.z = 0.25 * s;
        }
    }
    return q;
}

inline void quat::print(){
    printf("quat (xyzw): %.3f %.3f %.3f %.3f\n",x,y,z,w);
}

#endif