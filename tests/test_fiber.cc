#include "sylar/sylar.h"
#include <iostream>

void run_in_fiber() {
    std::cout << "run in fiber begin" << std::endl;
    for (int i = 0; i < 5; ++i) {
        std::cout << "fiber" << sylar::GetFiberId() << std::endl;
    }
    sylar::Fiber::GetThis()->back();
    std::cout << "run in fiber end" << std::endl;
    sylar::Fiber::GetThis()->back();
}

void test_fiber() { 
    std::cout << "test_fiber begin -1" << std::endl;
    {
        sylar::Fiber::GetThis();
        std::cout << "main begin" << std::endl;
        sylar::Fiber::ptr fiber(new sylar::Fiber(run_in_fiber, 128*1024, true));
        fiber->call();
        std::cout << "main run after swapin" << std::endl;
        fiber->call();
        std::cout << "main end" << std::endl;
        fiber->call();
    }
    std::cout << "main end2" << std::endl;
}

int main(int argc, char **argv) {
    sylar::Thread::SetName("main");

    std::vector<sylar::Thread::ptr> thrs;
    for (int i = 0; i < 1; ++i) {
        thrs.push_back(sylar::Thread::ptr (
            new sylar::Thread(&test_fiber, "name_"+ std::to_string(i))));
    }
    for (auto i : thrs) {
        i->join();
    }
    std::cout << "all tests ends" << std::endl;

    return 0;
}