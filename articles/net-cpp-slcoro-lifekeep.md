---
title: "CompletionTokenとオブジェクトの延命"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async,shared_ptr]
published: false
---

# タイマを動的に作成
@[card](https://zenn.dev/redboltz/articles/net-cpp-slcoro-multi-wait2)
では、タイマを2つ準備して、どちらが発火したのか区別する方法を示しました。
このとき、2つのタイマはmy_appのメンバ変数として準備しました。
タイマの数が動的に変化し、予測できない場合はどうすれば良いでしょうか?my_appにtimerのvectorを持たせる?もっと良い方法があります。
タイマを動的に生成し、CompletionTokenのinvokeまでその寿命を維持すればよいのです。
その方法を紹介します。

## 寿命が維持できていないコード

```cpp
#include <iostream>
#include <chrono>
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
    struct worker {
        worker(my_app& ma)
            :ma_{ma}
        {
            (*this)(); // 最初のoperator()をキック
        }
        void operator()() const {
            std::cout << "operator()()" << std::endl;
            proc(boost::system::error_code{}, std::string{});
        }
        void operator()(
            boost::system::error_code const& ec,
            std::string str
        ) const {
            proc(ec, std::move(str));
        }
    private:
        // 全てのoperator()の引数を受け入れる
        void proc(
            boost::system::error_code const& ec,
            std::string str
        ) const {
            reenter(coro_) {
                // ここに非同期処理の連鎖を書いていく
                std::cout << "start" << std::endl;

                // yieldを {} で囲む。
                yield {
                    // タイマを2個連続セット
                    as::steady_timer t1{ma_.exe_};
                    t1.expires_after(std::chrono::milliseconds(200));
                    t1.async_wait(
                        as::append(
                            *this,
                            "timer1"
                        )
                    );
                    as::steady_timer t2{ma_.exe_};
                    t2.expires_after(std::chrono::milliseconds(100));
                    t2.async_wait(
                        as::append(
                            *this,
                            "timer2"
                        )
                    );
                }
                // yield{} 内部で2回非同期関数を呼び出しているので、
                // この行は2回実行される
                std::cout << "passing this line 2 times" << std::endl;
                if (ec) {
                    std::cout << str << " " << ec.message() << std::endl;
                }
                else {
                    // エラーなしということはタイマ発火
                    // なお、引数なしのoperator()はこの箇所では
                    // 発生し得ないことが分かっているものとする
                    std::cout << str << " fired" << std::endl;
                }
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
https://godbolt.org/z/d4Ezfo9bj

```
operator()()
start
passing this line 2 times
timer2 Operation canceled
passing this line 2 times
timer1 Operation canceled
```

## 詳細

```cpp
yield {
    // タイマを2個連続セット
    as::steady_timer t1{ma_.exe_};
    t1.expires_after(std::chrono::milliseconds(200));
    t1.async_wait(
        as::append(
            *this,
            "timer1"
        )
    );
    as::steady_timer t2{ma_.exe_};
    t2.expires_after(std::chrono::milliseconds(200));
    t2.async_wait(
        as::append(
            *this,
            "timer2"
        )
    );
}
// 
```

タイマをmy_appのメンバとしてではなく、yieldの中でローカル変数として定義しました。
この方法では、yieldのスコープから抜けたときに、t1,t2の寿命が尽きてしまいます。


```cpp
// yield{} 内部で2回非同期関数を呼び出しているので、
// この行は2回実行される
std::cout << "passing this line 2 times" << std::endl;
if (ec) {
    std::cout << str << " " << ec.message() << std::endl;
}
else {
    // エラーなしということはタイマ発火
    // なお、引数なしのoperator()はこの箇所では
    // 発生し得ないことが分かっているものとする
    std::cout << str << " fired" << std::endl;
}
```

その結果、それぞれのタイマが破棄されるときに、デストラクタでタイマがキャンセルされ、その旨が出力されます。
寿命が適切に管理されていないローカル変数と、非同期関数の組み合わせは、Undefined Behaviorにも陥りやすいので注意が必要です。

### shared_ptrとconsignという解決策
多くの場合、タイマをshared_ptrにすることで問題を解決します。

```cpp
    as::steady_timer t1{ma_.exe_};
    t1.expires_after(std::chrono::milliseconds(200));
    t1.async_wait(
        as::append(
            *this,
            "timer1"
        )
    );
```

上記、スタック上に確保していたt1を以下のようにshared_ptrにしてheapから確保します。

```cpp
auto t1 = std::make_shared<as::steady_timer>(ma_.exe_);
t1->expires_after(std::chrono::milliseconds(200));
t1->async_wait(
    as::append(
        *this,
        "timer1"
    )
);
```

ここで問題が生じます。
async_waitに、t1を延命のためにぶら下げたいのですが、どうすれば良いのでしょう。

```cpp
t1->async_wait(
    [t1](boost::system::error_code const& ec) {
    )
);
```

コールバックをラムダ式で渡すなら上記のようにできますが、stackless coroutineの良さが失われます。また、stackless coroutineのyieldベースの記述と、コールバックを混ぜると、非常に理解が困難なコードになりがちです。

この問題を解決するには、consign()関数を使います。
これは、CompletionTokenに値をぶら下げる為のwrapperです。appendやprependと異なり、値にアクセスすることはできません。もっぱら延命目的の関数です。
(C++のことなので、もしかしたら、想像を超えた利用法があるかも知れませんが、少なくとも私は延命目的でしか使用したことがありません。)
https://www.boost.org/doc/html/boost_asio/reference/consign.html

```cpp
t1->async_wait(
    as::consign(
        as::append(
            *this,
            "timer1"
        ),
        t1
    )
);
```

consign()を追加したコードです。wrapperなので、タイマを区別するために用いたappend()と合わせて使うこともできます。append()とconsing()のwrap順序は、どちらでも問題ありません。

## 寿命が維持されたコード

```cpp
#include <iostream>
#include <chrono>
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
    struct worker {
        worker(my_app& ma)
            :ma_{ma}
        {
            (*this)(); // 最初のoperator()をキック
        }
        void operator()() const {
            std::cout << "operator()()" << std::endl;
            proc(boost::system::error_code{}, std::string{});
        }
        void operator()(
            boost::system::error_code const& ec,
            std::string str
        ) const {
            proc(ec, std::move(str));
        }
    private:
        // 全てのoperator()の引数を受け入れる
        void proc(
            boost::system::error_code const& ec,
            std::string str
        ) const {
            reenter(coro_) {
                // ここに非同期処理の連鎖を書いていく
                std::cout << "start" << std::endl;

                // yieldを {} で囲む。
                yield {
                    // タイマを2個連続セット
                    auto t1 = std::make_shared<as::steady_timer>(ma_.exe_);
                    t1->expires_after(std::chrono::milliseconds(200));
                    t1->async_wait(
                        as::consign(
                            as::append(
                                *this,
                                "timer1"
                            ),
                            t1
                        )
                    );
                    auto t2 = std::make_shared<as::steady_timer>(ma_.exe_);
                    t2->expires_after(std::chrono::milliseconds(100));
                    t2->async_wait(
                        as::append(
                            as::consign(
                                *this,
                                t2
                            ),
                            "timer2"
                        )
                    );
                }
                // yield{} 内部で2回非同期関数を呼び出しているので、
                // この行は2回実行される
                std::cout << "passing this line 2 times" << std::endl;
                if (ec) {
                    std::cout << str << " " << ec.message() << std::endl;
                }
                else {
                    // エラーなしということはタイマ発火
                    // なお、引数なしのoperator()はこの箇所では
                    // 発生し得ないことが分かっているものとする
                    std::cout << str << " fired" << std::endl;
                }
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

動作に影響が無いことを確認するため、t1とt2でwrapの順序を逆にしてみました。

## 出力
godboltでの実行
https://godbolt.org/z/aWYE43o5n

```
operator()()
start
passing this line 2 times
timer2 fired
passing this line 2 times
timer1 fired
```
