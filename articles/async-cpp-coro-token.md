---
title: "C++20 coroutineとCompletionToken"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: false
---

# Coroutine と CompletionToken

## CompletionTokenとは
CompletionTokenについてはこちらで紹介しています。

@[card](https://zenn.dev/redboltz/articles/async-cpp-callback-token)

再確認のため、ラムダ式と、関数オブジェクトによる、コールバックベースのコードを示します。
引数として、move only型のarg1と、moveもcopyもできない型のarg2を渡しています。

## async_initiateを用いた非同期関数。

```cpp
template <typename CompletionToken>
auto async_func(move_only arg1, ref_only& arg2, CompletionToken&& token) {
    return as::async_initiate<
        CompletionToken,
        void(int)
    >(
        [](auto completion_handler,
            move_only arg1,
            ref_only& arg2
        ) {
            (void)arg1;
            (void)arg2;
            std::move(completion_handler)(42);
        },
        token,
        std::move(arg1),
        std::ref(arg2)
    );
}
```

arg2を渡すのに、`std::ref`を用いています。これは、CompletionTokenにCoroutine関連のものを渡した場合に、コピーを避けるために必要です。


## Code

```cpp
#include <iostream>
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/experimental/use_promise.hpp>

namespace as = boost::asio;

struct move_only {
    move_only() = default;
    move_only(move_only const&) = delete;
    move_only(move_only&&) = default;
    move_only& operator=(move_only const&) = delete;
    move_only& operator=(move_only&&) = default;
    void operator()(int val) && {
        std::cout << "fobj:" << val << std::endl;
    }
};

struct ref_only {
    ref_only() = default;
    ref_only(ref_only const&) = delete;
    ref_only(ref_only&&) = delete;
    ref_only& operator=(ref_only const&) = delete;
    ref_only& operator=(ref_only&&) = delete;
};

template <typename CompletionToken>
auto async_func(move_only arg1, ref_only& arg2, CompletionToken&& token) {
    return as::async_initiate<
        CompletionToken,
        void(int)
    >(
        [](auto completion_handler,
            move_only arg1,
            ref_only& arg2
        ) {
            (void)arg1;
            (void)arg2;
            std::move(completion_handler)(42);
        },
        token,
        std::move(arg1),
        std::ref(arg2)
    );
}

as::awaitable<void> proc() {
    ref_only arg2;

    auto v1 = co_await async_func(
        move_only(),
        arg2,
        as::use_awaitable
    );
    std::cout << "use_awaitable:" << v1 << std::endl;

    auto v2 = co_await async_func(
        move_only(),
        arg2,
        as::deferred
    );
    std::cout << "deferred:" << v2 << std::endl;

    auto v3 = co_await async_func(
        move_only(),
        arg2,
        as::experimental::use_promise
    );
    std::cout << "use_promise:" << v3 << std::endl;

    co_return;
}

int main() {
    as::io_context ioc;

    // callback (lambda expression)
    move_only arg1;
    ref_only arg2;
    async_func(
        std::move(arg1),
        arg2,
        [](int val) {
            std::cout << "cb:" << val << std::endl;
        }
    );

    // callback (function object)
    move_only cb;
    async_func(
        move_only(),
        arg2,
        std::move(cb)
    );

    as::co_spawn(ioc.get_executor(), proc(), as::detached);
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/roMczonM4


## co_await可能なCompletionToken
Boost.Asioでは、`awaitable<T>`型の戻り値を持つ関数の中で、`co_await`を使うことができます。
`co_await`を使うときは、CompletionTokenに、`use_awaitable`, `deferred`, `experimental::use_promise`のいずれかを渡します。

それぞれの違いを以下にまとめます。

CompletionToken | 型消去 | awaitable<T>でのco_await | co_composedでのco_await | `&&` `\|\|` operatorでの複数待ち | make_parallel_groupでの複数待ち
---|---|---|---|---|---
use_awaitable|される|可能|不可能|可能|不可能
deferred|されない|可能|可能|不可能|可能
use_promise|される|可能|可能|不可能|不可能


