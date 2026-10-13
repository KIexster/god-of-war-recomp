// GOW-Port: datos copiados, FIFO entre eventos, presión y cierre con drenaje.
#include "../src/gow_gs_pcsx2_queue.h"
#include <chrono>
#include <future>
#include <cstdio>
#include <vector>
int main(){
    using namespace gow_pcsx2_client;
    Queue<std::vector<unsigned>> queue(8);
    std::vector<unsigned> borrowed{1,2};
    if(!queue.push(borrowed,8))return 1;
    borrowed[0]=99;
    auto producer=std::async(std::launch::async,[&]{return queue.push({3,4},8);});
    if(producer.wait_for(std::chrono::milliseconds(20))!=std::future_status::timeout)return 1;
    std::vector<unsigned> result;
    if(!queue.pop(result) || result!=std::vector<unsigned>{1,2})return 1;
    if(!producer.get())return 1;
    queue.close();
    if(!queue.pop(result) || result!=std::vector<unsigned>{3,4})return 1;
    if(queue.pop(result) || queue.push({5},4))return 1;
    Queue<unsigned> blocked(1);
    if(blocked.push(1,2) || !blocked.push(1,1))return 1;
    auto pending=std::async(std::launch::async,[&]{return blocked.push(2,1);});
    blocked.close();
    if(pending.get())return 1;
    unsigned value=0;if(!blocked.pop(value) || value!=1 || blocked.pop(value))return 1;
    std::puts("Cola GS: copia, orden, presión, rechazo de exceso y cierre con drenaje correctos");
}
