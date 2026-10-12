// GOW-Port: cola acotada, con copia propia y cierre sin descartar el prefijo.
#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>
namespace gow_pcsx2_client {
template<class T> class Queue {
    struct Item {T value;size_t bytes;};
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Item> items;
    size_t used=0;
    const size_t limit;
    bool closed=false;
public:
    explicit Queue(size_t budget):limit(budget) {}
    bool push(T value,size_t bytes) {
        if(!bytes || bytes>limit)return false;
        std::unique_lock lock(mutex);
        changed.wait(lock,[&]{return closed || bytes<=limit-used;});
        if(closed)return false;
        items.push_back({std::move(value),bytes});used+=bytes;
        changed.notify_all();return true;
    }
    bool pop(T& value) {
        std::unique_lock lock(mutex);
        changed.wait(lock,[&]{return closed || !items.empty();});
        if(items.empty())return false;
        auto item=std::move(items.front());items.pop_front();used-=item.bytes;
        value=std::move(item.value);changed.notify_all();return true;
    }
    void close() {std::lock_guard lock(mutex);closed=true;changed.notify_all();}
};
}
