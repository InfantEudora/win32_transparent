#ifndef _TYPE_INT2_H_
#define _TYPE_INT2_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct int2;

struct int2{
	union{
		int x;
		int q;
	};
	union{
		int y;
		int r;
	};

    int2() : x(0), y(0) {}
    int2(int x, int y) : x(x), y(y){}

    bool    operator==(const int2& rhs) const;
    int2   	operator-(const int2& rhs) const;    // subtract rhs
};

inline bool int2::operator==(const int2& rhs) const {
    return (x == rhs.x) && (y == rhs.y);
}

inline int2 int2::operator-(const int2& rhs) const {
    return int2(x-rhs.x, y-rhs.y);
}

#endif