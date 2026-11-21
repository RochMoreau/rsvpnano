// esp_http_server over POSIX sockets, for the companion API on NextUI.
//
// Like ESP-IDF's httpd, one thread owns every socket and serves one request at a time, so handlers
// never run concurrently with each other or with queued work. Handlers still run concurrently with
// the firmware's main loop, exactly as on the ESP32.
#include <esp_http_server.h>
#include <esp_log.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

    constexpr const char* kTag = "httpd";
    constexpr size_t kMaxHeaderBytes = 16 * 1024;
    constexpr size_t kDrainLimitBytes = 64 * 1024;
    constexpr uint16_t kFallbackPort = 8080;

    constexpr const char* kErrorStatus[HTTPD_ERR_CODE_MAX] = {
        "500 Internal Server Error", "501 Method Not Implemented", "505 Version Not Supported", "400 Bad Request",
        "401 Unauthorized", "403 Forbidden", "404 Not Found", "405 Method Not Allowed", "408 Request Timeout",
        "411 Length Required", "414 URI Too Long", "431 Request Header Fields Too Large",
    };

    bool equalsIgnoreCase(std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }

    std::string_view trim(std::string_view value) {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
            value.remove_suffix(1);
        return value;
    }

    int methodFromName(std::string_view name) {
        if (name == "GET")
            return HTTP_GET;
        if (name == "POST")
            return HTTP_POST;
        if (name == "PUT")
            return HTTP_PUT;
        if (name == "DELETE")
            return HTTP_DELETE;
        if (name == "PATCH")
            return HTTP_PATCH;
        if (name == "OPTIONS")
            return HTTP_OPTIONS;
        if (name == "HEAD")
            return HTTP_HEAD;
        return -1;
    }

    struct Server;

    struct Connection {
        int fd = -1;
        std::string buffer; // Bytes read past the current request's headers.
        std::chrono::steady_clock::time_point lastUsed;
    };

    // Per-request state, reached from httpd_req_t::aux.
    struct Request {
        Server* server = nullptr;
        Connection* connection = nullptr;
        std::string uri;
        std::vector<std::pair<std::string, std::string>> headers;
        size_t bodyRemaining = 0;

        std::string status = "200 OK";
        std::string contentType = "text/html";
        std::vector<std::pair<std::string, std::string>> responseHeaders;
        bool headersSent = false;
        bool chunked = false;
        bool complete = false;
        bool closeAfter = false;
        bool sendFailed = false;

        const std::string* header(std::string_view name) const {
            for (const auto& [field, value]: headers) {
                if (equalsIgnoreCase(field, name))
                    return &value;
            }
            return nullptr;
        }
    };

    struct Server {
        httpd_config_t config{};
        int listenFd = -1;
        int wakeRead = -1;
        int wakeWrite = -1;
        uint16_t port = 0;
        std::thread thread;
        std::atomic<bool> stopping{false};
        std::vector<httpd_uri_t> routes;
        httpd_err_handler_func_t errorHandlers[HTTPD_ERR_CODE_MAX]{};
        std::mutex workMutex;
        std::deque<std::pair<httpd_work_fn_t, void*>> work;
        std::vector<Connection> connections;

        void wake() {
            const char byte = 1;
            [[maybe_unused]] const ssize_t written = ::write(wakeWrite, &byte, 1);
        }
    };

    Request* requestOf(httpd_req_t* r) {
        return r == nullptr ? nullptr : static_cast<Request*>(r->aux);
    }

    bool sendAll(Request& request, const char* data, size_t length) {
        if (request.sendFailed)
            return false;
        while (length > 0) {
            const ssize_t sent = ::send(request.connection->fd, data, length, MSG_NOSIGNAL);
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent <= 0) {
                request.sendFailed = true;
                return false;
            }
            data += sent;
            length -= static_cast<size_t>(sent);
        }
        return true;
    }

    bool sendHeaders(Request& request, std::optional<size_t> contentLength) {
        std::string head = "HTTP/1.1 " + request.status + "\r\n";
        bool hasContentType = false;
        for (const auto& [field, value]: request.responseHeaders) {
            if (equalsIgnoreCase(field, "Connection") && equalsIgnoreCase(value, "close"))
                request.closeAfter = true;
            if (equalsIgnoreCase(field, "Content-Type"))
                hasContentType = true;
            head += field + ": " + value + "\r\n";
        }
        if (!hasContentType && (!contentLength || *contentLength > 0))
            head += "Content-Type: " + request.contentType + "\r\n";
        if (contentLength) {
            head += "Content-Length: " + std::to_string(*contentLength) + "\r\n";
        } else {
            head += "Transfer-Encoding: chunked\r\n";
            request.chunked = true;
        }
        if (request.closeAfter)
            head += "Connection: close\r\n";
        head += "\r\n";
        request.headersSent = true;
        return sendAll(request, head.data(), head.size());
    }

    // Reads up to length bytes of the request body; HTTPD_SOCK_ERR_* on failure.
    int receiveBody(Request& request, char* destination, size_t length) {
        if (request.bodyRemaining == 0 || length == 0)
            return 0;
        length = std::min(length, request.bodyRemaining);
        Connection& connection = *request.connection;
        if (!connection.buffer.empty()) {
            const size_t count = std::min(length, connection.buffer.size());
            std::memcpy(destination, connection.buffer.data(), count);
            connection.buffer.erase(0, count);
            request.bodyRemaining -= count;
            return static_cast<int>(count);
        }
        for (;;) {
            const ssize_t received = ::recv(connection.fd, destination, length, 0);
            if (received < 0 && errno == EINTR)
                continue;
            if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return HTTPD_SOCK_ERR_TIMEOUT;
            if (received <= 0)
                return HTTPD_SOCK_ERR_FAIL;
            request.bodyRemaining -= static_cast<size_t>(received);
            return static_cast<int>(received);
        }
    }

    // Reads one request's headers into request; false if the connection should be closed.
    // errorCode is set when an error response should be sent first.
    bool readRequestHead(Server& server, Connection& connection, Request& request, int& method,
                         std::optional<httpd_err_code_t>& errorCode) {
        size_t headerEnd = std::string::npos;
        char chunk[4096];
        while ((headerEnd = connection.buffer.find("\r\n\r\n")) == std::string::npos) {
            if (connection.buffer.size() > kMaxHeaderBytes) {
                errorCode = HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE;
                return false;
            }
            const ssize_t received = ::recv(connection.fd, chunk, sizeof(chunk), 0);
            if (received < 0 && errno == EINTR)
                continue;
            if (received <= 0) {
                if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && !connection.buffer.empty())
                    errorCode = HTTPD_408_REQ_TIMEOUT;
                return false;
            }
            connection.buffer.append(chunk, static_cast<size_t>(received));
        }

        const std::string head = connection.buffer.substr(0, headerEnd);
        connection.buffer.erase(0, headerEnd + 4);

        size_t lineEnd = head.find("\r\n");
        const std::string_view requestLine = std::string_view{head}.substr(0, lineEnd);
        const size_t firstSpace = requestLine.find(' ');
        const size_t secondSpace = requestLine.rfind(' ');
        if (firstSpace == std::string_view::npos || secondSpace == firstSpace) {
            errorCode = HTTPD_400_BAD_REQUEST;
            return false;
        }
        if (!requestLine.substr(secondSpace + 1).starts_with("HTTP/1.")) {
            errorCode = HTTPD_505_VERSION_NOT_SUPPORTED;
            return false;
        }
        method = methodFromName(requestLine.substr(0, firstSpace));
        request.uri = std::string{requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1)};
        if (request.uri.size() > server.config.max_uri_len || request.uri.size() > HTTPD_MAX_URI_LEN) {
            errorCode = HTTPD_414_URI_TOO_LONG;
            return false;
        }

        while (lineEnd != std::string::npos) {
            const size_t start = lineEnd + 2;
            lineEnd = head.find("\r\n", start);
            const std::string_view line = std::string_view{head}.substr(start, lineEnd == std::string::npos
                                                                                     ? std::string::npos
                                                                                     : lineEnd - start);
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            request.headers.emplace_back(std::string{trim(line.substr(0, colon))},
                                         std::string{trim(line.substr(colon + 1))});
        }

        if (const std::string* encoding = request.header("Transfer-Encoding");
            encoding != nullptr && !equalsIgnoreCase(*encoding, "identity")) {
            errorCode = HTTPD_411_LENGTH_REQUIRED; // ESP-IDF does not accept chunked uploads either.
            return false;
        }
        if (const std::string* length = request.header("Content-Length")) {
            char* end = nullptr;
            const unsigned long long value = std::strtoull(length->c_str(), &end, 10);
            if (end == length->c_str() || *end != '\0') {
                errorCode = HTTPD_400_BAD_REQUEST;
                return false;
            }
            request.bodyRemaining = static_cast<size_t>(value);
        }
        if (const std::string* connectionHeader = request.header("Connection");
            connectionHeader != nullptr && equalsIgnoreCase(*connectionHeader, "close")) {
            request.closeAfter = true;
        }

        // A page served by this device posting back to it is same-origin, not a cross-origin browser
        // request: drop its Origin so the firmware's cross-origin allow-list does not apply.
        if (const std::string* host = request.header("Host")) {
            const std::string sameOrigin = "http://" + *host;
            std::erase_if(request.headers, [&](const auto& header) {
                return equalsIgnoreCase(header.first, "Origin") && header.second == sameOrigin;
            });
        }
        return true;
    }

    bool matches(const Server& server, const httpd_uri_t& route, const std::string& uri) {
        if (server.config.uri_match_fn != nullptr)
            return server.config.uri_match_fn(route.uri, uri.c_str(), uri.size());
        const std::string_view path = std::string_view{uri}.substr(0, uri.find('?'));
        return path == route.uri;
    }

    void sendDefaultError(httpd_req_t* r, httpd_err_code_t error) {
        httpd_resp_send_err(r, error, nullptr);
    }

    // Serves one request on the connection. Returns false when the connection must be closed.
    bool serveOne(Server& server, Connection& connection) {
        Request request;
        request.server = &server;
        request.connection = &connection;

        httpd_req_t req{};
        req.handle = &server;
        req.aux = &request;

        int method = -1;
        std::optional<httpd_err_code_t> errorCode;
        const bool parsed = readRequestHead(server, connection, request, method, errorCode);
        if (!parsed) {
            if (errorCode) {
                request.closeAfter = true;
                sendDefaultError(&req, *errorCode);
            }
            return false;
        }

        req.method = method;
        req.content_len = request.bodyRemaining;
        std::memcpy(const_cast<char*>(req.uri), request.uri.c_str(), request.uri.size() + 1);

        const httpd_uri_t* handler = nullptr;
        bool pathKnown = false;
        for (const httpd_uri_t& route: server.routes) {
            if (!matches(server, route, request.uri))
                continue;
            pathKnown = true;
            if (static_cast<int>(route.method) == method) {
                handler = &route;
                break;
            }
        }

        esp_err_t result = ESP_OK;
        if (handler != nullptr) {
            req.user_ctx = handler->user_ctx;
            result = handler->handler(&req);
        } else {
            const httpd_err_code_t error = pathKnown ? HTTPD_405_METHOD_NOT_ALLOWED : HTTPD_404_NOT_FOUND;
            if (httpd_err_handler_func_t custom = server.errorHandlers[error])
                result = custom(&req, error);
            else
                sendDefaultError(&req, error);
        }

        if (request.headersSent && request.chunked && !request.complete)
            request.closeAfter = true; // An unfinished chunked response cannot be followed by another.
        if (result != ESP_OK || request.sendFailed || request.closeAfter || !request.headersSent)
            return false;

        // Discard an unread body (ESP-IDF does the same) so the next request starts cleanly.
        if (request.bodyRemaining > kDrainLimitBytes)
            return false;
        char discard[4096];
        while (request.bodyRemaining > 0) {
            if (receiveBody(request, discard, sizeof(discard)) <= 0)
                return false;
        }
        return true;
    }

    void closeConnection(Connection& connection) {
        if (connection.fd >= 0)
            ::close(connection.fd);
        connection.fd = -1;
    }

    void runQueuedWork(Server& server) {
        for (;;) {
            std::pair<httpd_work_fn_t, void*> item;
            {
                const std::lock_guard lock{server.workMutex};
                if (server.work.empty())
                    return;
                item = server.work.front();
                server.work.pop_front();
            }
            item.first(item.second);
        }
    }

    void acceptConnection(Server& server) {
        const int fd = ::accept(server.listenFd, nullptr, nullptr);
        if (fd < 0)
            return;

        if (server.connections.size() >= server.config.max_open_sockets) {
            if (!server.config.lru_purge_enable) {
                ::close(fd);
                return;
            }
            auto oldest = std::min_element(server.connections.begin(), server.connections.end(),
                                           [](const Connection& a, const Connection& b) {
                                               return a.lastUsed < b.lastUsed;
                                           });
            closeConnection(*oldest);
            server.connections.erase(oldest);
        }

        timeval receiveTimeout{.tv_sec = std::max<int>(1, server.config.recv_wait_timeout), .tv_usec = 0};
        timeval sendTimeout{.tv_sec = std::max<int>(1, server.config.send_wait_timeout), .tv_usec = 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
        const int noDelay = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
        server.connections.push_back({.fd = fd, .buffer = {}, .lastUsed = std::chrono::steady_clock::now()});
    }

    void serverLoop(Server& server) {
        std::vector<pollfd> fds;
        while (!server.stopping.load()) {
            fds.clear();
            fds.push_back({.fd = server.wakeRead, .events = POLLIN, .revents = 0});
            fds.push_back({.fd = server.listenFd, .events = POLLIN, .revents = 0});
            for (const Connection& connection: server.connections)
                fds.push_back({.fd = connection.fd, .events = POLLIN, .revents = 0});

            if (::poll(fds.data(), fds.size(), 1000) < 0) {
                if (errno == EINTR)
                    continue;
                ESP_LOGE(kTag, "poll failed: %s", std::strerror(errno));
                break;
            }
            if (fds[0].revents & POLLIN) {
                char drain[64];
                while (::read(server.wakeRead, drain, sizeof(drain)) > 0) {
                }
            }
            runQueuedWork(server);
            if (server.stopping.load())
                break;

            // Serve readable connections. Indexes into fds line up with connections at poll time.
            const size_t polled = fds.size() - 2;
            for (size_t i = polled; i-- > 0;) {
                if ((fds[i + 2].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
                    continue;
                Connection& connection = server.connections[i];
                connection.lastUsed = std::chrono::steady_clock::now();
                bool keep = serveOne(server, connection);
                // Requests already received on a keep-alive connection are served before polling again.
                while (keep && connection.buffer.find("\r\n\r\n") != std::string::npos)
                    keep = serveOne(server, connection);
                if (!keep) {
                    closeConnection(connection);
                    server.connections.erase(server.connections.begin() + static_cast<std::ptrdiff_t>(i));
                }
                runQueuedWork(server);
            }

            if (fds[1].revents & POLLIN)
                acceptConnection(server);
        }

        for (Connection& connection: server.connections)
            closeConnection(connection);
        server.connections.clear();
        runQueuedWork(server);
    }

    int bindListener(uint16_t port, int backlog) {
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            return -1;
        const int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(fd, backlog) != 0) {
            const int error = errno;
            ::close(fd);
            errno = error;
            return -1;
        }
        return fd;
    }

} // namespace

esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config) {
    if (handle == nullptr || config == nullptr)
        return ESP_ERR_INVALID_ARG;

    auto* server = new Server;
    server->config = *config;
    server->port = config->server_port;
    server->listenFd = bindListener(server->port, std::max<int>(1, config->backlog_conn));
    if (server->listenFd < 0 && server->port == 80 && (errno == EACCES || errno == EADDRINUSE)) {
        ESP_LOGW(kTag, "port 80 unavailable (%s); using %u", std::strerror(errno), kFallbackPort);
        server->port = kFallbackPort;
        server->listenFd = bindListener(server->port, std::max<int>(1, config->backlog_conn));
    }
    if (server->listenFd < 0) {
        ESP_LOGE(kTag, "listen on port %u failed: %s", server->port, std::strerror(errno));
        delete server;
        return ESP_FAIL;
    }

    int pipeFds[2];
    if (::pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK) != 0) {
        ::close(server->listenFd);
        delete server;
        return ESP_FAIL;
    }
    server->wakeRead = pipeFds[0];
    server->wakeWrite = pipeFds[1];
    server->thread = std::thread([server] { serverLoop(*server); });

    ESP_LOGI(kTag, "listening on port %u", server->port);
    *handle = server;
    return ESP_OK;
}

