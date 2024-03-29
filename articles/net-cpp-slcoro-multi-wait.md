---
title: "複数イベントの同時待ち"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: true
---

# マルチウェイト
非同期処理では、どちらが先に発生するか分からない複数のイベントを待ち受け、発生したイベントに応じて処理を行うことがよくあります。
例えば、メッセージ受信待ちと、そのタイムアウト待ちを同時に行い、一定時間以内にメッセージが受信されない場合、それまでの処理を無効にするといった処理などです。
コールバック関数を非同期処理毎に書く場合は、直感的に実装できますが、reenter-yieldを組み合わせたstackless coroutineではどのように書けば良いのか、掘り下げます。

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
    auto tim = std::make_shared<as::steady_timer>(exe);
    int result = a + b;
    // 足し算の結果ms ほど経過してからコールバック
    tim->expires_after(std::chrono::milliseconds(result));
    tim->async_wait(
        [result, cb, tim] (boost::system::error_code const& /*ec*/) {  
            cb(result); 
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

                // yieldを {} で囲む。
                yield {                
                    // タイムアウトが 1+2=3msよりも大きいかどうかで
                    // 足し算結果とtimeout表示のどちらが先になるかが変わる。
                    ma_.tim_.expires_after(std::chrono::milliseconds(4));
                    // 非同期関数呼び出しその1(タイムアウトタイマ)
                    ma_.tim_.async_wait(*this);
                    // 非同期関数呼び出しその2(足し算)
                    async_add(ma_.exe_, 1, 2, *this);
                }
                // yield{} 内部で2回非同期関数を呼び出しているので、
                // この行は2回実行される
                std::cout << "passing this line 2 times" << std::endl;
                if (ec) { 
                    // ecが設定されているということはタイムアウトタイマの結果
                    std::cout << "timeout" << std::endl;
                }
                else if (result) {
                    // ecが設定されているということはタイムアウトタイマの結果
                    std::cout << "async_add completed result is " << *result << std::endl;
                }
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
https://godbolt.org/z/3nacPeaKr

```
operator()()
start
operator()(int)
passing this line 2 times
async_add completed result is 3
operator()(boost::system::error_code const&)
passing this line 2 times
timeout
```

## 詳細

### 足し算の結果ms待つ非同期関数。

```cpp
template <typename Executor, typename Cb>
void async_add(Executor exe, int a, int b, Cb cb) {
    auto tim = std::make_shared<as::steady_timer>(exe);
    int result = a + b;
    // 足し算の結果ms ほど経過してからコールバック
    tim->expires_after(std::chrono::milliseconds(result));
    tim->async_wait(
        [result, cb, tim] (boost::system::error_code const& /*ec*/) {  
            cb(result); 
        }
    );
}
```

これまでも例に用いてきたasync_addの振る舞いを少し変更します。
引数で渡されたa+bを行い、その結果msほど待ってからコールバックを呼び出します。
結果を得るまでに時間のかかる非同期処理の模倣実装と考えてください。
(結果が負になる場合の考慮は行っていません。)

### reenter内で、yield{}を用いる。

```cpp
reenter(coro_) {
    // ここに非同期処理の連鎖を書いていく
    std::cout << "start" << std::endl;

    // yieldを {} で囲む。
    yield {                
        // タイムアウトが 1+2=3msよりも大きいかどうかで
        // 足し算結果とtimeout表示のどちらが先になるかが変わる。
        ma_.tim_.expires_after(std::chrono::milliseconds(4));
        // 非同期関数呼び出しその1(タイムアウトタイマ)
        ma_.tim_.async_wait(*this);
        // 非同期関数呼び出しその2(足し算)
        async_add(ma_.exe_, 1, 2, *this);
    }
    // yield{} 内部で2回非同期関数を呼び出しているので、
    // この行は2回実行される
    std::cout << "passing this line 2 times" << std::endl;
    if (ec) { 
        // ecが設定されているということはタイムアウトタイマの結果
        std::cout << "timeout" << std::endl;
    }
    else if (result) {
        // ecが設定されているということはタイムアウトタイマの結果
        std::cout << "async_add completed result is " << *result << std::endl;
    }
}
```

reenterの中で、`yield{}`というスコープ付きyieldを用います。
この中では、複数回非同期関数を呼び出すことができます。
async_addが4ms以内に終わるかチェックするタイマをセットし、`ma_.tim_.async_wait(*this);`しているのが、1つ目の非同期関数呼び出しです。
`async_add(ma_.exe_, 1, 2, *this);`呼び出しが2つ目の非同期関数呼び出しです。結果を得るのに1+2=3msかかります。
このように2つの非同期関数が呼ばれ、どちらが先に完了するかは引数によって変わります。

この場合、`yiled{}`の次の行、すなわち`std::cout << "passing this line 2 times" << std::endl;`以降がそれぞれの非同期関数が完了する度に実行されます。出力に`passing this line 2 times`が2回現れていることを確認してください。
あとは、引数`ec`,`result`どちらが設定されているか調べれば、タイムアウトが発生したのか、足し算結果が返ってきたのか区別できるので、それに応じて処理を実装することができます。
ちなみに、タイムアウトの値を4msから2msに変更すると、出力は以下のように変わります。

https://godbolt.org/z/nsT7rMMqq

```
operator()()
start
operator()(boost::system::error_code const&)
passing this line 2 times
timeout
operator()(int)
passing this line 2 times
async_add completed result is 3
```

タイムアウトが先に発生していることが分かるでしょう。

### 備考
#### タイマのキャンセルについて
本記事では、複数イベントのキックの方法と、その待ち方にフォーカスして説明を行いました。
よって、タイムアウトが発生したときの足し算処理のキャンセルや、足し算の結果が返ってきたときのタイムアウトタイマキャンセルなどは行っていません。
キャンセル処理は意外と奥が深く、イベント発火とキャンセルのすれ違い(ほとんど同時に発生した場合の振る舞い)対応など、考慮すべき点があるので、別の機会に掘り下げたいと思います。
すぐに知りたい方は私が2017年にstackoverflowに投稿した質問と回答(質問内に自ら回答)をご覧ください。
https://stackoverflow.com/questions/43045192/how-to-avoid-firing-already-destroyed-boostasiodeadline-timer

##### 追記

本件に関する記事を書きました。
https://zenn.dev/redboltz/articles/timer-cpp-timing
