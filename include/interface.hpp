/* #ifndef INTERFACE_HPP
#define INTERFACE_HPP

#include <array>
#include <boost/asio.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <cstring>
#include <iostream>
#include <memory>
#include <msgpack.hpp>
#include <sstream>
#include <string>
#include <tuple>

enum {
    NOTAPPLICATED = -3000,
    MAXPACKSIZE = 1024,
};

enum { ADD = 1,
       MINUS = 2,
       MULTI = 3,
       DIV = 4
};

// 客户端发送给服务端的报文格式：4 字节的整数代表函数映射表，4 字节的整数表示
// msgpack 的长度，后面不定长的部分为 msgpack 包 服务端发送给客户端的报文格式：1
// 个整数序列化之后的 msgpack 包

// 客户端

class Client : public boost::enable_shared_from_this<Client> {
public:
    using address = boost::asio::ip::address;
    using tcp = boost::asio::ip::tcp;
    Client(boost::asio::io_service& io_service, tcp::endpoint& endpoint)
        : io_service_(io_service), socket_(io_service), endpoint_(endpoint) {
        buffer = std::make_shared<std::array<char, MAXPACKSIZE>>();
    }

    int add(int a, int b) {
        return start(ADD, a, b);
    }

    int minus(int a, int b) {
        return start(MINUS, a, b);
    }

    int multi(int a, int b) {
        return start(MULTI, a, b);
    }

    int div(int a, int b) {
        return start(DIV, a, b);
    }

private:
    boost::asio::io_service& io_service_;
    tcp::socket socket_;
    tcp::endpoint endpoint_;
    std::shared_ptr<std::array<char, MAXPACKSIZE>> buffer;
    int result{0};

    int start(int tag, int a, int b) { // 开始 RPC
        boost::system::error_code ec;
        socket_.connect(endpoint_, ec); // 连接服务器
        if (!ec) {
            static tcp::no_delay option(true);
            socket_.set_option(option); // 设置 socket 为无延时 socket

            construct_rpc_data(tag, a, b); // 构造 RPC 包，并将包存入 buffer 成员变量中
            send_recive_rpc_data(ec);      // 发送需求包，并且接受客户端的结果

            std::cout << "send_recive_rpc_data return value: " << result << std::endl;
            return result; // 将结果返回
        }

        std::cerr << boost::system::system_error(ec).what() << std::endl;

        return NOTAPPLICATED;
    }

    void construct_rpc_data(size_t tag, int a, int b) {
        std::cout << " tag " << tag << std::endl; // 根据协议首先需要传输的是函数映射
        size_t tag_bigend = boost::asio::detail::socket_ops::host_to_network_long(tag);
        memcpy(buffer->data(), &tag_bigend, 4); // 将函数映射存储在 buffer 的最前面

        std::tuple<int, int> src(a, b); // 将需要发送给服务端的参数封装在 truple 里面
        std::stringstream sbuffer;
        msgpack::pack(sbuffer, src);       // 序列化
        std::string strbuf(sbuffer.str()); // 转化为 std::string 类型方便传输

        std::cout << " len " << strbuf.size() << std::endl;
        size_t len_bigend = boost::asio::detail::socket_ops::host_to_network_long(strbuf.size());
        memcpy(buffer->data() + 4, &len_bigend, 4);               // 将 msgpack 包长度写到函数映射之后
        memcpy(buffer->data() + 8, strbuf.data(), strbuf.size()); // 将 msgpack 包写入 buffer
    }

    void send_recive_rpc_data(const boost::system::error_code& error) {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;
        // 异步将信息发送给服务端
        boost::asio::async_write(socket_, boost::asio::buffer(*async_buffer, MAXPACKSIZE), [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
            recive_rpc_data(ec); // 当执行这一句的时候，数据已经发送给服务端了，调用本函数来接受服务端的数据
            io_service_.stop();
        });
        io_service_.run();
    }

    void recive_rpc_data(const boost::system::error_code& error) {
        std::cout << "sending finished, start receiving data" << std::endl;
        auto self = this->shared_from_this();
        auto async_buffer = buffer;
        // 异步读取数据
        boost::asio::async_read(socket_, boost::asio::buffer(*async_buffer, async_buffer->size()), [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
            std::cout << "data reading completed" << std::endl;
            handle_rpc_data(ec); // 处理数据
            io_service_.stop();  // 手动停止事件循环
        });
        io_service_.run();
    }

    void handle_rpc_data(const boost::system::error_code& error) {
        std::cout << "data read: " << buffer->data() << std::endl;
        msgpack::object_handle msg = msgpack::unpack(buffer->data(), buffer->size());
        auto tp = msg.get().as<std::tuple<int>>();
        std::cout << " msgpack " << std::get<0>(tp) << std::endl;
        result = std::get<0>(tp); // 将结果写入成员变量 result 里面
    }
};

// 服务端::会话流控制
class Session : public std::enable_shared_from_this<Session> {
public:
    using address = boost::asio::ip::address;
    using tcp = boost::asio::ip::tcp;
    Session(boost::asio::io_service& io_service)
        : io_service_(io_service), socket_(io_service) {
        buffer = std::make_shared<std::array<char, MAXPACKSIZE>>();
        len_[0] = '\0';
        tag_[0] = '\0';
    }

    void start() {
        static tcp::no_delay option(true);
        socket_.set_option(option); // 设置 socket 为无延时 socket
        start_chains();             // 获取函数映射 -> 获取 msgpack 包长度 -> 获取 msgpack 包 -> 循环获取函数映射 ->
    }

    tcp::socket& socket() {
        return socket_;
    }

private:
    boost::asio::io_service& io_service_;
    tcp::socket socket_;
    boost::asio::streambuf sbuf_;
    std::shared_ptr<std::array<char, MAXPACKSIZE>> buffer;
    std::array<char, 4> len_{};
    std::array<char, 4> tag_{};
    // char len_[4];
    // char opt_[4];
    int len{};
    int tag{};
    msgpack::object_handle msg;

    void start_chains() {
        read_tag();
    }

    // 获取函数映射
    void read_tag() {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;
        boost::asio::async_read(socket_, boost::asio::buffer(tag_, 4), [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
            if (ec != nullptr) {
                std::cout << "read tag error" << std::endl;
                return;
            }
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " tag data reveived " << std::endl;
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " tag data " << tag_[0] << tag_[1] << tag_[2] << tag_[3] << std::endl;

            tag = boost::asio::detail::socket_ops::network_to_host_long(*reinterpret_cast<int*>(tag_.data()));
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " tag " << tag << std::endl;

            read_msgpack_len();
        });
    }

    // 获取 msgpack 包长度
    void read_msgpack_len() {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;
        boost::asio::async_read(socket_, boost::asio::buffer(len_, 4), [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
            if (ec != nullptr) {
                std::cout << "read len error" << std::endl;
                return;
            }
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " len data reveived " << std::endl;
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " len data " << len_[0] << len_[1] << len_[2] << len_[3] << std::endl;

            len = boost::asio::detail::socket_ops::network_to_host_long(*reinterpret_cast<int*>(len_.data()));
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " len " << len << std::endl;

            read_msgpack();
        });
    }

    // 获取 msgpack 包
    void read_msgpack() {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        boost::asio::async_read(socket_, boost::asio::buffer(*async_buffer, len), [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
            if (ec != nullptr) {
                std::cout << "read msgpack error" << std::endl;
                return;
            }
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " msgpack data reveived " << std::endl;
            // std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " msgpack data " << async_buffer->data() << std::endl;

            msg = msgpack::unpack(async_buffer->data(), len);
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " msgpack data unpacked " << std::endl;

            handle_msgpack();
        });
    }

    // 处理 msgpack 包
    void handle_msgpack() {
        std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " enter rpc_caculate_return " << std::endl;
        auto tp = msg.get().as<std::tuple<int, int>>();
        std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " handle msgpack " << std::get<0>(tp) << " " << std::get<1>(tp) << std::endl;

        int result = 0;
        switch (tag) {
        case ADD:
            result = std::get<0>(tp) + std::get<1>(tp);
            break;
        case MINUS:
            result = std::get<0>(tp) - std::get<1>(tp);
            break;
        case MULTI:
            result = std::get<0>(tp) * std::get<1>(tp);
            break;
        case DIV:
            if (std::get<1>(tp) == 0) {
                std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " divide by zero " << std::endl;
                result = NOTAPPLICATED;
            }
            result = std::get<0>(tp) / std::get<1>(tp);
            break;
        default:
            break;
        }
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        std::tuple<int> src(result);
        std::stringstream sbuffer;
        msgpack::pack(sbuffer, src);
        std::string strbuff = sbuffer.str();
        memcpy(async_buffer->data(), strbuff.data(), strbuff.size());
        std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " rpc calculation completed " << std::endl;

        boost::asio::async_write(socket_, boost::asio::buffer(*async_buffer, strbuff.size()), [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
            if (ec != nullptr) {
                std::cout << "write msgpack error" << std::endl;
                return;
            }
            std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " msgpack data sent " << std::endl;
            // std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " msgpack data " << async_buffer->data() << std::endl;
        });
    }
};

using Session_ptr = std::shared_ptr<Session>;

// 服务器 管理链接
class Server {
public:
    using address = boost::asio::ip::address;
    using tcp = boost::asio::ip::tcp;
    Server(boost::asio::io_service& io_service, tcp::endpoint& endpoint)
        : io_service_(io_service), acceptor_(io_service, endpoint) {
        Session_ptr new_session = std::make_shared<Session>(io_service_);
        acceptor_.async_accept(new_session->socket(), boost::bind(&Server::handle_accept, this, new_session, boost::asio::placeholders::error));
    }

    void handle_accept(Session_ptr session, const boost::system::error_code& error) {
        if (!error) {
            session->start();
        }
        session.reset(new Session(io_service_));
        acceptor_.async_accept(session->socket(), boost::bind(&Server::handle_accept, this, session, boost::asio::placeholders::error));
        io_service_.run();
    }

    void run() {
        io_service_.run();
    }

private:
    boost::asio::io_service& io_service_;
    tcp::acceptor acceptor_;
};

#endif // INTERFACE_HPP */

