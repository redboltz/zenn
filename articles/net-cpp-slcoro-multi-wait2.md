---
title: "同じCompletionToken引数の複数イベントの同時待ち"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: false
---

# 同じCompletionTokenの引数のマルチウェイト
@[card](https://zenn.dev/redboltz/articles/net-cpp-slcoro-multi-wait)
では、非同期足し算と、タイムアウトの2つのイベントをマルチウェイトしました。どちらが先に終了しても、有効な(nulloptでない)引数の型を確認することでイベントを区別できました。
しかし、マルチウェイトしたい非同期処理のCompletionTokenの引数の型が、常に都合良く異なるとは限りません。
たとえば、同じ非同期関数を2回以上呼び出した場合、どちらが完了したのか、どうやって知ればよいのでしょうか?

タイマを2回セットし、2回目のセットしたタイマが先に発火するシナリオを考えてみます。

## どちらの結果か区別できないコード

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
            proc(boost::system::error_code{});
        }
        void operator()(boost::system::error_code const& ec) const {
            proc(ec);
        }
    private:
        // 全てのoperator()の引数を受け入れる
        void proc(
            boost::system::error_code const& ec
        ) const {
            reenter(coro_) {
                // ここに非同期処理の連鎖を書いていく
                std::cout << "start" << std::endl;

                // yieldを {} で囲む。
                yield {
                    // タイマを2個連続セット
                    ma_.t1_.expires_after(std::chrono::milliseconds(200));
                    ma_.t1_.async_wait(*this);
                    ma_.t2_.expires_after(std::chrono::milliseconds(100));
                    ma_.t2_.async_wait(*this);
                }
                // yield{} 内部で2回非同期関数を呼び出しているので、
                // この行は2回実行される
                std::cout << "passing this line 2 times" << std::endl;
                if (!ec) {
                    // エラーなしということはタイマ発火
                    // なお、引数なしのoperator()はこの箇所では
                    // 発生し得ないことが分かっているものとする
                    std::cout << "timer fired" << std::endl;
                }
            }
        }
        my_app& ma_;
        mutable as::coroutine coro_;
    };

    Executor exe_;
    // タイマを2個用意
    as::steady_timer t1_{exe_};
    as::steady_timer t2_{exe_};
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
https://godbolt.org/z/W9534P38f

```
operator()()
start
passing this line 2 times
timer fired
passing this line 2 times
timer fired
```

## 詳細

```cpp
// タイマを2個用意
as::steady_timer t1_{exe_};
as::steady_timer t2_{exe_};
```

my_appのメンバとしてタイマを2個用意しました。

```cpp
yield {
    // タイマを2個連続セット
    ma_.t1_.expires_after(std::chrono::milliseconds(200));
    ma_.t1_.async_wait(*this);
    ma_.t2_.expires_after(std::chrono::milliseconds(100));
    ma_.t2_.async_wait(*this);
}
```

そのタイマをyieldの中で連続して設定します。最初に`t1_`を200ms後に発火するように設定し、続いて、`t2_`を100ms後に発火するように設定します。

```cpp
void operator()(boost::system::error_code const& ec) const {
    proc(ec);
}
```

タイマのコールバックは、上記オーバーロードにマッチします。

```cpp
void proc(
    boost::system::error_code const& ec
) const {
```

そしてそれをprocにそのまま渡します。今回、procの引数はoptionalにしていません。

```cpp
void operator()() const {
    std::cout << "operator()()" << std::endl;
    proc(boost::system::error_code{});
}
```

引数なしのoperator()は、冒頭にしか呼ばれないことを知っているので、それを前提とした設計にしてみました。

### 型による判定の限界
```cpp
yield {                
    // 非同期関数呼び出しその1(足し算)
    async_add(ma_.exe_, 2, 3, *this);
    // 非同期関数呼び出しその2(足し算)
    async_add(ma_.exe_, 1, 2, *this);
}
```

非同期関数は、2+3, 1+2の順で呼ばれていますが、以下のif文ではどちらの結果か区別できません。いずれも条件は成立します。

```cpp
if (result) {
    // ecが設定されているということはタイムアウトタイマの結果
    std::cout << "async_add completed result is " << *result << std::endl;
}
```

```cpp
// yield{} 内部で2回非同期関数を呼び出しているので、
// この行は2回実行される
std::cout << "passing this line 2 times" << std::endl;
if (!ec) {
    // エラーなしということはタイマ発火
    // なお、引数なしのoperator()はこの箇所では
    // 発生し得ないことが分かっているものとする
    std::cout << "timer fired" << std::endl;
}
```

タイマが発火する度に上記コードが実行されますが、どちらのタイマが発火したのか知る術はありません。
型で判定しようにも、同じ非同期関数の複数回呼び出しなので、当然ながら型も同じです。

### appendという対策

このような場合に使えるのが、append()による引数の追加です。
https://www.boost.org/doc/html/boost_asio/reference/append.html

append()は、CompletionTokenの末尾引数を追加するwrapperです。

```cpp
ma_.t1_.async_wait(*this);
```

を以下のように変更します。

```cpp
ma_.t1_.async_wait(
    as::append(
        *this,
        "timer1"
    )
);
```

こうすることで、CompletionTokenの末尾に"timer1"が追加されます。

operator()周辺コードもこれにマッチするように変更します。

```cpp
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
```

タイマ発火に対応する、引数`boost::system::error_code const&`を持つメンバ関数の末尾に、`std::string`を追加しました。

## どちらの結果か区別できるコード

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
                    ma_.t1_.expires_after(std::chrono::milliseconds(200));
                    ma_.t1_.async_wait(
                        as::append(
                            *this,
                            "timer1"
                        )
                    );
                    ma_.t2_.expires_after(std::chrono::milliseconds(100));
                    ma_.t2_.async_wait(
                        as::append(
                            *this,
                            "timer2"
                        )
                    );
                }
                // yield{} 内部で2回非同期関数を呼び出しているので、
                // この行は2回実行される
                std::cout << "passing this line 2 times" << std::endl;
                if (!ec) {
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
    // タイマを2個用意
    as::steady_timer t1_{exe_};
    as::steady_timer t2_{exe_};
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
https://godbolt.org/z/4TbKdrvKE

```
operator()()
start
passing this line 2 times
timer2 fired
passing this line 2 times
timer1 fired
```

このように、追加された引数strを参照することで、どちらのタイマが発火したか分かるようになりました。

## 関連事項
今回の例では、CompletionTokenの末尾にstd::stringをついかしましたが、もちろん任意の型の引数を追加することができます。
類似の関数として以下があります。
https://www.boost.org/doc/html/boost_asio/reference/append.html
https://www.boost.org/doc/html/boost_asio/reference/prepend.html
https://www.boost.org/doc/html/boost_asio/reference/consign.html

関数名|機能|用途例
---|---|---
append|末尾に引数追加|Token invoke時の情報付加
prepend|先頭に引数追加|Token invoke時の情報付加
consign|invokeまでコピーを保持|shared_ptrなどの延命
