#if !defined(ECHO_SERVER_HPP)
#define ECHO_SERVER_HPP

#include <memory>
#include <iostream>
#include <boost/asio.hpp>

namespace echo {

namespace as = boost::asio;

template <typename Executor>
class server {
public:
    server(Executor exe, std::uint16_t port)
        :exe_{exe}, 
         ep_{as::ip::address::from_string("127.0.0.1"), port},
         ac_{exe_, ep_}
    {
        do_accept(); 
    }
    void close() {
        ac_.close();
    }

private:
    void do_accept() {
        auto sock = std::make_shared<as::ip::tcp::socket>(exe_);
        ac_.async_accept(
            *sock,
            [this, sock](boost::system::error_code const& ec) {
                std::cout << "[server] aync_accept:" << ec.message() << std::endl;
                if (ec) return;
                do_read(sock);
            }
        );
    }
    void do_read(auto sock) {
        auto rbuf = std::make_shared<std::string>();
        rbuf->resize(1024);
        sock->async_read_some(
            as::buffer(*rbuf),
            [this, rbuf, sock]
            (boost::system::error_code const& ec, std::size_t size) {
                std::cout << "[server] aync_read_some:" << ec.message() << std::endl;
                if (ec) return;
                rbuf->resize(size);
                sock->async_write_some(
                    as::buffer(*rbuf),
                    [this, rbuf, sock]
                    (boost::system::error_code const& ec, std::size_t size) {
                        std::cout << "[server] aync_write_some:" << ec.message() << std::endl;
                        if (ec) return;
                        do_read(sock);
                    }
                );
            }
        );
    }

    Executor exe_;
    as::ip::tcp::endpoint ep_;
    as::ip::tcp::acceptor ac_;
};

} // echo

#endif // ECHO_SERVER_HPP
