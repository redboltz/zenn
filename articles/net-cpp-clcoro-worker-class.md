---
title: "非同期処理を行うクラス"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: false
---

# ラムダ式ではなく、クラスで実装する

[Boost.Asioのstackless coroutineの基本](net-cpp-slcoro-simple-lambda.md)では、ラムダ式でシンプルなstackless coroutineの実装を示しました。
しかし、実際の開発ではクラスを使うことが多いと思います。
クラスのメンバ関数として、`operator()`を持たせることで、そのクラスはラムダ式と同様にcallableとなります。
postによって非同期処理を進めるコードを、クラスを用いて書いてみます。


## コード

```cpp
#include <iostream>
#include <boost/asio.hpp>

namespace as = boost::asio;

#include <boost/asio/yield.hpp>

template <typename Executor>
struct my_app {
    my_app(Executor exe)
        :exe_{std::move(exe)},
         worker_{*this}
    {}

private:
    friend struct worker;
    // workerをcopyableにして、非同期関数に *this を渡す。
    // 保持すべき情報は、外側クラスmy_appへの参照と、
    // as::coroutineのみ。
    // 必要なデータは、my_app側に足していく。
    struct worker {
        worker(my_app& ma)
            :ma_{ma}
        {
            (*this)(); // 最初のoperator()をキック
        }
        void operator()() const {
            std::cout << "operator()()" << std::endl;
            reenter(coro_) {
                std::cout << "1st" << std::endl;
                // 第2引数にcallableとしてのworker自身を渡す。
                yield as::post(ma_.exe_, *this);
                std::cout << "2nd" << std::endl;
                // 第2引数にcallableとしてのworker自身を渡す。
                yield as::post(ma_.exe_, *this);
                std::cout << "3rd" << std::endl;
            }
        }
        my_app& ma_;
        mutable as::coroutine coro_;
    };

    Executor exe_;
    worker worker_;
};

#include <boost/asio/unyield.hpp>

int main() {
    as::io_context ioc;
    my_app ma{ioc.get_executor()};
    ioc.run();
}
```

## 出力
godboltでの実行
https://godbolt.org/z/b58PhMKco

```
operator()()
1st
operator()()
2nd
operator()()
3rd
```

## 詳細
非同期処理を行うアプリケーションロジックは

```cpp
template <typename Executor>
struct my_app {
    my_app(Executor exe)
        :exe_{std::move(exe)},
         worker_{*this}
    {}
```
に実装しています。

そして、friendなnest classとして、workerを定義しています。

```cpp
private:
    friend struct worker;
    // workerをcopyableにして、非同期関数に *this を渡す。
    // 保持すべき情報は、外側クラスmy_appへの参照と、
    // as::coroutineのみ。
    // 必要なデータは、my_app側に足していく。
    struct worker {
        worker(my_app& ma)
            :ma_{ma}
        {
            (*this)(); // 最初のoperator()をキック
        }
```

my_appは、そのコンストラクタで、`*this`をwokerのコンストラクタに渡し、wokerはそれを保持します。

workerはメンバ変数として、

```cpp
        my_app& ma_;
        mutable as::coroutine coro_;
```

だけを持ちます。`ma_`を介してmy_appにアクセスし、`coro_`によって、yieldによる非同期の継続実行を実現します。
`coro_`はmutableになっています。これは、constメンバ関数の中からでも、書き換えられるようにしたいからです。
`coro_`の書き換えは、あくまでもreenterブロック内で、どこまで実行したかの情報を書き換えるだけなので、アプリケーションのconst性には影響しないという判断です。
排他制御のためのmutexをmutableにするのと似た感覚です。

```cpp
        void operator()() const {
            std::cout << "operator()()" << std::endl;
            reenter(coro_) {
                std::cout << "1st" << std::endl;
                // 第2引数にcallableとしてのworker自身を渡す。
                yield as::post(ma_.exe_, *this);
```

postの引数に`*this`を渡すことで、非同期処理完了時に、`woker::operator()`が呼び出されるという仕組みです。
wokerはコピーで渡されますが、メンバがmy_appへの参照と、`as::coroutine`だけなのでコストは極めて低いです。
この先、アプリケーションロジックに必要なメンバが追加されていったとしても、my_appにメンバが増えるだけなので、workerのコピーコストは変化しません。

### 細かい設計判断

#### workerが、coroutineをメンバとして持つか継承するか
どちらでも実現できます。
継承版は https://godbolt.org/z/ePEneY1Wj です。
こうした場合、

クラス定義は

```cpp
    struct worker : private as::coroutine {
```

となり、

```cpp
        void operator()() /*const*/ { // ここがconstにできない
            std::cout << "operator()()" << std::endl;
            reenter(*this) {
```

reenterに`coro_`の代わりに`*this`を渡します。
ここで注意したいのは、`operator()`をconstにできないという点です。
これは、基底クラスのcoroutineをmutableにできないためです。

以下はエラーになるコードですが、

```cpp
    struct worker : private mutable as::coroutine {
```

もしこのように、基底クラスをmutableにできれば、話は違ったかも知れません。

しかし、本ケースではメンバ変数で済むものを基底クラスにすることにメリットは無いと思います。
EBOが働く状況でもないですし。
さらにいえば、2系統の`coro1_` `coro2_` を持ちたいケースも出てくるかも知れません。

そんなわけで、`coro_`はメンバ変数として持たせる設計判断をしています。

### workerクラスを準備してコピーベースの実装にした理由
my_appに直接`operator()`を実装することをまず考えました。
この場合、post()に`*this`を渡すと、my_appの全てのメンバがコピーされてしまいます。
このコストは避けたいです。
では、`as::post(exe_, std::move(*this));`とmoveしてはどうか?というアイデアが浮かびました。
しかし、`*this`のmoveは非常に注意深く実装しなければならず、ミスが起こりやすいです。
例えば、この例でも、`exe_`はmoved from objectになっているでしょう。
さらに、**moveしたからといって必ずしもコピーコストが避けられるとは限りません**。
my_appのメンバ変数に`std::array`のようなmoveがcopyにfallbackされるクラスが存在したら、結局コピーは発生します。

これらの問題を解決するために、wokerクラスに、my_appの参照を持たせる、という設計としました。


