---
title: "非同期関数の実装内部でco_awaitを使う"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async.co_composed]
published: true
---

# 非同期関数の実装内部でco_awaitを使う

## CompletionToken対応の非同期関数

CompletionToken対応の非同期関数は、CompetionTokenに、`deferred`, `use_awaitable`, `experimental::use_future`を渡すことで、co_awaitが可能となります。またコールバック関数を渡すこともできます。
このようなCompletionToken対応の非同期関数の実装にco_awaitを使う方法を示します。

通常、co_awaitを使うためには、co_awaitを使う関数の戻り値の型が、`awaitable<T>`である必要があります。こうなると、関数が、coroutine前提となり、コールバック関数対応など柔軟なCompletionToken対応ができません。
これを解決するために、`boost::asio::experimental::co_composed`を利用します。

## Code

```cpp
#include <iostream>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/asio/experimental/co_composed.hpp>

namespace as = boost::asio;

template <typename CompletionToken>
auto co_composed_func(
    as::any_io_executor exe,
    CompletionToken&& token
) {
    return as::async_initiate<
        CompletionToken,
        void()
    >(
        as::experimental::co_composed<
            void()
        >(
            [](auto /*state*/, auto exe) -> void {
                as::steady_timer tim{exe, std::chrono::seconds(1)};
                auto [ec] = co_await tim.async_wait(as::as_tuple(as::deferred));
                std::cout << ec.message() << std::endl;
                co_return {};
            },
            exe
        ),
        token,
        exe
    );
}

int main() {
    as::io_context ioc;

    std::cout << "start" << std::endl;
    auto start = std::chrono::system_clock::now();

    co_composed_func(
        ioc.get_executor(),
        [&] {
            auto end = std::chrono::system_clock::now();
            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
            std::cout << elapsed << std::endl;
        }
    );
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/fheq4xc8v

今回実装する非同期関数は、`co_composed_func()`です。`int main()`からコールバック関数をCompletionTokenとして呼び出しています。ここまで、Coroutine的な要素が混入していないことに注意してください。

async_initiateの第1引数として、`experimental::co_composed`を渡します。template引数に、signatureを渡しますが、これは、`async_initiate`の第2 template引数と同じsignatureを渡します。async_initiateの、第1引数には、`token`を、第2引数以降は任意の引数を渡します。`co_composed`の第1引数`state`は`token`に対応します。第2引数以降はasync_initiateの引数が対応します。

さて、ここで1秒タイマを設定しています。そして、タイマの、`async_wait`のCompletionTokenとして、`deferred`を渡しています。そして、`co_await`で完了を待っています。このように、`co_composed_func()`の戻り値が`awaitable<T>`ではないにも関わらず、関数の実装で、`co_await`を使うことができます。実装の戻り値は、`co_return`で指定します。また、戻り値がvoidであっても、`{}`と空のinitialize listを返す必要がある点に注意してください。
なお、実装内部のCompletionTokenとして、`deferred`を用いていますが、ここで、`use_awaitable`を使うことはできません。これが、`co_composed`の現在の制約といえます。`deferred`では型情報が維持されるため、`variant`などとの組み合わせで、戻り値の型が一致しないエラーに遭遇することがあります。このような場合、型消去が必要となりますが、`use_awaitable`が使えないため、`experimental::use_promise`を使うことになるでしょう。


## 備考
本記事は、experimentalな要素に関しての内容であり、Boost.1.84.0で動作確認しています。将来、仕様が変更される可能性がある点に注意してください。

