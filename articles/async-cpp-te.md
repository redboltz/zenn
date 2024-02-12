---
title: "CompletionTokenと型消去"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async.co_composed]
published: false
---

# 型消去の目的
今回、型消去を、「同じように振る舞う型群を統一的に扱えるようにする」という目的で使用します。
例えば、チャットサーバなどのアプリケーションを考えたとき、クライアントとして、TCP接続、TLS接続、WebSocket接続など、様々な接続方式が考えられます。これらを統一的に扱うことで、誰かの書き込んだメッセージを、他のクライアント群に配信するようなコードが書きやすくなります。
今回の例では、tcp, tlsという型と、それらを統一的に扱う、connectionという型を例に説明します。

## オブジェクト指向アプローチとその限界

### Code

```cpp
#include <iostream>
#include <chrono>
#include <string>
#include <memory>
#include <vector>
#include <boost/asio.hpp>

namespace as = boost::asio;

struct connection {
    virtual void async_send(
        std::string data, 
        std::function<void(boost::system::error_code)> finish
    ) = 0;
    virtual ~connection() = default;
};

struct tcp : connection {
    tcp(as::any_io_executor exe):exe_{std::move(exe)} {}
    void async_send(
        std::string data, 
        std::function<void(boost::system::error_code)> finish
    ) override {
        as::post(
            exe_,
            [this,data = std::move(data),finish = std::move(finish)] {
                std::cout 
                    << "tcp data:" << data 
                    << " send finish" << std::endl;
                finish(boost::system::error_code{});
            }
        );
    }
    as::any_io_executor exe_;
};

struct tls : connection {
    tls(as::any_io_executor exe):exe_{std::move(exe)} {}
    void async_send(
        std::string data, 
        std::function<void(boost::system::error_code)> finish
    ) override {
        as::post(
            exe_,
            [data = std::move(data),finish = std::move(finish)] {
                std::cout 
                    << "tls data:" << data 
                    << " send finish" << std::endl;
                finish(boost::system::error_code{});
            }
        );
    }
    as::any_io_executor exe_;
};


int main() {
    as::io_context ioc;
    std::vector<std::shared_ptr<connection>> v;
    v.emplace_back(std::make_shared<tcp>(ioc.get_executor()));
    v.emplace_back(std::make_shared<tls>(ioc.get_executor()));
    for (auto& e : v) {
        e->async_send(
            "hello", 
            [](auto ec) { 
                std::cout << "finish ec:" << ec.message() << std::endl; 
            }
        );
    }
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/qEKK8r4o7

### 問題点
クラスconnectionで、純粋仮想関数としてasync_sendを宣言しています。戻り値の型はvoid、第1引数はデータ、第2引数は完了コールバック関数です。今回、コールバック関数だけではなく、Boost.Asioがサポートする一連のCompletionTokenをサポートしたいわけですが、仮想関数ではそれができません。

```cpp
struct connection {
    template <typename CompletionToken>
    virtual auto async_send(
        std::string data, 
        CompletionToken&& token
    ) = 0;
    virtual ~connection() = default;
};
```

godboltでの実行:
https://godbolt.org/z/o6cWjfqrq

この段階でエラーとなります。仮想関数は、関数テンプレート引数や、autoの戻り値といった、推論によって導かれる要素を持つことができないためです。

## variantによるアプローチ

### Code

```cpp
#include <iostream>
#include <chrono>
#include <string>
#include <variant>
#include <vector>
#include <boost/asio.hpp>

namespace as = boost::asio;

struct tcp {
    tcp(as::any_io_executor exe):exe_{std::move(exe)} {}
    template <typename CompletionToken>
    auto async_send(
        std::string data, 
        CompletionToken&& token
    ) {
        return as::async_initiate<
            CompletionToken,
            void(boost::system::error_code)
        >(
            [](
                auto completion_handler,
                as::any_io_executor& exe, 
                std::string data
            ) {
                as::post(
                    exe,
                    [
                        data = std::move(data), 
                        completion_handler = std::move(completion_handler)
                    ] () mutable {
                        std::cout 
                            << "tcp data:" << data 
                            << " send finish" << std::endl;
                        std::move(completion_handler)(boost::system::error_code{});
                    }
                );
            },
            token,
            std::ref(exe_),
            std::move(data)
        );
    }
    as::any_io_executor exe_;
};

struct tls {
    tls(as::any_io_executor exe):exe_{std::move(exe)} {}
    template <typename CompletionToken>
    auto async_send(
        std::string data, 
        CompletionToken&& token
    ) {
        return as::async_initiate<
            CompletionToken,
            void(boost::system::error_code)
        >(
            [](
                auto completion_handler,
                as::any_io_executor& exe, 
                std::string data
            ) {
                as::post(
                    exe,
                    [
                        data = std::move(data), 
                        completion_handler = std::move(completion_handler)
                    ] () mutable {
                        std::cout 
                            << "tls data:" << data 
                            << " send finish" << std::endl;
                        std::move(completion_handler)(boost::system::error_code{});
                    }
                );
            },
            token,
            std::ref(exe_),
            std::move(data)
        );
    }
    as::any_io_executor exe_;
};

using connection = std::variant<tcp, tls>;

int main() {
    as::io_context ioc;
    std::vector<connection> v;
    v.emplace_back(tcp(ioc.get_executor()));
    v.emplace_back(tls(ioc.get_executor()));
    for (auto& e : v) {
        std::visit(
            [](auto& e) {
                e.async_send(
                    "hello", 
                    [](auto ec) { 
                        std::cout << "finish ec:" << ec.message() << std::endl; 
                    }
                );
            },
            e
        );
    }
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/39jfbovzE

### variantでCompletionTokenを試す

上記のコードは、variantを利用した型消去のコード例です。オブジェクト指向アプローチと同様、main関数では、コールバック関数を与えています。coroutineを利用するコードに書き換えてみます。

```cpp
as::awaitable<void> proc(as::io_context& ioc) {
    std::vector<connection> v;
    v.emplace_back(tcp(ioc.get_executor()));
    v.emplace_back(tls(ioc.get_executor()));
    for (auto& e : v) {
        co_await std::visit(
            [](auto& e) -> as::awaitable<void> {
                auto [ec] = co_await e.async_send(
                    "hello",
                    as::as_tuple(as::use_awaitable)
                );
                std::cout << "finish ec:" << ec.message() << std::endl;
                co_return;
            },
            e
        );
    }
    co_return;
}

int main() {
    as::io_context ioc;
    as::co_spawn(ioc.get_executor(), proc(ioc), as::detached);
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/sYz6Ycva7

コールバックの場合と異なり、vectorの要素ひとつずつ結果待ちしてしまっており、効率が悪いですが、きちんとcoroutineとして動作しています。

