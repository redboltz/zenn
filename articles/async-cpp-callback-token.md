---
title: "コールバックのCompletionToken化"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: false
---

# CompletionTokenとは
Boost.Asioのドキュメントには、以下のようにCompletion Tokensが説明されています。
https://www.boost.org/doc/html/boost_asio/overview/model/completion_tokens.html

さらに、
https://www.boost.org/doc/libs/1_82_0/doc/html/boost_asio/overview/composition.html
では、Completion Tokenの組み合わせて使える機能などが説明されています。

2023年において、Completion Tokenと組み合わせて使うことの多い機能は、

- callback (extended)
- future
- stackless coroutine
- C++20 coroutines (stackful)

といったところかと思います。

## ホスト名やIPアドレスからendpointを得るexample
Boost.Asioの機能に、ネットワーク接続の端点となるオブジェクト endpointを取得する機能があります。
ホスト名を名前解決してendpointを得たり、IPアドレスからendpointを得たりします。オンラインコンパイラのgodboltでは、ホスト名の解決が(おそらくセキュリティ上の都合で)できなかったので、IPアドレスからendpointを得る例を示します。

### callback(extended)
コールバックは、最もシンプルなCompletionTokenの使い方です。

```cpp
#include <iostream>
#include <boost/asio.hpp>

namespace as = boost::asio;

int main() {
    as::io_context ioc;
    as::ip::tcp::resolver r{ioc.get_executor()};
    r.async_resolve(
        "127.0.0.1",
        "12345",
        [&]
        (
            boost::system::error_code const& ec,
            as::ip::tcp::resolver::results_type results
        ) {
            std::cout << ec.message() << std::endl;
            for (auto const& result : results) {
                std::cout << result.endpoint() << std::endl;
            }
        }
    );
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/W6nPb84rT

async_resolveにhost, port, callbackの順で引数を渡しています。callbackはcallableなら何でも良いのですが、ここではラムダ式を渡しています。
async_resolveの仕様は以下を参照して下さい。
https://www.boost.org/doc/libs/1_82_0/doc/html/boost_asio/reference/ip__basic_resolver/async_resolve/overload2.html

CompletionTokenは、async_resolveの場合は、ResolveTokenと呼ばれるようですが、意味は非同期処理完了時にinvokeされるもの、ということです。
https://www.boost.org/doc/html/boost_asio/reference/ResolveToken.html

エラーコードecと、結果の集合resultsを引数で受け、結果をひとつひとつresultとして取り出し、そこから、endpointを取り出して表示しています。

callback(extended)と書いているのは、append()などの拡張が行えることを意味しています。
これに関しては
@[card](https://zenn.dev/redboltz/articles/net-cpp-slcoro-multi-wait2)
を参照していただければと思います。

以下に、56789という特に意味の無い値をappendした例を示します。

```cpp
#include <iostream>
#include <boost/asio.hpp>

namespace as = boost::asio;

int main() {
    as::io_context ioc;
    as::ip::tcp::resolver r{ioc.get_executor()};
    r.async_resolve(
        "127.0.0.1",
        "12345",
        as::append(
            [&]
            (
                boost::system::error_code const& ec,
                as::ip::tcp::resolver::results_type results,
                int append_val
            ) {
                std::cout << ec.message() << std::endl;
                for (auto const& result : results) {
                    std::cout << result.endpoint() << std::endl;
                }
                std::cout << "append_val:" << append_val << std::endl;
            }, 56789
        )
    );
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/fn9P6deYx

コールバックの引数の末尾にappend_valを追加し、そこに56789が渡されていることが確認できます。

#### callbackの欠点
callbackは、非同期処理の連鎖をラムダ式で書いた場合、どんどんネストが深くなっていき、コードの可読性が低下するという欠点があります。この状況はcallback hellなどと呼ばれることもあります。
では、コールバックを関数やメンバ関数にしたらどうでしょうか? ネストの問題は解決しますが、今度は、一連の非同期シーケンスを実行するためのコードが散らばってしまい、これはこれで可読性が低下するという欠点があります。

### future

futureは、callbackの欠点を解決する手法のひとつとして使うことができます。
futureをつかったコードを以下に示します。

```cpp
#include <iostream>
#include <boost/asio.hpp>

namespace as = boost::asio;

int main() {
    as::io_context ioc;
    auto guard = as::make_work_guard(ioc.get_executor());
    std::thread th {
        [&] {
            ioc.run();
        }
    };

    as::ip::tcp::resolver r{ioc.get_executor()};
    std::future<
        as::ip::tcp::resolver::results_type
    > f = r.async_resolve(
        "127.0.0.1",
        "12345",
        as::use_future
    );

    try {
        auto const& results = f.get();
        for (auto const& result : results) {
            std::cout << result.endpoint() << std::endl;
        }
    }
    catch (boost::system::system_error const& se) {
        std::cout << se.what() << std::endl;
    }

    guard.reset();
    th.join();
}
```

godboltでの実行:
https://godbolt.org/z/T795T4sjo

```cpp
    as::io_context ioc;
    auto guard = as::make_work_guard(ioc.get_executor());
    std::thread th {
        [&] {
            ioc.run();
        }
    };
```

まず、冒頭で、非同期処理実行用のthreadを準備します。そしてその**threadの中で**、ioc.run()を呼び出します。ただし、ioc.run()は非同期処理が登録されていない場合、すぐに抜けて終了してしまいます。これを避けるために、処理が登録されてなくてもioc.run()から抜けないようにするwork_guardというものを設定しています。

```cpp
    guard.reset();
    th.join();
```

コードの末尾で、work_guardを外し、ioc.run()に処理が登録されていない状態になったら抜けるようにしています。そして直後にthreadをjoin()しています。これは、futureとasioを組み合わせて使う場合の定番コードといえるでしょう。

```cpp
    std::future<
        as::ip::tcp::resolver::results_type
    > f = r.async_resolve(
        "127.0.0.1",
        "12345",
        as::use_future
    );
```

futureを使うには、CompletionTokenとして、`use_future`を渡します。すると、非同期関数`async_resolve()`はfutureを戻り値として返すようになります。
futureの型は`as::ip::tcp::resolver::results_type`です。
ここで、callbackの時の第1引数だった`boost::system::error_code`はどこに行ったのか気になると思います。
実は、`use_future`では、**CompletionTokenの第1引数が`boost::system::error_code`の場合にだけ、戻り値のfutureの型からそれを削除します**。
そして、エラーコードは、**エラー発生時にのみ`boost::system::system_error` というexceptionとして、futureをget()したときにthrowされる**のです。

```cpp
    try {
        auto const& results = f.get();
        for (auto const& result : results) {
            std::cout << result.endpoint() << std::endl;
        }
    }
    catch (boost::system::system_error const& se) {
        std::cout << se.what() << std::endl;
    }
```

こういった理由から、上記のような、try catchの記述を行っています。

CompletionTokenの引数とfutureの型の対応例をいくつか挙げておきます。

CompletionTokenの引数|futureの型
---|---
boost::system::error_code, int|int
boost::system::error_code, int, double|std::tuple<int, double>
int|int
boost::system::error_code, int, double, boost::system::error_code|std::tuple<int, double, boost::system::error_code>

