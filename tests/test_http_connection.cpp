#include <iostream>
#include "sylar/http/http_connection.h"

void test_pool() {
    sylar::http::HttpConnectionPool::ptr pool(new sylar::http::HttpConnectionPool(
            "www.dm.com", "", 80, 10, 1000*30, 5));
    sylar::IOManager::GetThis()->addTimer(1000, [pool](){
        auto r = pool->doGet("/", 3000);
        std::cout << r->toString() << std::endl;
    }, true);
}

void run() {
    sylar::Address::ptr addr = sylar::Address::LookupAnyIPAddress("www.baidu.com:80");
    if (!addr) {
        std::cout << "get addr error";
        return ;
    }

    sylar::Socket::ptr sock = sylar::Socket::CreateTCP(addr);
    bool rt = sock->connect(addr);
    if (!rt) {
        std::cout << "connect " << addr->toString() << " failed" << std::endl;
        return ;
    }

    sylar::http::HttpConnection::ptr conn(new sylar::http::HttpConnection(sock));
    sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest);
    req->setHeader("host", "www.baidu.com");
    std::cout << "req:" << req->toString() << std::endl;
    
    conn->sendRequest(req);
    auto rsp = conn->recvResponse();

    if (!rsp) {
        std::cout << "recv response error";
        return ;
    }
    std::cout << "rsp:" << rsp->toString() << std::endl;
    test_pool();
}

int main() {
    sylar::IOManager iom(2);
    iom.schedule(run);
    return 0;
}