#ifndef __INTERFACE_HPP__
#define __INTERFACE_HPP__

// 与 asio 和 msgpack 相关的头文件
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <boost/asio/streambuf.hpp>

#include <boost/asio/placeholders.hpp>
#include <boost/asio.hpp>
using boost::asio::ip::tcp;
using boost::asio::ip::address;
#include <msgpack.hpp>

#include <string>
#include <iostream>

// 双方需要约定的特殊常量
#define NOTAPPLICATED -3000
#define MAXPACKSIZE 1024

// 函数映射表
#define ADD 1
#define MINUS 2
#define MULTI 3
#define DIV 4

// 客户端发送给服务端的报文格式：4 字节的整数代表函数映射表，4 字节的整数表示 msgpack 的长度，后面不定长的部分为 msgpack 包。
// 服务端发送给客户端的报文格式：1 个整数序列化之后的 msgpack 包

// 客户端类
class client : public boost::enable_shared_from_this<client> {
public:
    client(boost::asio::io_service& io_service, tcp::endpoint& endpoint)
        : io_service_(io_service), socket_(io_service), endpoint_(endpoint) {
        buffer = std::make_shared<std::array<char, MAXPACKSIZE>>(); // 初始化 buffer
        result = 0;
    }
    int add(int a, int b) // 调用 add 和调用 start 的区别在于少传入一个代表函数映射的参数 opt
    {
        return start(ADD, a, b);
    }
    int minus(int a, int b) {
        return start(MINUS, a, b);
    }
    int multi(int a, int b) {
        return start(MULTI, a, b);
    }
    int div(int a, int b) {
        return start(DIV, a, b);
    }
    int start(int opt, int a, int b) { // 开始 RPC
        boost::system::error_code ec;
        socket_.connect(endpoint_, ec); // 连接服务器
        if (!ec) {
            static tcp::no_delay option(true);
            socket_.set_option(option); // 设置 socket 为无延时 socket

            construct_rpc_data(opt, a, b); // 构造 RPC 包，并将包存入 buffer 成员变量中
            send_recive_rpc_data(ec);      // 发送需求包，并且接受客户端的结果
            std::cout << "send_recive_rpc_data return value: " << result << std::endl;
            return result; // 将结果返回
        }

        else {
            std::cerr << boost::system::system_error(ec).what() << std::endl;
        }
        return NOTAPPLICATED;
    }

private:
    void construct_rpc_data(size_t opt, int a, int b) {
        std::cout << " opt " << opt << std::endl; // 根据协议首先需要传输的是函数映射
        size_t opt_bigend = boost::asio::detail::socket_ops::host_to_network_long(opt);
        memcpy(buffer->data(), &opt_bigend, 4); // 将函数映射存储在 buffer 的最前面

        std::tuple<int, int> src(a, b); // 将需要发送给服务端的参数封装在 truple 里面
        std::stringstream sbuffer;
        msgpack::pack(sbuffer, src);       // 序列化
        std::string strbuf(sbuffer.str()); // 转化为 std::string 类型方便传输

        std::cout << "len " << strbuf.size() << std::endl;
        size_t len_bigend = boost::asio::detail::socket_ops::host_to_network_long(strbuf.size());
        memcpy(buffer->data() + 4, &len_bigend, 4);               // 将 msgpack 包长度写到函数映射之后
        memcpy(buffer->data() + 8, strbuf.data(), strbuf.size()); // 将 msgpack 包写入 buffer
    }
    void send_recive_rpc_data(const boost::system::error_code& error) {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        boost::asio::async_write(socket_, boost::asio::buffer(*async_buffer, MAXPACKSIZE), // 异步将信息发送给服务端
                                 [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
                                     recive_rpc_data(ec); // 当执行这一句的时候，数据以及发送给服务端了，调用本函数来接受服务端的数据

                                     io_service_.stop();
                                 });
        io_service_.run();
    }

