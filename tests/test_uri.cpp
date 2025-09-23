#include "sylar/uri.h"
#include <iostream>

int main() {
    sylar::Uri::ptr uri = sylar::Uri::Create("http://www.baidu.com/test/uri?id=100&name=sylar&vv=s#frg");
    std::cout << uri->toString() << std::endl;
    auto addr = uri->createAddress();
    std::cout << addr->toString() << std::endl;

    return 0;
}