esp_err_t httpd_stop(httpd_handle_t handle) {
    auto* server = static_cast<Server*>(handle);
    if (server == nullptr)
        return ESP_ERR_INVALID_ARG;
    server->stopping = true;
    server->wake();
    if (server->thread.joinable()) {
        if (server->thread.get_id() == std::this_thread::get_id())
            server->thread.detach();
        else
            server->thread.join();
    }
    ::close(server->listenFd);
    ::close(server->wakeRead);
    ::close(server->wakeWrite);
    if (server->config.global_user_ctx_free_fn != nullptr)
        server->config.global_user_ctx_free_fn(server->config.global_user_ctx);
    delete server;
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri_handler) {
    auto* server = static_cast<Server*>(handle);
    if (server == nullptr || uri_handler == nullptr || uri_handler->uri == nullptr)
        return ESP_ERR_INVALID_ARG;
    for (const httpd_uri_t& route: server->routes) {
        if (route.method == uri_handler->method && std::strcmp(route.uri, uri_handler->uri) == 0)
            return ESP_ERR_HTTPD_HANDLER_EXISTS;
    }
    // ESP-IDF stores the pointer it was given; route strings here are string literals.
    server->routes.push_back(*uri_handler);
    return ESP_OK;
}

esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t error, httpd_err_handler_func_t handler) {
    auto* server = static_cast<Server*>(handle);
    if (server == nullptr || error < 0 || error >= HTTPD_ERR_CODE_MAX)
        return ESP_ERR_INVALID_ARG;
    server->errorHandlers[error] = handler;
    return ESP_OK;
}

