#include "sylar/sylar.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>

void test_fiber() {
    loginfo2("test_fiber");
    // sleep(1);
    // sylar::Scheduler::GetThis()->schedule(&test_fiber);
    /////////
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "183.2.172.17", &addr.sin_addr.s_addr);

    if (!connect(sock, (const sockaddr*)&addr, sizeof(addr))) {
        loginfo2("connected immidiately");
    } else if (errno == EINPROGRESS) {
        loginfo2("connected successed");
        std::cout << "add event errno " << errno << std::endl;
        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::READ, []() {
            loginfo2("read back");
        });
        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::WRITE, [sock]() {
            loginfo2("write back");
            sylar::IOManager::GetThis()->cancelEvent(sock, sylar::IOManager::READ);
            close(sock);
        });
    } else {
        loginfo2("connected error");
    }
}

void test1() {
    sylar::IOManager iom(2, false);
    iom.schedule(&test_fiber);
}

void test_timer() {
    sylar::IOManager iom(2);
    iom.addTimer(500, [](){
        loginfo2("hello timer");
    });
}

int main(int argc, char **argv) {
    //test_timer();
    //test_fiber();
    test1();

    return 0;
}