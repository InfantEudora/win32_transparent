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

    //Functions
    void    print();
    void    identity();
    void    set_rotation(const vec3& axis, float angle);

    fmat4   tofmat4() const;

    quat    operator*(const quat& rhs) const; // multiplication
};

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

inline void quat::print(){
    printf("quat (xyzw): %.3f %.3f %.3f %.3f\n",x,y,z,w);
}

#endif