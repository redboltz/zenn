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
