#if !defined(ECHO_SERVER_HPP)
#define ECHO_SERVER_HPP

#include <memory>
#include <iostream>
#include <boost/asio.hpp>

namespace echo {

namespace as = boost::asio;

template <typename Executor>
as::awaitable<void>
server_impl(Executor exe, std::uint16_t port) {
    try {
        as::ip::tcp::endpoint ep{
            as::ip::address::from_string("127.0.0.1"), 
            port
        };
        as::ip::tcp::acceptor ac{exe, ep};
        as::ip::tcp::socket s{exe};
        co_await ac.async_accept(
            s,
            as::use_awaitable
        );
        std::cout << "[server] " << "async_accept success" << std::endl;

        while (true) {
            std::string buf;
            buf.resize(1024);
            auto size = co_await s.async_read_some(
                as::buffer(buf),
                as::use_awaitable
            );
            std::cout << "[server] " << "async_read success" << std::endl;
            buf.resize(size);
            co_await s.async_write_some(
                as::buffer(buf),
                as::use_awaitable
            );
            std::cout << "[server] " << "async_write success" << std::endl;
        }
    }
    catch (boost::system::system_error const& se) {
        std::cout << "[server] " << se.code().message() << std::endl;
    }
}

template <typename Executor>
void server(Executor exe, std::uint16_t port) {
    as::co_spawn(exe, server_impl(exe, port), as::detached);
}

} // echo

#endif // ECHO_SERVER_HPP
