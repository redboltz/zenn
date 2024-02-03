---
title: "debug print時のthread識別"
emoji: "🧵"
type: "tech" # tech: 技術記事 / idea: アイデア
topics: [C++,thread,debug]
published: false
---

# Threadのdebug printに関するTips


```cpp
#include <iostream>
#include <syncstream>
#include <atomic>
#include <thread>
#include <vector>
#include <ranges>

constexpr std::size_t num_of_threads = 10;

std::atomic<int> tid_gen;
thread_local int tid = tid_gen++;

int main() {
    std::vector<std::thread> ths;
    ths.reserve(num_of_threads);
    for (auto _ : std::ranges::iota_view{std::size_t{0}, num_of_threads}) {
        ths.emplace_back(
            [&] {
                std::osyncstream(std::cout) << "runnning on thread " << tid << std::endl;
            }
        );
    }
    for (auto& th : ths) th.join();
}
```

godboltでの実行:
https://godbolt.org/z/x6Gczf8Ge

## インクリメンタルなID取得
`std::this_thread::get_id()`で、thread idは取得可能ですが、桁が多く、ぱっと見区別が付きにくいです。
そこで、以下のように、0からインクリメンタルにthread固有のidを生成することが可能です。

```cpp
std::atomic<int> tid_gen;
thread_local int tid = tid_gen++;
```

debug print時に、どのthreadで動いているかが簡単に出力できます。

## 入り交じらない出力

```cpp
#include <syncstream>
```

のようにincludeし、

```cpp
std::osyncstream(std::cout)
```

を使うことで、行が入り交じった出力になることを防げます。
単なる`std::cout`と置き換えてみると効果が分かるかと思います。
(複数回の試行を行わないと、入り交じらないかも知れません。)

