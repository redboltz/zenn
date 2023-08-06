---
title: "g++ で C++20 coroutineに関するバグ"
emoji: "🔌"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [coroutine,async g++,gcc]
published: true
---

# バグの概要
```cpp
co_await async_func(std::vector<std::string>{"aaa", "bbb"}, handler)
```
のように、co_awaitする非同期関数の引数に、非PODのvectorの右辺値を渡すとinternal compiler errorが発生します。

## 確認に用いたコンパイラのバージョン
g++ 13.2.1 にて確認しました。

## 確認に用いたコード

```cpp
#include <vector>
#include <string>
#include <coroutine>
#include <boost/asio.hpp>

#define REPRODUCE_ISSUE 1

boost::asio::awaitable<void>
test1(boost::asio::io_context& ioc) {
    co_await boost::asio::post(
        ioc,
        boost::asio::append(
            boost::asio::use_awaitable,
            std::vector<int> {
                1, 2, 3
            }
        )
    );
}

boost::asio::awaitable<void>
test2(boost::asio::io_context& ioc) {
    std::vector<std::string> str{ "aaa", "bbb" };
    co_await boost::asio::post(
        ioc,
        boost::asio::append(
            boost::asio::use_awaitable,
#if REPRODUCE_ISSUE
            std::vector<std::string> {
                "aaa", "bbb"
            }
#else
            str
#endif
        )
    );
}

int main() {
    boost::asio::io_context ioc;
    boost::asio::co_spawn(ioc, test1(ioc), boost::asio::detached);
    boost::asio::co_spawn(ioc, test2(ioc), boost::asio::detached);
    ioc.run();
}
```

## エラー内容

```
cpp20coro_error.cpp: In function ‘boost::asio::awaitable<void> test2(boost::asio::io_context&)’:
cpp20coro_error.cpp:37:1: internal compiler error: in build_special_member_call, at cp/call.cc:11093
   37 | }
      | ^
0x1ad3438 internal_error(char const*, ...)
        ???:0
0x6b7b33 fancy_abort(char const*, int, char const*)
        ???:0
0x10dc453 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc453 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc573 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc453 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc453 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc453 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc573 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc453 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x10dc453 walk_tree_1(tree_node**, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*, tree_node* (*)(tree_node**, int*, tree_node* (*)(tree_node**, int*, void*), void*, hash_set<tree_node*, false, default_hash_traits<tree_node*> >*))
        ???:0
0x74de61 finish_function(bool)
        ???:0
0x944370 c_common_parse_file()
        ???:0
Please submit a full bug report, with preprocessed source (by using -freport-bug).
Please include the complete backtrace with any bug report.
See <https://bugs.archlinux.org/> for instructions.
```

## clang++での状況

x86-64 clang 16.0.0
-std=c++20
https://godbolt.org/z/qnWqrcPKc

エラーは発生しませんでした。

## g++ でもgodboltで確認

x86-64 clang 13.2
-std=c++20
https://godbolt.org/z/K74G65Yb1

エラーが発生します。

## バグレポート状況

https://gcc.gnu.org/bugzilla/show_bug.cgi?id=110913
報告したところ、dupulicatedでした。確認したつもりでしたが、調査が甘かったようです。

### duplicate元の状況
https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105574

次のリリースで修正されるかは分かりません。

2023/08/06 12:17 JSTにgodboltで確認した、x86-64-gcc (trunk)では、まだエラーが出ます。
https://godbolt.org/z/TEhK79c31

## 現在取れる対策

1. g++を使わずにclang++などを使う
2. g++を使う場合、変数を介して渡す(コードのstrのように)。std::move()して良いかは不明。実験したところ動いたが、rvalueを渡すと問題が発生するとの記載があり、xvalueでOKで, prvalueではNGという振る舞いが、たまたまなのかも知れないので、使わない方が無難。


