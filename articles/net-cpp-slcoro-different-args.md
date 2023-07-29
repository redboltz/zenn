---
title: "異なるコールバック引数への対応"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: true
---

# 異なるコールバック引数への対応
@[card](https://zenn.dev/redboltz/articles/net-cpp-clcoro-worker-class)
では、非同期処理の例として`post()`しか使いませんでした。
これは、CompletionToken(非同期コールバックもその一種)の引数が無く、最もシンプルなためです。

しかし、実際の非同期処理では処理結果のエラーコード、多くの場合`boost::system::error_code const&`や、その他非同期処理固有の引数を持ちます。

本記事では、そのような多様な引数を扱いながらも、内部の処理の連続性を保つ方法を紹介します。

## コード

```cpp
#include <iostream>
#include <chrono>
#include <optional>
#include <boost/asio.hpp>

namespace as = boost::asio;

#include <boost/asio/yield.hpp>

template <typename Executor, typename Cb>
void async_add(Executor exe, int a, int b, Cb cb) {
    as::post(
        exe, [a, b, cb] {  
            cb(a + b); 
        }
    );
}

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
        // 引数無し
        void operator()() const {
            std::cout << "operator()()" << std::endl;
            proc(std::nullopt, std::nullopt);
        }
        // 引数エラーコード
        void operator()(boost::system::error_code const& ec) const {
            std::cout << "operator()(boost::system::error_code const&)" << std::endl;
            proc(ec, std::nullopt);
        }
        // 引数async_addの結果のint
        void operator()(int result) const {
            std::cout << "operator()(int)" << std::endl;
            proc(std::nullopt, result);
        }
    private:
        // 全てのoperator()の引数を受け入れる
        void proc(
            std::optional<boost::system::error_code> ec,
            std::optional<int> result
        ) const {
            reenter(coro_) {
                // ここに非同期処理の連鎖を書いていく
                std::cout << "start" << std::endl;
                
                // workerオブジェクト自身を渡すことで、
                // post完了時にマッチするoperator()()が呼ばれる
                yield as::post(ma_.exe_, *this);
                std::cout << "post finished" << std::endl;

                ma_.tim_.expires_after(std::chrono::seconds(1));
                yield ma_.tim_.async_wait(*this);
                std::cout << "timer fired" << std::endl;

                yield async_add(ma_.exe_, 1, 2, *this);
                BOOST_ASSERT(result);
                std::cout << "async_add completed result is " << *result << std::endl;
            }
        }
        my_app& ma_;
        mutable as::coroutine coro_;
    };

    Executor exe_;
    as::steady_timer tim_{exe_};
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
https://godbolt.org/z/zfTYsnGWr

```
operator()()
start
operator()()
post finished
operator()(boost::system::error_code const&)
timer fired
operator()(int)
async_add completed result is 3
```

## 詳細

### 様々なCompletionTokenの引数

まず、非同期で足し算する関数を説明のために準備しました。
これは、引数として整数 a, bを渡し、足し算を行った結果をコールバックの引数で返すというものです。
つまり、CompletionTokenの引数として、ひとつのintを持つ、ということです。

```cpp
template <typename Executor, typename Cb>
void async_add(Executor exe, int a, int b, Cb cb) {
    as::post(
        exe, [a, b, cb] {  
            cb(a + b); 
        }
    );
}
```

さらに、Boost.Asioのsteady_timerも利用します。
https://www.boost.org/doc/html/boost_asio/reference/basic_deadline_timer/async_wait.html
こちらは、CompletionTokenの引数としてひとつの、`boost::system::error_code const&` を持ちます。

### operator()のオーバーロードとproc()による集約

```cpp
        // 引数無し
        void operator()() const {
            std::cout << "operator()()" << std::endl;
            proc(std::nullopt, std::nullopt);
        }
        // 引数エラーコード
        void operator()(boost::system::error_code const& ec) const {
            std::cout << "operator()(boost::system::error_code const&)" << std::endl;
            proc(ec, std::nullopt);
        }
        // 引数async_addの結果のint
        void operator()(int result) const {
            std::cout << "operator()(int)" << std::endl;
            proc(std::nullopt, result);
        }
```

まず、operator()ですが、CompletionTokenの引数に対応するものを、全て網羅するように、オーバーロードで実装します。
そして、引数は、proc()に渡します。

```cpp
    private:
        // 全てのoperator()の引数を受け入れる
        void proc(
            std::optional<boost::system::error_code> ec,
            std::optional<int> result
        ) const {
            reenter(coro_) {
                // ここに非同期処理の連鎖を書いていく
                std::cout << "start" << std::endl;
                
                // workerオブジェクト自身を渡すことで、
                // post完了時にマッチするoperator()()が呼ばれる
                yield as::post(ma_.exe_, *this);
                std::cout << "post finished" << std::endl;

                ma_.tim_.expires_after(std::chrono::seconds(1));
                yield ma_.tim_.async_wait(*this);
                std::cout << "timer fired" << std::endl;

                yield async_add(ma_.exe_, 1, 2, *this);
                BOOST_ASSERT(result);
                std::cout << "async_add completed result is " << *result << std::endl;
            }
        }
```

proc()は、全てのoperator()のオーバーロードの引数を並べて受け入れられるようにします。
今回は、引数の型を全てoptionalにしています。こうすることで、operator()内のproc()呼び出し側で、マッチしない箇所にはnulloptを渡せます。

reenterは、proc()内で実装します。
まず、引数無しのoperator()経由でコンストラクタから処理がスタートされます。
次に、post()を呼び出します。この結果も引数無しのoperator()が、マッチします。
次に、1秒タイマを設定します。1秒後に引数`boost::system::error_code const&`のoperator()がマッチします。
最後に、今回説明用に準備した、async_add()を呼び出します。こちらは引数intのoperator()がマッチします。
その値は、procの`optional<int> result`に渡され、`*result`として出力しています。

このように、operator()のオーバーロードを準備し、呼び出されたときに、引数をひとつの関数proc()に転送することで、異なる引数を持つCompletionTokenに対応することができます。

### 細かい設計判断
#### proc()の引数の型はoptionalにする必要があるか
今回は、全てのproc()の引数をoptionalにしました。コードの意図を明確にするためです。
ただ、参照で渡ってきた引数をコピーしてしまっています。
例えば、`boost::system::error_code const&`に関しては、以下のようにすることもできます。

```cpp
        void proc(
            boost::system::error_code const& ec, // const&のままで、optionalにしない
            std::optional<int> result
        ) const {
```

```cpp
        // 引数async_addの結果のint
        void operator()(int result) const {
            std::cout << "operator()(int)" << std::endl;
            proc(boost::system::error_code{}, result); // error_codeをdefault constructする。(successになる)
        }

```

この場合、ecがsuccessな場合に、非同期処理が成功してsuccessなのか、それともecは使わないパターンのオーバーロードがマッチしたのか、区別できません。
コピーを避けつつ、nullを区別したいならば、ポインタも選択肢になるかも知れません。
