#include "sylar/sylar.h"
#include <assert.h>
#include <iostream>

void test_assert() {
    std::cout << sylar::BacktraceToString(10, 0, "       ");
}
 
int main(int argc, char **argv) {
    test_assert();

    return 0;
}