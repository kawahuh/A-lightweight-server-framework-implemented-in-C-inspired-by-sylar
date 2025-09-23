#ifndef __SYLAR_LOG_H__
#define __SYLAR_LOG_H__

#include <string>
#include <iostream>
#include <mutex>
using namespace std;

inline void loginfo2(string s) {
    static mutex lock;
    lock.lock();
    cout << ": " << s << endl;
    lock.unlock();
}

#endif