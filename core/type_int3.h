#ifndef _TYPE_INT3_H_
#define _TYPE_INT3_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct int3;

struct int3{
	union{
		int x;
		int q;
	};
	union{
		int y;
		int r;
	};
    union{
		int z;
		int s;
	};
    int3() : x(0), y(0), z(0) {}
    int3(int x, int y, int z) : x(x), y(y), z(z){}

    bool operator==(const int3& rhs) const;
};

inline bool int3::operator==(const int3& rhs) const {
    return (x == rhs.x) && (y == rhs.y) && (z == rhs.z);
}

#endif