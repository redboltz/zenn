---
title: "Boost.Asioのstackless coroutineの基本"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: false
---

# Boost.Asioのstackless coroutine

Boost.Asioではstackless coroutuneがサポートされています。
これは、switch-caseをバックグラウンドで活用した、非同期プログラミングを助ける仕組みです。
https://www.boost.org/doc/html/boost_asio/overview/composition/coroutine.html

ライブラリ独自の仕組みということもあり、なかなか情報がありません。
さらに、多くの場合、asioの非同期処理と合わせて利用されるので、どこまでが、stackless coroutineの仕組みで、どこまでがasioの非同期関数の仕組みなのかも分かりにくいです。
ここでは、stackless coroutineに必要な要素に絞ってその振る舞いを紹介したいと思います。

## コード

```cpp
#include <iostream>
#include <boost/asio.hpp>

namespace as = boost::asio; // このnamespace aliasを常に使います

#include <boost/asio/yield.hpp>

int main() {
    as::coroutine coro;

    auto resumable_function =
        [&] {
            std::cout << "before reenter" << std::endl;
            reenter(coro) {
                std::cout << "1st" << std::endl;
                yield;
                std::cout << "2nd" << std::endl;
                yield;
                std::cout << "3rd" << std::endl;
            }
            std::cout << "after reenter" << std::endl;
        };

    std::cout << "call 1st" << std::endl;
    resumable_function();
    std::cout << "call 2nd" << std::endl;
    resumable_function();
    std::cout << "call 3rd" << std::endl;
    resumable_function();
    std::cout << "call 4th" << std::endl;
    resumable_function();
}

#include <boost/asio/unyield.hpp>
```

## 出力

godboltでの実行
https://godbolt.org/z/874G5fvPd

```
call 1st
before reenter
1st
after reenter
call 2nd
before reenter
2nd
after reenter
call 3rd
before reenter
3rd
after reenter
call 4th
before reenter
after reenter
```

## 詳細

```cpp
    as::coroutine coro;
```

まず、coroutineクラスのインスタンスcoroを定義します。この内部に、どこまで処理が進んでいるかを記憶します。
https://www.boost.org/doc/html/boost_asio/reference/coroutine.html

次にラムダ式、resumable_functionを定義します。ひとまず、定義だけで実行はされないので、処理の順にコードを追います。

```cpp
    std::cout << "call 1st" << std::endl;
    resumable_function();
```

ここで1回目のresumable_function()呼び出しを行います。

```cpp
    auto resumable_function =
        [&] {
            std::cout << "before reenter" << std::endl;
            reenter(coro) {
                std::cout << "1st" << std::endl;
                yield; // ここで抜ける。breakのイメージ。
                std::cout << "2nd" << std::endl; // ここまでは進まない。
```

すると、"before reenter"が出力されます。
次に、reenter(coro)で、以前に実行している箇所にジャンプします。
switch文のイメージです。
なお、reenterはマクロです。
以下のように、マクロを有効化、無効化するヘッダファイルがあり、これでマクロを利用するコードを挟み込みます。

```cpp
#include <boost/asio/yield.hpp>
// この間、reenter, yield, forkが有効
#include <boost/asio/unyield.hpp>
```

この種のマクロを使いたくない場合、BOOST_ASIO_CORO_REENTER, BOOST_ASIO_CORO_YIELD を使うことができます。

さて、コードに戻り、coroの判断結果、まだ何も実行していないので、直後の、"1st"を出力します。
次に、yieldです。一般には、`yield async_func(...);` のように利用し、async_func()を呼び出してreenterスコープから抜けます。
今回は最もシンプルなコードを目指しているので、`yield;`のみを記述空いています。
よって、なにも呼び出さずに、ここで抜けます。このとき、**coroに、このyieldまで実行したことが記憶されます**。


```cpp
            }
            std::cout << "after reenter" << std::endl;
        };
```

reenterスコープを抜けると、"after reenter"が出力されラムダ式から抜けます。

```cpp
    std::cout << "call 2nd" << std::endl;
    resumable_function();
```

ラムダ式を抜けると次のresumable_function()呼び出しを行います。

```cpp
    auto resumable_function =
        [&] {
            std::cout << "before reenter" << std::endl;
            reenter(coro) {                       // ここから
                std::cout << "1st" << std::endl;  //   |
                yield;                            //   V
                std::cout << "2nd" << std::endl;  // ここへジャンプ
                yield;
```

2回目の呼び出しでは、"before reenter"を出力した後、上記のように、前回のyieldの次の箇所にジャンプします。
このように、resumable_function()を呼び出す度に、次のyieldまで処理が進んでいくのです。
ただし、reenterの外側の"before reenter"と"after reenter"は常に出力されます。多くの場合、ここに意味のある処理を書くことはありません。
では、最後のyieldまで処理が進んだ状態で、resumable_function()を呼び出したらどうなるのでしょうか?
その場合は、reenterスコープを何もせずに抜けます。

```
call 4th
before reenter
after reenter
```

この出力が何もせずに抜けたことを示しています。

今回は、シンプルに振る舞いを理解するために、main()から、resumable_function()を何度も呼び出しました。
実際の非同期プログラミングでは、resumable_function()を非同期関数の完了コールバックとして渡します。
すると、非同期関数が完了したら、reenterスコープの続きが実行されるようになります。

# 非同期関数との組み合わせ例

最後に、非同期関数postとの組み合わせの例を示します。

## コード

```cpp
#include <iostream>
#include <functional>
#include <boost/asio.hpp>

namespace as = boost::asio;

#include <boost/asio/yield.hpp>

int main() {
    as::io_context ioc;
    as::coroutine coro;

    std::function<void()> resumable_function =
        [&] {
            std::cout << "before reenter" << std::endl;
            reenter(coro) {
                std::cout << "1st" << std::endl;
                yield as::post(ioc, resumable_function);
                std::cout << "2nd" << std::endl;
                yield as::post(ioc, resumable_function);
                std::cout << "3rd" << std::endl;
            }
            std::cout << "after reenter" << std::endl;
        };

    std::cout << "call 1st" << std::endl;
    resumable_function();

    ioc.run();
}

#include <boost/asio/unyield.hpp>
```

## 出力

godboltでの実行
https://godbolt.org/z/6KvxenMsc

```
call 1st
before reenter
1st
after reenter
before reenter
2nd
after reenter
before reenter
3rd
after reenter
```

## 詳細

```cpp
    std::cout << "call 1st" << std::endl;
    resumable_function();
```

初回実行のみ、main()から呼び出していますが、それ以降は非同期の連鎖で実行が進みます。

```cpp
reenter(coro) {                              //
    std::cout << "1st" << std::endl;         // 初回 ここから 
    yield as::post(ioc, resumable_function); //      ここまで
    std::cout << "2nd" << std::endl;         // 非同期コールバックで ここから
    yield as::post(ioc, resumable_function); //                    ここまで
    std::cout << "3rd" << std::endl;         // 非同期コールバックで ここからここまで
}
```

それぞれyieldのあとに、非同期関数postを呼び出し、その引数にresumable_functionを渡しています。
こうすることで、コールバックのネストが深くなりがちな非同期関数の連鎖を、縦に並べて書くことができるようになります。

