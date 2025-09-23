#include "sylar/address.h"

void test() {
    std::vector<sylar::Address::ptr> addrs;

    bool v = sylar::Address::Lookup(addrs, "www.baidu.com:http");
    if (!v) {
        std::cout << "lookup fail" << std::endl;
        return ;
    }
    for (size_t i = 0; i < addrs.size(); ++i) {
        std::cout << i << " - " << addrs[i]->toString() << std::endl;
    }
}

void test_iface() {
    std::multimap<std::string, std::pair<sylar::Address::ptr, uint32_t>> results;

    bool v = sylar::Address::GetInterfaceAddresses(results);
    if (!v) {
        std::cout << "GetInterfaceAddresses fail" << std::endl;
        return ;
    }
    for (auto i : results) {
        std::cout << i.first << " - " << i.second.first->toString() 
        << " - " << i.second.second << std::endl;
    }
}

void test_ipv4() {
    //auto addr = sylar::IPAddress::Create("www.baidu.com");
    auto addr = sylar::IPAddress::Create("127.0.0.1");
    if (addr) {
        std::cout << addr->toString() << std::endl;
    }
}

int main(int argc, char** argv) {
    //test();
    //test_iface();
    test_ipv4();
    return 0;
}