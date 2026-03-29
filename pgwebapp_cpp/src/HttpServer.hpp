#pragma once
/*
 * HttpServer.hpp — Minimal HTTP/1.1 server (C++17, POSIX sockets)
 * Each connection handled in a detached std::thread.
 */

#include <string>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <sstream>

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ── HTTP request ─────────────────────────────────────────── */
struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

/* ── HTTP response ────────────────────────────────────────── */
struct HttpResponse {
    int         status  = 200;
    std::string content_type = "text/plain";
    std::string body;

    static HttpResponse json(int status, const std::string& body) {
        HttpResponse r;
        r.status       = status;
        r.content_type = "application/json; charset=utf-8";
        r.body         = body;
        return r;
    }
    static HttpResponse html(const std::string& body) {
        HttpResponse r;
        r.status       = 200;
        r.content_type = "text/html; charset=utf-8";
        r.body         = body;
        return r;
    }
    static HttpResponse not_found() {
        return json(404, R"({"error":"Not Found"})");
    }
};

/* ── Route handler type ───────────────────────────────────── */
using Handler = std::function<HttpResponse(const HttpRequest&)>;

/* ─────────────────────────────────────────────────────────── */
class HttpServer {
public:
    explicit HttpServer(int port) : port_(port), running_(false), srv_fd_(-1) {}

    ~HttpServer() { stop(); }

    /* Register a route: method + path → handler */
    void route(const std::string& method, const std::string& path, Handler h) {
        routes_[method + " " + path] = std::move(h);
    }

    /* Start listening — blocks until stop() is called */
    void listen_and_serve() {
        srv_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv_fd_ < 0) throw std::runtime_error("socket(): " + std::string(strerror(errno)));

        int opt = 1;
        ::setsockopt(srv_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(srv_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw std::runtime_error("bind(): " + std::string(strerror(errno)));
        if (::listen(srv_fd_, 16) < 0)
            throw std::runtime_error("listen(): " + std::string(strerror(errno)));

        running_ = true;

        while (running_) {
            sockaddr_in cli{};
            socklen_t cli_len = sizeof(cli);
            int cli_fd = ::accept(srv_fd_, reinterpret_cast<sockaddr*>(&cli), &cli_len);
            if (cli_fd < 0) {
                if (errno == EINTR || !running_) break;
                continue;
            }

            /* Detached thread per connection */
            std::thread([this, cli_fd]() {
                handle_connection(cli_fd);
                ::close(cli_fd);
            }).detach();
        }
    }

    void stop() {
        running_ = false;
        if (srv_fd_ >= 0) { ::close(srv_fd_); srv_fd_ = -1; }
    }

private:
    int  port_;
    std::atomic<bool> running_;
    int  srv_fd_;
    std::unordered_map<std::string, Handler> routes_;

    static constexpr size_t MAX_REQUEST = 256 * 1024;

    /* ── Read full HTTP request from socket ─────────────────── */
    static std::string read_request(int fd) {
        std::string buf;
        buf.reserve(4096);
        char chunk[4096];

        while (buf.size() < MAX_REQUEST) {
            ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            buf.append(chunk, static_cast<size_t>(n));

            auto hdr_end = buf.find("\r\n\r\n");
            if (hdr_end == std::string::npos) continue;

            /* Check Content-Length */
            auto cl_pos = buf.find("Content-Length: ");
            if (cl_pos != std::string::npos) {
                size_t cl_end   = buf.find("\r\n", cl_pos);
                size_t cl_val   = std::stoull(buf.substr(cl_pos + 16, cl_end - cl_pos - 16));
                size_t hdr_size = hdr_end + 4;
                if (buf.size() >= hdr_size + cl_val) break;
            } else {
                break;
            }
        }
        return buf;
    }

    /* ── Parse raw HTTP into HttpRequest ────────────────────── */
    static HttpRequest parse_request(const std::string& raw) {
        HttpRequest req;
        if (raw.empty()) return req;

        std::istringstream ss(raw);
        std::string line;
        std::getline(ss, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream req_line(line);
        req_line >> req.method >> req.path;

        /* Headers */
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            auto colon = line.find(": ");
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 2);
                req.headers[key] = val;
            }
        }

        /* Body */
        auto hdr_end = raw.find("\r\n\r\n");
        if (hdr_end != std::string::npos)
            req.body = raw.substr(hdr_end + 4);

        return req;
    }

    /* ── Serialize HttpResponse → raw HTTP ──────────────────── */
    static std::string build_response(const HttpResponse& resp) {
        const char* status_str =
            resp.status == 200 ? "OK" :
            resp.status == 404 ? "Not Found" :
            resp.status == 405 ? "Method Not Allowed" : "Bad Request";

        std::ostringstream out;
        out << "HTTP/1.1 " << resp.status << " " << status_str << "\r\n"
            << "Content-Type: " << resp.content_type << "\r\n"
            << "Content-Length: " << resp.body.size() << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << resp.body;
        return out.str();
    }

    /* ── Handle one connection ──────────────────────────────── */
    void handle_connection(int fd) {
        auto raw     = read_request(fd);
        auto req     = parse_request(raw);
        auto key     = req.method + " " + req.path;
        auto it      = routes_.find(key);
        HttpResponse resp = (it != routes_.end())
                          ? it->second(req)
                          : HttpResponse::not_found();

        auto wire = build_response(resp);
        ::send(fd, wire.data(), wire.size(), MSG_NOSIGNAL);
    }
};