    void recive_rpc_data(const boost::system::error_code& error) {
        std::cout << "sending finished, start receiving data" << std::endl;
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        boost::asio::async_read(socket_, boost::asio::buffer(*async_buffer, async_buffer->size()), // 异步读取数据
                                [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
                                    std::cout << "data reading completed" << std::endl;
                                    handle_rpc_data(ec);
                                    io_service_.stop(); // 手动停止事件循环
                                });
        io_service_.run();
    }

    void handle_rpc_data(const boost::system::error_code& error) {
        // std::cout << "data read: " << buffer->data() << std::endl;
        msgpack::object_handle msg = msgpack::unpack(buffer->data(), buffer->size());
        auto tp = msg.get().as<std::tuple<int>>();
        std::cout << "msgpack " << std::get<0>(tp) << std::endl;
        result = std::get<0>(tp); // 将结果写入成员变量 result 里面
    }

private:
    boost::asio::io_service& io_service_;
    tcp::socket socket_;
    tcp::endpoint& endpoint_;
    std::shared_ptr<std::array<char, MAXPACKSIZE>> buffer;
    int result;
};

// 服务端类

class session
    : public boost::enable_shared_from_this<session> {
public:
    session(boost::asio::io_service& io_service)
        : io_service_(io_service), socket_(io_service) {
        buffer = std::make_shared<std::array<char, MAXPACKSIZE>>(); // 初始化 buffer
        *len_ = '\0';
        *opt_ = '\0';
    }

    void start() {
        static tcp::no_delay option(true); // 设置 socket 为无时延模式
        socket_.set_option(option);
        start_chains(); // 开始 读函数映射 -> 读 msgpack 长度 -> 读 msgpack -> 读函数映射 的循环
    }

    tcp::socket& socket() {
        return socket_;
    }

private:
    void start_chains() {
        read_opt();
    }
    void read_opt() // 读函数映射
    {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        boost::asio::async_read(socket_, boost::asio::buffer(opt_, 4),
                                [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " opt data received" << std::endl;
                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " original data " << opt_ << std::endl;

                                    opt = boost::asio::detail::socket_ops::network_to_host_long(int(*(int*)opt_));
                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " opt " << opt << std::endl;

                                    read_msgpack_len();
                                });
    }

    void read_msgpack_len() // 读 msgpack 长度
    {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        boost::asio::async_read(socket_, boost::asio::buffer(len_, 4),
                                [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " len data received" << std::endl;
                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " original data " << len_ << std::endl;

                                    len = boost::asio::detail::socket_ops::network_to_host_long(int(*(int*)len_));
                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << "len " << opt << std::endl;

                                    read_msgpack();
                                });
        // io_service_.run();
    }

    void read_msgpack() // 读 msgpack 包
    {
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        boost::asio::async_read(socket_, boost::asio::buffer(async_buffer->data(), len),
                                [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
                                    if (ec) {
                                        std::cout << ec.message() << std::endl;
                                        return;
                                    }

                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " magpack data received" << std::endl;
                                    std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " original data " << async_buffer->data() << std::endl;

                                    msg = msgpack::unpack(async_buffer->data(), len);

                                    rpc_caculate_return();
                                });

        // io_service_.run();
    }

    void rpc_caculate_return() // 将计算结果返回
    {
        std::cout << "enter rpc_caculate_return" << std::endl;
        auto tp = msg.get().as<std::tuple<int, int>>();
        std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " magpack " << std::get<0>(tp) << " " << std::get<1>(tp) << std::endl;

        int result = 0;
        switch (opt) {
        case ADD:
            result = std::get<0>(tp) + std::get<1>(tp);
            break;

        case MINUS:
            result = std::get<0>(tp) - std::get<1>(tp);
            break;

        case MULTI:
            result = std::get<0>(tp) * std::get<1>(tp);
            break;
        case DIV:
            if (std::get<1>(tp) == 0)
                result = NOTAPPLICATED;
            result = std::get<0>(tp) / std::get<1>(tp);
            break;
        }
        auto self = this->shared_from_this();
        auto async_buffer = buffer;

        std::tuple<int> src(result);
        std::stringstream sbuffer;
        msgpack::pack(sbuffer, src);

        std::string strbuff(sbuffer.str());

        memcpy(async_buffer->data(), strbuff.data(), strbuff.size());

        std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " rpc calculation completed" << std::endl;
        boost::asio::async_write(socket_, boost::asio::buffer(async_buffer->data(), async_buffer->size()), //
                                 [this, self, async_buffer](const boost::system::error_code& ec, std::size_t size) {
                                     if (ec) {
                                         std::cout << ec.message() << std::endl;
                                         return;
                                     }
                                     std::cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << " rpc success" << std::endl;

                                     // io_service_.stop();
                                 });
        // io_service_.run();
    }

private:
    boost::asio::io_service& io_service_;
    tcp::socket socket_;
    boost::asio::streambuf sbuf_;
    std::shared_ptr<std::array<char, MAXPACKSIZE>> buffer;
    char len_[4];
    int len;
    char opt_[4];
    int opt;
    msgpack::object_handle msg;
};

typedef boost::shared_ptr<session> session_ptr;

class server {
public:
    server(boost::asio::io_service& io_service, tcp::endpoint& endpoint)
        : io_service_(io_service), acceptor_(io_service, endpoint) {
        session_ptr new_session(new session(io_service_));
        acceptor_.async_accept(new_session->socket(),              // 异步接受连接
                               boost::bind(&server::handle_accept, // 若有连接进入就调用成员函数 handle_accept()
                                           this,
                                           new_session,
                                           boost::asio::placeholders::error));
    }

    void handle_accept(session_ptr new_session, const boost::system::error_code& error) {
        if (error) {
            return;
        }

        new_session->start(); // 处理本次连接

        new_session.reset(new session(io_service_)); // 重置 io_service
        acceptor_.async_accept(new_session->socket(), boost::bind(&server::handle_accept, this, new_session,
                                                                  boost::asio::placeholders::error)); // 异步接受连接

        io_service_.run();
    }

    void run() {
        io_service_.run();
    }

private:
    boost::asio::io_service& io_service_;
    tcp::acceptor acceptor_;
};
#endif