#include <memory>
#include <boost/asio.hpp>

#if !defined(ECHO_SERVER_HPP)
#define ECHO_SERVER_HPP

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
            [sock](boost::system::error_code const& ec) {
                if (!ec) {
                    do_read();
                }
            }
        );
    }
    void do_read() {
    }

    Executor exe_;
    as::ip::tcp::endpoint ep_;
    as::ip::tcp::acceptor ac_;
};

} // echo

#endif // ECHO_SERVER_HPP
