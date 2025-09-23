#include "sylar/thread.h"
#include "sylar/util.h"
#include <stdio.h>
#include <iostream>
#include <vector>

int count = 0;
sylar::RWMutex s_mutex;

// test passed if locks worked
void fun1() {
    std::cout << "name: " <<  sylar::Thread::GetName() 
        << "this.name: " << sylar::Thread::GetThis()->getName()
        << "id: " << sylar::GetThreadId()
        << "this.id: " << sylar::Thread::GetThis()->getId()
        << std::endl;

    for (int i = 0; i < 1000000; ++i) {
        sylar::RWMutex::WriteLock lock(s_mutex);
        ++count;
    }
}

int main(int argc, char **argv) {
    printf("test thread begin\n");
    std::vector<sylar::Thread::ptr> thrs;
    for (int i = 0; i < 5; ++i) {
        sylar::Thread::ptr thr(new sylar::Thread(&fun1, "testThread" + std::to_string(i)));
        thrs.push_back(thr);
    }
    
    for (int i = 0; i < 5; i++) {
        thrs[i]->join();
    }

    printf("test ends\n");

    std::cout << count << std::endl;

    return 0;
}

