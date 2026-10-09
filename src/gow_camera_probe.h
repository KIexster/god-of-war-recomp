// GOW-Port: lectura opcional del estado de cámara retail SCUS-97399.
// Offsets comprobados en MIPS 0x1697F0: renView+0/0x40/0x100/0x240,
// lista en +0x360 y camera::Client en nodo+8. No interpreta tipos Ghidra.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gow_camera_probe {
    struct Snapshot {
        bool validView=false, validNode=false, validCamera=false, empty=false;
        uint32_t node=0, camera=0;
        uint16_t joint=0;
        uint64_t matrixTick=0, inverseTick=0;
        std::array<uint32_t,16> world{}, inverse{}, projection{}, combined{};
        std::array<uint32_t,16> clientWorld{}, clientInverse{};
        bool validParent=false, validParentSkeleton=false, validParentJoint=false;
        uint32_t parent=0, parentSkeleton=0, parentJoints=0, parentJointAddress=0;
        uint64_t parentTick=0, parentSkeletonTick=0;
        std::array<uint32_t,16> parentJointWorld{};
    };
    inline const uint8_t *span(const uint8_t *ram,size_t size,uint32_t address,size_t count) {
        const uint32_t physical=address & 0x1FFFFFFFu;
        if(!ram || !address || physical>=size || count>size-physical) return nullptr;
        return ram+physical;
    }
    template<class T> inline T read(const uint8_t *p) {
        T result{}; std::memcpy(&result,p,sizeof(result)); return result;
    }
    inline Snapshot capture(const uint8_t *ram,size_t size,uint32_t view) {
        Snapshot s;
        const auto *v=span(ram,size,view,0x3B0u);
        if(!v) return s;
        s.validView=true;
        std::memcpy(s.inverse.data(),v,64);
        std::memcpy(s.combined.data(),v+0x40,64);
        std::memcpy(s.projection.data(),v+0x100,64);
        std::memcpy(s.world.data(),v+0x240,64);
        s.node=read<uint32_t>(v+0x360);
        s.empty=s.node==view+0x360u; // beq compara direcciones completas, no alias físicos.
        if(s.empty) return s;
        const auto *node=span(ram,size,s.node,12);
        if(!node) return s;
        s.validNode=true; s.camera=read<uint32_t>(node+8);
        const auto *client=span(ram,size,s.camera,0x108u);
        if(!client) return s;
        s.validCamera=true; s.joint=read<uint16_t>(client+0x60);
        s.matrixTick=read<uint64_t>(client+0x68);
        s.inverseTick=read<uint64_t>(client+0xF0);
        // lh == -1 selecciona matriz local; las demás articulaciones usan +0x70.
        std::memcpy(s.clientWorld.data(),client+(s.joint==0xFFFFu ? 0x20 : 0x70),64);
        std::memcpy(s.clientInverse.data(),client+0xB0,64);
        // CalcWorldMatrix 0x1303D0: el cliente articula el array de su padre (+0x18).
        // La jerarquía de cámara puede tener count de skin=0: no usarlo de límite.
        s.parent=read<uint32_t>(client+0x18);
        const auto *parent=span(ram,size,s.parent,0x108u);
        if(!parent) return s;
        s.validParent=true; s.parentTick=read<uint64_t>(parent+0x68);
        s.parentSkeleton=read<uint32_t>(parent+0x104);
        const auto *skeleton=span(ram,size,s.parentSkeleton,0x90u);
        if(!skeleton) return s;
        s.validParentSkeleton=true; s.parentSkeletonTick=read<uint64_t>(skeleton+0x40);
        s.parentJoints=read<uint32_t>(skeleton+0x8C);
        if(!s.parentJoints || s.joint==0xFFFFu) return s;
        const uint64_t address=(s.parentJoints & 0x1FFFFFFFu)+uint64_t(s.joint)*64u;
        if(address>=0x20000000ull) return s;
        const auto *joint=span(ram,size,uint32_t(address),64);
        if(!joint) return s;
        s.validParentJoint=true; s.parentJointAddress=uint32_t(address);
        std::memcpy(s.parentJointWorld.data(),joint,64);
        return s;
    }
}
