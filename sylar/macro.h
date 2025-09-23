#ifndef __SYLAR_MACRO_H__
#define __SYLAR_MACRO_H__

#include <string.h>
#include <assert.h>
#include <iostream>
#include "util.h"

// #if defined __GNUC__ || defined __llvm__
    #define SYLAR_LICKLY(x)     __builtin_expect(!!(x), 1)
    #define SYLAR_UNLICKLY(x)   __builtin_expect(!!(x), 0)
// #else
//     #define SYLAR_LIKELY(x)     (x)
//     #define SYLAR_UNLIKEL(x)    (x)
// #endif

#define SYLAR_ASSERT(x)\
    if(SYLAR_UNLICKLY(!(x))) {\
        std::cout << "assertion error" << std::endl;\
        assert(x);\
    }

#define SYLAR_ASSERT2(x, w)\
    if(SYLAR_UNLICKLY(!(x))) {\
        std::cout << "assertion error" << std::endl;\
        assert(x);\
    }

#endif