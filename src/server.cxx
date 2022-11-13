// This is a tiny project for RPC
#include "../include/interface.hpp"
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>


auto main () -> int { 
    boost::asio::io_service io_service;
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 12345);
    server server(io_service, endpoint);
    server.run();
    return 0;
}