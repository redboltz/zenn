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


https://godbolt.org/z/YKGYe8G31