void* httpd_get_global_user_ctx(httpd_handle_t handle) {
    auto* server = static_cast<Server*>(handle);
    return server == nullptr ? nullptr : server->config.global_user_ctx;
}

esp_err_t httpd_queue_work(httpd_handle_t handle, httpd_work_fn_t work, void* arg) {
    auto* server = static_cast<Server*>(handle);
    if (server == nullptr || work == nullptr)
        return ESP_ERR_INVALID_ARG;
    if (server->stopping.load())
        return ESP_ERR_INVALID_STATE;
    {
        const std::lock_guard lock{server->workMutex};
        server->work.emplace_back(work, arg);
    }
    server->wake();
    return ESP_OK;
}

uint16_t nextui_httpd_port(httpd_handle_t handle) {
    auto* server = static_cast<Server*>(handle);
    return server == nullptr ? 0 : server->port;
}

int httpd_req_recv(httpd_req_t* r, char* buf, size_t buf_len) {
    Request* request = requestOf(r);
    if (request == nullptr || buf == nullptr)
        return HTTPD_SOCK_ERR_INVALID;
    return receiveBody(*request, buf, buf_len);
}

size_t httpd_req_get_hdr_value_len(httpd_req_t* r, const char* field) {
    Request* request = requestOf(r);
    if (request == nullptr || field == nullptr)
        return 0;
    const std::string* value = request->header(field);
    return value == nullptr ? 0 : value->size();
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* r, const char* field, char* val, size_t val_size) {
    Request* request = requestOf(r);
    if (request == nullptr || field == nullptr || val == nullptr || val_size == 0)
        return ESP_ERR_INVALID_ARG;
    const std::string* value = request->header(field);
    if (value == nullptr)
        return ESP_ERR_NOT_FOUND;
    const size_t count = std::min(value->size(), val_size - 1);
    std::memcpy(val, value->data(), count);
    val[count] = '\0';
    return count < value->size() ? ESP_ERR_HTTPD_RESULT_TRUNC : ESP_OK;
}

