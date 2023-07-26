---
title: "Boost.Asioのstackless coroutineの基本"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,coroutine,async]
published: false
---

# 異なるコールバック引数への対応

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
         impl_{*this}
    {}

private:
    friend struct impl;
    // implをcopyableにして、非同期関数に *this を渡す。
    // 保持すべき情報は、外側クラスmy_appへの参照と、
    // as::coroutineのみ。
    // 必要なデータは、my_app側に足していく。
    struct impl {
        impl(my_app& ma)
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
            std::optional<boost::system::error_code> const& ec,
            std::optional<int> result
        ) const {
            reenter(coro_) {
                // ここに非同期処理の連鎖を書いていく
                std::cout << "start" << std::endl;
                
                // implオブジェクト自身を渡すことで、
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
    impl impl_;
};

#include <boost/asio/unyield.hpp>

int main() {
    as::io_context ioc;
    my_app ma{ioc.get_executor()};
    ioc.run();
}
```