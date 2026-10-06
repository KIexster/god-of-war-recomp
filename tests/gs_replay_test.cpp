// GOW-Port: captura/repetición sintética; no depende del juego ni de OpenGL.
#include "../src/gow_gs_replay.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <iostream>

#ifdef _WIN32
int wmain(int argc,wchar_t **argv)
#else
int main(int argc,char **argv)
#endif
{
    if(argc!=2) return 2;
    const std::filesystem::path path=argv[1];
    std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE);
    gow_gs_replay::Backend capture(std::make_unique<GSCpuBackend>(),nullptr,path,0,3600);
    capture.Initialize(vram.data(),uint32_t(vram.size()));
    GSTransferCommand command{}; command.direction=0; command.bitbltbuf.dbw=1;
    command.bitbltbuf.dpsm=1; command.trxreg={2,1}; capture.BeginTransfer(command);
    const uint8_t a[]={0x12,0x34},b[]={0x56,0x78,0x9a,0xbc};
    capture.UploadImage(a,2); capture.UploadImage(b,4);
    GSPrimitiveBatch sprite{}; sprite.vertexCount=2; sprite.state.prim.type=GS_PRIM_SPRITE;
    sprite.state.context.frame.fbw=1; sprite.state.context.scissor={0,7,0,7};
    sprite.state.context.zbuf.zmask=true; sprite.state.context.test=1ull<<17; sprite.state.colclamp=1;
    sprite.vertices[0].r=0x11; sprite.vertices[0].g=0x22; sprite.vertices[0].b=0x33; sprite.vertices[0].a=0x80;
    sprite.vertices[1]=sprite.vertices[0]; sprite.vertices[1].x=4; sprite.vertices[1].y=4;
    capture.Submit(sprite); capture.TextureFlush();
    if(capture.ReadVram(0,0,1,1,1)!=0x80332211u || !capture.finish()) return 1;
    gow_gs_replay::Reader reader(path); gow_gs_replay::Record record;
    bool initial=false,ended=false; unsigned uploads=0,draws=0;
    while(reader.next(record)) {
        if(record.op==gow_gs_replay::Op::Initial || record.op==gow_gs_replay::Op::End) {
            gow_gs_replay::Snapshot state; if(!gow_gs_replay::decodeSnapshot(record,state)) return 1;
            if(record.op==gow_gs_replay::Op::Initial) initial=true; else ended=true;
            // El parser rechaza tamaños inconsistentes y bytes CT24 inválidos.
            auto bad=record; bad.data.pop_back(); if(gow_gs_replay::decodeSnapshot(bad,state)) return 1;
            gow_gs_replay::FixedState fixed{}; std::memcpy(&fixed,record.data.data(),sizeof(fixed));
            fixed.upload24.size=3; bad=record; std::memcpy(bad.data.data(),&fixed,sizeof(fixed));
            if(gow_gs_replay::decodeSnapshot(bad,state)) return 1;
        }
        if(record.op==gow_gs_replay::Op::Upload) ++uploads;
        if(record.op==gow_gs_replay::Op::Submit) ++draws;
    }
    if(!initial || !ended || uploads!=2 || draws!=1 || !reader.error.empty()) return 1;
    // Un archivo cortado no se puede certificar como una captura completa.
    auto truncated=path; truncated += ".truncated";
    std::ifstream original(path,std::ios::binary); std::vector<char> raw((std::istreambuf_iterator<char>(original)),{});
    { std::ofstream out(truncated,std::ios::binary); out.write(raw.data(),raw.size()-1); }
    { gow_gs_replay::Reader incomplete(truncated); while(incomplete.next(record)) {}
      if(incomplete.error.empty()) return 1; }
    std::filesystem::remove(truncated);
    std::cout<<"Captura sintética, parser y truncamiento: OK\n";
    return 0;
}