size_t httpd_req_get_url_query_len(httpd_req_t* r) {
    Request* request = requestOf(r);
    if (request == nullptr)
        return 0;
    const size_t question = request->uri.find('?');
    return question == std::string::npos ? 0 : request->uri.size() - question - 1;
}

esp_err_t httpd_req_get_url_query_str(httpd_req_t* r, char* buf, size_t buf_len) {
    Request* request = requestOf(r);
    if (request == nullptr || buf == nullptr || buf_len == 0)
        return ESP_ERR_INVALID_ARG;
    const size_t question = request->uri.find('?');
    if (question == std::string::npos)
        return ESP_ERR_NOT_FOUND;
    const std::string_view query = std::string_view{request->uri}.substr(question + 1);
    const size_t count = std::min(query.size(), buf_len - 1);
    std::memcpy(buf, query.data(), count);
    buf[count] = '\0';
    return count < query.size() ? ESP_ERR_HTTPD_RESULT_TRUNC : ESP_OK;
}

esp_err_t httpd_query_key_value(const char* qry, const char* key, char* val, size_t val_size) {
    if (qry == nullptr || key == nullptr || val == nullptr || val_size == 0)
        return ESP_ERR_INVALID_ARG;
    const std::string_view query{qry};
    const std::string_view wanted{key};
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string_view::npos)
            end = query.size();
        const std::string_view pair = query.substr(start, end - start);
        const size_t equals = pair.find('=');
        if (pair.substr(0, equals) == wanted) {
            const std::string_view value = equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1);
            const size_t count = std::min(value.size(), val_size - 1);
            std::memcpy(val, value.data(), count);
            val[count] = '\0';
            return count < value.size() ? ESP_ERR_HTTPD_RESULT_TRUNC : ESP_OK;
        }
        start = end + 1;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t httpd_resp_set_status(httpd_req_t* r, const char* status) {
    Request* request = requestOf(r);
    if (request == nullptr || status == nullptr)
        return ESP_ERR_INVALID_ARG;
    request->status = status;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t* r, const char* type) {
    Request* request = requestOf(r);
    if (request == nullptr || type == nullptr)
        return ESP_ERR_INVALID_ARG;
    request->contentType = type;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t* r, const char* field, const char* value) {
    Request* request = requestOf(r);
    if (request == nullptr || field == nullptr || value == nullptr)
        return ESP_ERR_INVALID_ARG;
    if (request->headersSent)
        return ESP_ERR_HTTPD_RESP_HDR;
    request->responseHeaders.emplace_back(field, value);
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t* r, const char* buf, ssize_t buf_len) {
    Request* request = requestOf(r);
    if (request == nullptr)
        return ESP_ERR_INVALID_ARG;
    if (request->headersSent)
        return ESP_ERR_HTTPD_RESP_SEND;
    const size_t length = buf == nullptr ? 0 : buf_len == HTTPD_RESP_USE_STRLEN ? std::strlen(buf) : static_cast<size_t>(buf_len);
    if (!sendHeaders(*request, length) || (length > 0 && !sendAll(*request, buf, length)))
        return ESP_ERR_HTTPD_RESP_SEND;
    request->complete = true;
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t* r, const char* buf, ssize_t buf_len) {
    Request* request = requestOf(r);
    if (request == nullptr)
        return ESP_ERR_INVALID_ARG;
    if (request->complete)
        return ESP_ERR_HTTPD_RESP_SEND;
    if (!request->headersSent && !sendHeaders(*request, std::nullopt))
        return ESP_ERR_HTTPD_RESP_SEND;
    const size_t length = buf == nullptr ? 0 : buf_len == HTTPD_RESP_USE_STRLEN ? std::strlen(buf) : static_cast<size_t>(buf_len);
    char size[24];
    const int sizeLength = std::snprintf(size, sizeof(size), "%zx\r\n", length);
    if (!sendAll(*request, size, static_cast<size_t>(sizeLength)))
        return ESP_ERR_HTTPD_RESP_SEND;
    if (length > 0 && !sendAll(*request, buf, length))
        return ESP_ERR_HTTPD_RESP_SEND;
    if (!sendAll(*request, "\r\n", 2))
        return ESP_ERR_HTTPD_RESP_SEND;
    if (length == 0)
        request->complete = true;
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t* req, httpd_err_code_t error, const char* msg) {
    Request* request = requestOf(req);
    if (request == nullptr || error < 0 || error >= HTTPD_ERR_CODE_MAX)
        return ESP_ERR_INVALID_ARG;
    request->status = kErrorStatus[error];
    request->contentType = "text/plain";
    request->closeAfter = request->closeAfter || error != HTTPD_404_NOT_FOUND;
    return httpd_resp_send(req, msg != nullptr ? msg : kErrorStatus[error], HTTPD_RESP_USE_STRLEN);
}
