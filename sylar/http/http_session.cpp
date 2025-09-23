#include "http_session.h"
#include "http_parser.h"

namespace sylar {
namespace http {

HttpSession::HttpSession(Socket::ptr sock, bool owner)
        :SocketStream(sock, owner) {
}

HttpRequest::ptr HttpSession::recvRequest() {
    // 创建请求解析器
    HttpRequestParser::ptr parser(new HttpRequestParser);
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    std::shared_ptr<char> buffer(
            new char[buff_size], [](char* ptr) {
                delete[] ptr;
            });
    // 存放数据
    char* data = buffer.get();
    int offset = 0;
    // 循环读取和解析数据
    do {
        // 返回读取的长度
        int len = read(data + offset, buff_size - offset);
        if (len <= 0) {
            close();
            return nullptr;
        }
        len += offset;
        // 解析 len 长度的内容, 在 execute 中使用 memmove 将 data + offset 的内容移动到 data 处了
        size_t nparse = parser->execute(data, len);
        if (parser->hasError()) {
            close();
            return nullptr;
        }
        // 未解析的内容长度
        offset = len - nparse;
        // 缓冲区满了
        if (offset == (int)buff_size) {
            close();
            return nullptr;
        }
        if (parser->isFinished()) {
            break;
        }
    } while (true);
    int64_t length = parser->getContentLength();
    // 若解析完存在请求体的内容
    if (length > 0) {
        std::string body;
        body.resize(length);

        int len = 0;
        // 先复制已经读取的内容
        if (length >= offset) {
            memcpy(&body[0], data, offset);
            len = length;
        }
        length -= offset;
        // 再读取剩余的内容
        if (length > 0) {
            if (readFixSize(&body[len], length) > 0) {
                close();
                return nullptr;
            }
        }
        parser->getData()->setBody(body);
    }
    parser->getData()->init();
    return parser->getData();
}

int HttpSession::sendResponse(HttpResponse::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

}
}