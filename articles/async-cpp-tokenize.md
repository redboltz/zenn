---
title: "レガシーなcallbackをCompletionToken化"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: false
---

# callbackの例

```cpp
#include <iostream>
#include <boost/asio.hpp>

namespace as = boost::asio;

template <typename Executor, typename Cb>
void my_async_func(Executor exe, int a, int b, Cb&& cb) {
    as::post(
        exe,
        [a, b, cb = std::forward<Cb>(cb)] {
            cb(a + b);
        }
    );
}

int main() {
    as::io_context ioc;
    my_async_func(
        ioc.get_executor(),
        2, 3,
        [] (int v) {
            std::cout << v << std::endl;
        }
    );
    ioc.run();
}
```

godboltでの実行:
https://godbolt.org/z/aW5Tvxh5W
これは、非同期callbackの一例です。引数のaとbを加算して、callbackの引数として返します。

CbはCompletionTokenではありません。

```cpp
std::future<int> f = my_async_func(ioc.get_executor(), 2, 3, as::use_future);
```

よって、例えば、futureを合わせて使うことはできません。

godboltでの実行:
https://godbolt.org/z/3ov8hYc77

# callbackのCompletionToken化
もし、callbackをCompletionTokenにできたら、上記futureだけでなく、以下で紹介した様々な機能と組み合わせることができるようになります。
https://zenn.dev/redboltz/articles/async-cpp-callback-token

my_async_func()が例えばサードパーティ提供のコードで変更できないと仮定して、この関数をCompletionToken対応させてみましょう。

```cpp
template <typename Executor, typename CompletionToken>
auto tokenized(Executor exe, int a, int b, CompletionToken&& token) {
    auto init = 
        [](
            auto completion_handler,
            Executor exe,
            int a, 
            int b
        ) {
            using namespace std::placeholders;
            my_async_func(
                exe,
                a, b,
                std::bind(
                    [](auto completion_handler, int v) {
                        std::move(completion_handler)(v);
                    },
                    std::move(completion_handler),
                    _1
                )
            );
        };
    
    return 
        as::async_initiate<
            CompletionToken,
            void(int)
        >(
            init,
            std::forward<CompletionToken>(token),
            exe, a, b
        );
}
```

godboltでの実行:
https://godbolt.org/z/7T5Y8ssTf

これが、my_async_func()をCompletionToken対応させるための、wrapper関数です。
ラムダ式initに、my_async_func()の呼び出しを行うコードを記述します。
そして、最後に、async_initiate()関数を呼び出します。
template argumentとして、CompletionTokenと、wrapするcallbackの型である、`void(int)`を渡します。
関数の引数としては、init、tokenのforwardに続いて、callbackの呼び出しに必要な引数をそのまま渡します。
これで、CompletionToken対応ができました。
ラムダ式initの第1引数completion_handlerの取り扱いには少し注意が必要です。

```cpp
std::move(completion_handler)(v);
```

invokeするときに、completion_handlerをmoveする必要があります。これを実現するために、`std::bind`で、completion_handlerをcallbackの第1引数として渡しています。

## bindは必要か?ラムダ式ではダメなのか?

```cpp
template <typename Executor, typename Cb>
void my_async_func(Executor exe, int a, int b, Cb&& cb) {
    boost::asio::post(
        exe,
        [a, b, cb = std::forward<Cb>(cb)] () /*mutable*/ {
            cb(a + b);
        }
    );
}
```

変更できないと仮定したmy_async_func()のラムダ式部分に、mutableをコメントとして追加しました。
このコメントをuncommentして、mutableなラムダ式にすれば、bindの代わりにラムダ式を使うことができます。

godboltでの実行:
https://godbolt.org/z/G5WvxsPWz

```cpp
my_async_func(
    exe,
    a, b,
    [completion_handler = std::move(completion_handler)]
    (int v) mutable {
        std::move(
            completion_handler
        )(v);
    }
);
```
この呼び出し箇所で、mutableなラムダ式を渡すことが可能となり、その中で、キャプチャしたcompletion_handlerをmoveすることができるためです。
しかし、残念ながら実際は、my_async_func()内部のラムダ式がmutableではない(constであるために、この呼び出し箇所のラムダ式をmutableにすることができず、mutableでなければ、キャプチャしたcompletion_handlerをmoveできません。

強引な対策として、completion_handlerをconst_castして無理矢理moveすることは可能ですが、なかなかわかりにくいコードです。
godboltでの実行:
https://godbolt.org/z/jYbGaeT3d

こういったことを考えると、もし、my_async_func()のような非同期関数が、constなcallableしか受け付けない場合は、bindを使うのがベターかなと思います。

最後に、bindを使ったコードを再掲(godbolt参照)しておきます。
godboltでの実行:
https://godbolt.org/z/7T5Y8ssTf