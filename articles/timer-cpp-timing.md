---
title: "タイマのすれ違い発火に備える"
emoji: "⏰"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [boost,asio,timer,async]
published: false
---

# タイマの発火とキャンセルが同時に発生した場合の振る舞い

## 問題が生じるコード
```cpp
#include <iostream>
#include <memory>
#include <map>
#include <boost/asio.hpp>

namespace as = boost::asio;
// steady_timer with index ctor/dtor print
struct debug_tim : as::steady_timer {
    debug_tim(as::io_context& ioc, int i) : as::steady_timer{ioc}, i{i} {
        std::cout << " debug_tim() " << i << std::endl;
    }
    ~debug_tim() {
        std::cout << "~debug_tim() " << i << std::endl;
    }
    int i;
};

int main() {
    constexpr int max = 5;
    as::io_context ioc;
    std::map<int, std::shared_ptr<debug_tim>> timers;
    for (int i = 0; i != max; ++i) {
        auto tim = std::make_shared<debug_tim>(ioc, i);
        std::cout << " set timer   " << i << std::endl;
        tim->expires_after(std::chrono::seconds(1));
        timers.emplace(i, tim);
        tim->async_wait(
            [&timers, i]
            (boost::system::error_code const& ec) {
                std::cout << " timer fired " << i << " : " <<  ec.message() << std::endl;
                auto it = timers.find(i);
                if (it == timers.end()) {
                    std::cout << "  already destructed." << std::endl;
                }
                else {
                    // erase (and cancel) other timer
                    int other_idx = (i + 1) % max;
                    std::cout << "  erase " << other_idx << std::endl;
                    timers.erase(other_idx);
                }
            }
        );
    }
    ioc.run();
}

```

## 実行結果(一例)

godboltでの実行:
https://godbolt.org/z/q5Mn7h4M5

```
 debug_tim() 0
 set timer   0
 debug_tim() 1
 set timer   1
 debug_tim() 2
 set timer   2
 debug_tim() 3
 set timer   3
 debug_tim() 4
 set timer   4
 timer fired 0 : Success
  erase 1
~debug_tim() 1
 timer fired 1 : Success
  already destructed.
 timer fired 2 : Success
  +erase 3
~debug_tim() 3
 timer fired 3 : Operation canceled
  already destructed.
 timer fired 4 : Success
  erase 0
~debug_tim() 0
~debug_tim() 4
~debug_tim() 2
```

## 詳細

```cpp
// steady_timer with index ctor/dtor print
struct debug_tim : as::steady_timer {
    debug_tim(as::io_context& ioc, int i) : as::steady_timer{ioc}, i{i} {
        std::cout << " debug_tim() " << i << std::endl;
    }
    ~debug_tim() {
        std::cout << "~debug_tim() " << i << std::endl;
    }
    int i;
};
```

まず、タイマとして、`steady_timer`を利用しています。これを、`debug_tim`が継承することで番号を付け、生成と破棄のメッセージを番号付きで、コンストラクタとデストラクタで出力しています。

```cpp
std::map<int, std::shared_ptr<debug_tim>> timers;
```

タイマはshared_ptrとして作成し、番号をkeyとしたmapで管理することにします。

```cpp
auto tim = std::make_shared<debug_tim>(ioc, i);
std::cout << " set timer   " << i << std::endl;
tim->expires_after(std::chrono::seconds(1));
timers.emplace(i, tim);
tim->async_wait(
```

タイマを作成したら1秒後に発火するようにセットして、mapに番号とともに登録します。そして発火を非同期待ちします。

```
 debug_tim() 0
 set timer   0
 debug_tim() 1
 set timer   1
 debug_tim() 2
 set timer   2
 debug_tim() 3
 set timer   3
 debug_tim() 4
 set timer   4
```

この出力から分かるように、番号0-4の5つのタイマを、全て1秒後に発火するように連続でセット、登録しています。

```cpp
std::cout << " timer fired " << i << " : " <<  ec.message() << std::endl;
```

非同期待ちが終わったら、番号と結果メッセージを表示します。結果メッセージは、タイマが正常に発火した場合`Success`、キャンセルされた場合`Operation canceled`となります。

```cpp
auto it = timers.find(i);
if (it == timers.end()) {
    std::cout << "  already destructed." << std::endl;
}
else {
    // erase (and cancel) other timer
    int other_idx = (i + 1) % max;
    std::cout << "  erase " << other_idx << std::endl;
    timers.erase(other_idx);
}
```

そして、番号をキーにmapを検索し、見つかった場合は隣の(ひとつ番号の大きい)タイマをmapから削除します。すると、debug_timのデストラクタが呼ばれ、steady_timerはキャンセルされます。この削除は、検証目的で行っています。全てのタイマは1秒に設定されているので、発火したとき隣のタイマを削除すると、発火とキャンセルがほぼ同時に起こるはずなので、そのときの動作を観測しようというわけです。
このとき、`erase 1` のように削除するタイマを出力しています。

```
 timer fired 0 : Success
  erase 1
~debug_tim() 1
```

出力を確認すると、タイマ0が発火し、となりのタイマ1を削除(タイマはキャンセルされる)しています。

```
 timer fired 1 : Success
  already destructed.
```

その直後の出力に注目してください。今まさに削除したタイマ1が発火しています。結果メッセージが`Success`であることから、キャンセルされず発火したことが分かります。
mapに要素は無いため、`if (it == timers.end()) {`の条件が成立し、`already destructed`と出力されています。
これは多くの場合、実装者の期待とは異なる振る舞いではないかと思います。削除によってキャンセルされたタイマが発火しているわけですから。例えば、タイマ削除と共に、発火
時にアクセスするであろうリソースも解放し、その後の期待と異なる発火で、解放されたリソースにアクセスしてUndefined Behaviorに陥るということが発生するかも知れません。
なお今回は、検証目的で、タイマを設定する際常にmapに登録しています。実用的なコードでは、mapを検索して存在を確認するというのは非効率で、マルチスレッドの場合はロックも必要になるかもしれず、良い解決策とはいえません。

```
 timer fired 2 : Success
  +erase 3
~debug_tim() 3
 timer fired 3 : Operation canceled
  already destructed.
```

その後の出力も見ていきましょう。
今度は、タイマ2が発火して、となりのタイマ3を削除しています。その後、タイマ3の非同期ハンドラが呼ばれていますが、今度は、結果メッセージが`Operation canceled`となっているので、これは期待通りの振る舞いといえるでしょう。
ただし、先ほどタイマ1のケースで見たように、常に`Operation canceled`になるわけではなく、タイミングによっては`Success`になることもあります。これは、**タイマが発火したかキャンセルされたかを、`ec`を確認するだけでは判断できない**ことを意味します。

なぜこのような振る舞いになるのでしょうか?Boost.Asioのバグでしょうか?
違います。これを理解するには、Boost.Asioのio_contextのメカニズムを知る必要があります。

![](/images/timer.svg)

## 解決策を適用したコード

