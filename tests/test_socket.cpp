#include "sylar/socket.h"
#include "sylar/sylar.h"
#include "sylar/iomanager.h"

void test_socket() {
    // sylar::IPAddress::ptr addr = sylar::Address::LookupAnyIPAddress("www.baidu.com");
    std::vector<sylar::Address::ptr> addrs;
    sylar::Address::Lookup(addrs, "www.baidu.com", AF_INET);
    sylar::IPAddress::ptr addr;
    for (auto& i : addrs) {
        std::cout << i->toString() << std::endl;
        addr = std::dynamic_pointer_cast<sylar::IPAddress>(i);
        if (addr) {
            break;
        }
    }

    if (addr) {
        std::cout << "get address: " << addr->toString() << std::endl;
    } else {
        std::cout << "get address fail" << std::endl;
        return ;
    }

    sylar::Socket::ptr sock = sylar::Socket::CreateTCP(addr);
    addr->setPort(80);
    if (!sock->connect(addr)) {
        std::cout << "connect " << addr->toString() << " fail" << std::endl;
        return ;
    } else {
        std::cout << "connect " << addr->toString() << " connected" << std::endl;
    }

    const char buff[] = "GET / HTTP/1.0\r\n\r\n";
    int rt = sock->send(buff, sizeof(buff));
    if (rt <= 0) {
        std::cout << "send fail rt=" << rt << std::endl;
        return ;
    }

    std::string buffs;
    buffs.resize(4096);
    rt = sock->recv(&buffs[0], buffs.size());

    if (rt <= 0) {
        std::cout << "recv fail rt=" << rt << std::endl;
        return ;
    }

    buffs.resize(rt);
    std::cout << buffs << std::endl;
}

int main(int argc, char** argv) {
    sylar::IOManager iom;
    iom.schedule(&test_socket);
    return 0;
}