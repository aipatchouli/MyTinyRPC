#include "../include/interface.hpp"
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

int main() {
    
    boost::asio::io_service io_service;
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string("127.0.0.1"), 12345);
    boost::shared_ptr<client> Client(new client(io_service, endpoint));
    std::cout << "RPC ADD is " << Client->add(8, 9) << std::endl;

    return 0;
}