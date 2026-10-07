// GOW-Port: el dump procedural de PCSX2 debe reproducir la captura CPU a través de GIF PATH3.
#include "../src/gow_gs_replay.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <algorithm>
#include <iostream>
#include <span>

namespace {
    struct DumpReader {
        std::vector<uint8_t> data;
        size_t offset=0;
        bool good=true;
        std::span<const uint8_t> take(size_t count) {
            if(!good || count>data.size()-offset) {good=false;return {};}
            const auto result=std::span<const uint8_t>(data).subspan(offset,count);
            offset+=count;return result;
        }
        uint32_t number(unsigned bytes) {
            const auto value=take(bytes); uint32_t result=0;
            for(unsigned i=0;i<value.size();++i) result|=uint32_t(value[i])<<(i*8);
            return result;
        }
    };

    bool verify(const std::filesystem::path &directory,const char *variant) {
        const auto name=std::string("feedback_gs_")+variant;
        gow_gs_replay::Reader capture(directory/(name+".bin"));
        gow_gs_replay::Record record; gow_gs_replay::Snapshot initial,expected;
        bool hasInitial=false,hasEnd=false;
        while(capture.next(record)) {
            if(record.op==gow_gs_replay::Op::Initial) hasInitial=gow_gs_replay::decodeSnapshot(record,initial);
            if(record.op==gow_gs_replay::Op::End) hasEnd=gow_gs_replay::decodeSnapshot(record,expected);
        }
        if(!hasInitial || !hasEnd || !capture.error.empty()) return false;
        std::ifstream input(directory/(name+".gs"),std::ios::binary|std::ios::ate);
        if(!input || input.tellg()<0 || input.tellg()>5*1024*1024) return false;
        DumpReader dump;dump.data.resize(size_t(input.tellg()));input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(dump.data.data()),dump.data.size())) return false;
        if(dump.number(4)!=0 || dump.number(4)!=PS2_GS_VRAM_SIZE+448) return false;
        const auto frozen=dump.take(PS2_GS_VRAM_SIZE+448);
        if(!dump.good || frozen[0]!=8 || frozen[1] || frozen[2] || frozen[3] ||
           !std::equal(initial.vram.begin(),initial.vram.end(),frozen.begin()+364)) return false;
        const auto registers=dump.take(8192);
        if(dump.number(1)!=0 || dump.number(1)!=3) return false;
        const auto packetSize=dump.number(4);
        if(!packetSize || packetSize%16 || packetSize>1024*1024) return false;
        const auto packet=dump.take(packetSize);
        for(unsigned field=0;field<2;++field) {
            if(dump.number(1)!=3) return false;
            const auto repeated=dump.take(8192);
            if(!dump.good || !std::equal(registers.begin(),registers.end(),repeated.begin()) ||
               dump.number(1)!=1 || dump.number(1)!=field) return false;
        }
        if(!dump.good || dump.offset!=dump.data.size()) return false;
        auto vram=initial.vram;
        GS gs;gs.setRasterBackend(std::make_unique<GSCpuBackend>());
        gs.init(vram.data(),uint32_t(vram.size()));
        std::copy(initial.vram.begin(),initial.vram.end(),vram.begin());
        gs.processGIFPacket(packet.data(),packetSize,GifPathId::Path3);
        size_t different=0;
        for(size_t i=0;i<vram.size();++i) different+=vram[i]!=expected.vram[i];
        const bool pending=gs.hasPendingGIFPacket(GifPathId::Path3);
        std::cout<<variant<<": GS freeze inicial exacto; GIF vs End CPU bytes="<<different
                 <<" pending="<<pending<<'\n';
        return different==0 && !pending;
    }
}

#ifdef _WIN32
int wmain(int argc,wchar_t **argv)
#else
int main(int argc,char **argv)
#endif
{
    if(argc!=2) return 2;
    for(const char *variant:{"self","disjoint","nearest"})
        if(!verify(std::filesystem::path(argv[1]),variant)) return 1;
    return 0;
}
