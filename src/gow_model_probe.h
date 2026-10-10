// GOW-Port: muestreo y lectura de modelos retail SCUS-97399, sin escribir RAM.
#pragma once
#include "gow_camera_probe.h"
#include <array>
#include <chrono>
namespace gow_model_probe {
    struct PhaseSamples {
        std::array<unsigned,3> counts{};
        unsigned take(uint32_t state) {
            auto &n=counts[state==4u ? 1u : state==11u ? 2u : 0u];
            return n<64u ? ++n : 0u;
        }
    };
    // Cada fase tiene su propia tabla: ni los objetos ni las vistas del menú
    // consumen las plazas de la intro. Tiempo explícito para probar los límites.
    struct Timeline {
        using Clock=std::chrono::steady_clock;
        struct Item { uint32_t model=0,view=0; unsigned count=0; Clock::time_point next{}; };
        std::array<std::array<Item,64>,3> phases{};
        unsigned take(uint32_t model,uint32_t view,uint32_t state,Clock::time_point now) {
            model &= 0x1FFFFFFFu; view &= 0x1FFFFFFFu;
            if(!model) return 0u;
            auto &items=phases[state==4u ? 1u : state==11u ? 2u : 0u];
            Item *selected=nullptr;
            for(auto &item:items) if(item.model==model && item.view==view) { selected=&item; break; }
            if(!selected) for(auto &item:items) if(!item.model) {
                selected=&item; item.model=model; item.view=view; break;
            }
            if(!selected || selected->count>=32u || now<selected->next) return 0u;
            selected->next=now+std::chrono::seconds(2);
            return ++selected->count;
        }
    };
    struct Pose {
        bool validObject=false,validSkeleton=false,validRoot=false;
        uint32_t skeleton=0,joints=0,jointCount=0,rootAddress=0;
        uint16_t joint=0,rootId=0;
        uint64_t objectTick=0,skeletonTick=0;
        std::array<uint32_t,16> local{},world{},rootWorld{};
        struct Joint { bool valid=false; uint32_t address=0; std::array<uint32_t,16> world{}; };
        // Primeras ocho articulaciones: lectura acotada, sin copiar toda la paleta.
        std::array<Joint,8> firstJoints{};
    };
    // GOW-Port: límite de salida del diagnóstico, no límite de la jerarquía del juego.
    struct PaletteRead { uint32_t read=0; bool complete=false; };
    template<class Visitor> inline PaletteRead visitPalette(const uint8_t *ram,size_t size,
        uint32_t palette,uint32_t count,Visitor visitor) {
        using namespace gow_camera_probe;
        PaletteRead result;
        for(uint32_t i=0;i<count && i<256u;++i) {
            if(!palette) break;
            const uint64_t at=(palette & 0x1FFFFFFFu)+uint64_t(i)*64u;
            if(at>=0x20000000ull) break;
            const auto *matrix=span(ram,size,uint32_t(at),64u);
            if(!matrix) break;
            Pose::Joint joint; joint.valid=true; joint.address=uint32_t(at);
            std::memcpy(joint.world.data(),matrix,64u);
            visitor(i,joint); ++result.read;
        }
        result.complete=result.read==count;
        return result;
    }
    inline Pose capture(const uint8_t *ram,size_t size,uint32_t object) {
        using namespace gow_camera_probe;
        Pose p;
        const auto *o=span(ram,size,object,0x108u);
        if(!o) return p;
        p.validObject=true;
        // CalcWorldMatrix 0x1303D0: matriz local +0x20, mundo +0x70,
        // articulación +0x60 y sello +0x68; aquí no se fuerza su actualización.
        p.joint=read<uint16_t>(o+0x60); p.objectTick=read<uint64_t>(o+0x68);
        std::memcpy(p.local.data(),o+0x20,64); std::memcpy(p.world.data(),o+0x70,64);
        p.skeleton=read<uint32_t>(o+0x104);
        const auto *s=span(ram,size,p.skeleton,0x90u);
        if(!s) return p;
        p.validSkeleton=true; p.rootId=read<uint16_t>(s+0x86);
        p.skeletonTick=read<uint64_t>(s+0x40); p.joints=read<uint32_t>(s+0x8C);
        const auto *definition=span(ram,size,read<uint32_t>(s+0x60),0x14u);
        if(!definition) return p;
        // CalcSkinHierarchy 0x137558 toma el número de articulaciones de +0x10;
        // CalcWorldMatrix 0x1306E4 indexa matrices de 64 bytes desde skeleton+0x8C.
        p.jointCount=read<uint32_t>(definition+0x10);
        if(p.joints) for(size_t i=0;i<p.firstJoints.size() && i<p.jointCount;++i) {
            const uint64_t at=(p.joints & 0x1FFFFFFFu)+uint64_t(i)*64u;
            if(at>=0x20000000ull) break;
            const auto *matrix=span(ram,size,uint32_t(at),64);
            if(!matrix) break;
            auto &joint=p.firstJoints[i]; joint.valid=true; joint.address=uint32_t(at);
            std::memcpy(joint.world.data(),matrix,64);
        }
        if(!p.joints || p.rootId>=p.jointCount) return p;
        const uint64_t address=(p.joints & 0x1FFFFFFFu)+uint64_t(p.rootId)*64u;
        if(address>=0x20000000ull) return p;
        const auto *root=span(ram,size,uint32_t(address),64);
        if(!root) return p;
        p.validRoot=true; p.rootAddress=uint32_t(address);
        std::memcpy(p.rootWorld.data(),root,64);
        return p;
    }
}
