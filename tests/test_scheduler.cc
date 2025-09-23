#include "sylar/sylar.h"
#include <iostream>
#include <unistd.h>

void test_fiber() {
    loginfo2("test in fiber");
    sleep(1);
    //sylar::Scheduler::GetThis()->schedule(&test_fiber);
    
}

int main(int argc, char **argv) {
    std::cout << "tests begin" << std::endl;
    sylar::Scheduler sc(2, false, "test_main");
    sc.start();
    sc.schedule(&test_fiber);
    sc.stop();
    std::cout << "tests end" << std::endl;
    return 0;
}