#include "http_server.h"
// #include "sylar/http/servlets/config_servlet.h"
// #include "sylar/http/servlets/status_servlet.h"

namespace sylar {
namespace http {

HttpServer::HttpServer(bool keepalive
                    ,sylar::IOManager* worker
                    ,sylar::IOManager* io_worker
                    ,sylar::IOManager* accept_worker)
    :TcpServer(worker, io_worker, accept_worker)
    ,m_isKeepAlive(keepalive) {
    m_dispatch.reset(new ServletDispatch);
    m_type = "http";
    // m_dispatch->addServlet("/_/status", Servlet::ptr(new StatusServlet));
    // m_dispatch->addServlet("/_/config", Servlet::ptr(new ConfigServlet));
}

void HttpServer::setName(const std::string& v) {
    TcpServer::setName(v);
    m_dispatch->setDefault(std::make_shared<NotFoundServlet>(v));
}

void HttpServer::handleClient(Socket::ptr client) {
    std::cout << "handleCilent" << *client << std::endl;
    HttpSession::ptr session(new HttpSession(client));
    do {
        auto req = session->recvRequest();
        if (!req) {
            std:cout << "recv http request fail, errno=" 
                << errno << " errstr=" << strerror(errno) 
                << " client:" << *client << " keep_alive=" << m_isKeepAlive
                <<std::endl; 
            break;
        }
        HttpResponse::ptr rsp(new HttpResponse(req->getVersion()
                        ,req->isClose() || !m_isKeepAlive));
        //rsp->setHeader("Server", getName());
        m_dispatch->handle(req, rsp, session);
        session->sendResponse(rsp);

        if (!m_isKeepAlive || req->isClose()) {
            break;
        }
    } while (true);
    session->close();
}

}
}