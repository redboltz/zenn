---
title: "async_writeを連続で呼び出す場合のキューイング"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,async]
published: false
---

Boost.Asioには、非同期送信用の関数がいくつかああります。いずれも、以前に非同期送信関数を呼び出している場合、その完了、すなわちCompletionTokenがinvokeされるまで、次の非同期送信関数を呼び出してはならないというルールがあります。


```cpp
#include <https://raw.githubusercontent.com/redboltz/zenn/main/src/echo_server.hpp>

#include <iostream>
#include <boost/asio.hpp>

namespace as = boost::asio;

// クライアントコード
template <typename Executor>
as::awaitable<void>
app(Executor exe) {
    try {
        std::uint16_t port = 12345;
        auto ep = as::ip::tcp::endpoint(as::ip::make_address("127.0.0.1"), port);
        as::ip::tcp::socket sock{exe};
        std::string buf;
        buf.resize(1024);

        co_await sock.async_connect(
            ep,
            as::use_awaitable
        );
        std::string str1{"ABC"};
        auto s1 = sock.async_write_some(
            as::buffer(str1),
            as::use_awaitable
        );
        auto send_size1 = co_await std::move(s1);
        std::cout << "send_size1:" << send_size1 << std::endl;

        std::string str2{"DEF"};
        auto s2 = sock.async_write_some(
            as::buffer(str2),
            as::use_awaitable
        );
        auto send_size2 = co_await std::move(s2);
        std::cout << "send_size2:" << send_size2 << std::endl;

        {
            auto recv_size = co_await sock.async_read_some(
                as::buffer(buf),
                as::use_awaitable
            );
            std::cout << "recv_size:" << recv_size << std::endl;
            std::cout << buf << std::endl;
        }
        {
            auto recv_size = co_await sock.async_read_some(
                as::buffer(buf),
                as::use_awaitable
            );
            std::cout << "recv_size:" << recv_size << std::endl;
            std::cout << buf << std::endl;
        }
        
        sock.close();
    }
    catch (boost::system::system_error const& se) {
        std::cout << se.what() << std::endl;
    }
}


int main() {
    as::io_context ioc;
    
    // TCPのechoサーバを作成 実装はヘッダファイル
    echo::server s{ioc.get_executor(), 12345};

    as::co_spawn(ioc, app(ioc.get_executor()), as::detached);
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/6KcM46sTE