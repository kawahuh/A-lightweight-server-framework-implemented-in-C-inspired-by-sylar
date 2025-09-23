#include "sylar/sylar.h"
#include "sylar/iomanager.h"
#include "sylar/log.h"
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

void test_sleep() {
    sylar::IOManager iom(1);
    iom.schedule([](){
        sleep(2);
        loginfo2("sleep 2");
    });
    iom.schedule([](){
        sleep(3);
        loginfo2("sleep 3");
    });
    loginfo2("hook_test over");
}

void test_sock() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "183.2.172.17", &addr.sin_addr.s_addr);

    int rt = connect(sock, (const sockaddr*)&addr, sizeof(addr));
    std::cout << "connect rt=" << rt << " error=" << errno << std::endl;
    if (rt) {
        return ;
    }

    const char data[] = "GET / HTTP/1.0\r\n\r\n";
    rt = send(sock, data, sizeof(data), 0);
    std::cout << "send message, rt=" << rt << " errno=" << errno << std::endl;

    if (rt <= 0) {
        return ;
    }

    std::string buff;
    buff.resize(4096);

    rt = recv(sock, &buff[0], buff.size(), 0);
    std::cout << "recv rt=" << rt << " errno=" << errno << std::endl;

    if (rt <= 0) {
        return ;
    }

    buff.resize(rt);
    std::cout << buff << std::endl;
}

int main(int argc, char** argv) {
    sylar::IOManager iom;
    iom.schedule(&test_sock);
    // test_sock();
    return 0;
}