// GOW-Port: lectura del destino real de skinning retail SCUS-97399.
// No ejecuta matemáticas del juego ni modifica memoria o registros.
#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gow_skin_probe {
    constexpr uint32_t maxSlots=256u;
    using Matrix=std::array<uint32_t,16>;
    struct Memory {
        const uint8_t *ram=nullptr, *scratch=nullptr;
        size_t ramSize=0, scratchSize=0;
        // Resolver una sola región y comprobar offset/rango antes de sumar.
        // Nunca usar RAM_MASK para convertir una dirección inválida en una válida.
        const uint8_t *at(uint32_t address,uint64_t offset,size_t count) const {
            if(!address) return nullptr;
            const uint8_t *base=nullptr;
            size_t size=0;
            uint32_t physical=0;
            const uint32_t lower=address & 0x7FFFFFFFu;
            if(lower>=0x70000000u && lower<0x70004000u) {
                base=scratch; size=scratchSize<0x4000u ? scratchSize : 0x4000u;
                physical=lower-0x70000000u;
            } else if(address<0x40000000u || (address>=0x80000000u && address<0xC0000000u)) {
                base=ram; size=ramSize<0x02000000u ? ramSize : 0x02000000u;
                physical=address & 0x1FFFFFFFu;
            } else return nullptr;
            if(!base || physical>=size || offset>size-physical || count>size-physical-size_t(offset))
                return nullptr;
            return base+physical+size_t(offset);
        }
        template<class T> bool read(uint32_t address,uint64_t offset,T &result) const {
            const auto *p=at(address,offset,sizeof(T));
            if(!p) return false;
            std::memcpy(&result,p,sizeof(T)); return true;
        }
        bool matrix(uint32_t address,uint32_t index,Matrix &result) const {
            const auto *p=at(address,uint64_t(index)*64u,64u);
            if(!p) return false;
            std::memcpy(result.data(),p,64u); return true;
        }
    };
    struct Layout {
        bool valid=false, selectionValid=false;
        uint16_t count=0;
        uint32_t records=0, source=0, visibility=0, definition=0, selectedCount=0, matrixOffset=0;
        std::array<uint32_t,8> selection{};
    };
    inline Layout layout(const Memory &memory,uint32_t skeleton) {
        Layout l;
        // MIPS 0x1375E8/EC, 0x137704/710 y variante 0x137B44/0x137CFC.
        // +0x5C limita slots de destino; definición+0x10 cuenta selección, no slots.
        if(!memory.read(skeleton,0x5Cu,l.count) || l.count>maxSlots ||
           !memory.read(skeleton,0x50u,l.source) || !memory.read(skeleton,0x54u,l.visibility) ||
           !memory.read(skeleton,0x58u,l.records) || !memory.read(skeleton,0x60u,l.definition)) return l;
        l.valid=true;
        if(memory.read(l.definition,0x10u,l.selectedCount) &&
           memory.read(l.definition,0xCu,l.matrixOffset) && l.matrixOffset>=32u) {
            const auto *p=memory.at(l.definition,uint64_t(l.matrixOffset)-32u,32u);
            if(p) { std::memcpy(l.selection.data(),p,32u); l.selectionValid=true; }
        }
        return l;
    }
    struct Slot {
        bool recordValid=false, maskValid=false, visibilityValid=false;
        int16_t indexField=0, parent=0;
        uint32_t flags=0, threshold=0, visibility=0;
        bool masked=false;
    };
    inline Slot slot(const Memory &memory,uint32_t skeleton,const Layout &l,uint32_t index) {
        Slot s;
        if(!l.valid || index>=l.count) return s;
        const auto *p=memory.at(l.records,uint64_t(index)*16u,16u);
        if(p) {
            s.recordValid=true;
            // GOW-Port: 0x1378AC/0x137EBC; bits 0x6000 llaman a la
            // transformación adicional de 0x136870 después del producto.
            std::memcpy(&s.flags,p,4);
            std::memcpy(&s.indexField,p+4,2); std::memcpy(&s.parent,p+8,2);
            std::memcpy(&s.threshold,p+12,4);
            // El juego indexa la máscara por signed lh; un índice negativo no se
            // convierte en lectura gigantesca o alias. Se informa como desconocido.
            if(s.parent>=0 && s.parent<int(maxSlots)) {
                uint32_t word=0;
                s.maskValid=memory.read(skeleton,0x64u+uint32_t(s.parent/32)*4u,word);
                s.masked=s.maskValid && ((word>>uint32_t(s.parent%32)) & 1u);
            }
        }
        if(l.visibility) s.visibilityValid=memory.read(l.visibility,uint64_t(index)*4u,s.visibility);
        return s;
    }
    inline bool selected(const Layout &l,uint32_t index) {
        return l.valid && l.selectionValid && index<l.count &&
               ((l.selection[index/32u]>>(index%32u)) & 1u);
    }
    // Las matrices de definición siguen el orden de bits activos, no index*64.
    // MIPS 0x137738/740 y 0x1377A8/B4 avanza el ordinal solo por selección.
    inline bool bind(const Memory &memory,const Layout &l,uint32_t index,Matrix &matrix,uint32_t &ordinal) {
        if(!selected(l,index) || !l.selectedCount || l.selectedCount>maxSlots) return false;
        ordinal=0;
        for(uint32_t i=0;i<index;++i) ordinal+=selected(l,i) ? 1u : 0u;
        return ordinal<l.selectedCount && memory.read(l.definition,uint64_t(l.matrixOffset)+uint64_t(ordinal)*64u,matrix);
    }
    struct Timeline {
        using Clock=std::chrono::steady_clock;
        struct Item { uint32_t skeleton=0,view=0,entry=0; unsigned count=0; Clock::time_point next{}; };
        std::array<Item,16> items{};
        unsigned total=0;
        unsigned take(uint32_t skeleton,uint32_t view,uint32_t entry,Clock::time_point now,
                      uint64_t videoTick=0,uint64_t fromTick=0) {
            // GOW-Port: esperar la ventana solicitada sin gastar el cupo en la intro.
            if(videoTick<fromTick || !skeleton || total>=128u) return 0;
            Item *found=nullptr;
            for(auto &item:items) if(item.skeleton==skeleton && item.view==view && item.entry==entry) {
                found=&item; break;
            }
            if(!found) for(auto &item:items) if(!item.skeleton) {
                found=&item; item.skeleton=skeleton; item.view=view; item.entry=entry; break;
            }
            if(!found || found->count>=32u || now<found->next) return 0;
            ++found->count; found->next=now+std::chrono::milliseconds(100);
            return ++total;
        }
    };
    // Las reanudaciones llaman al original. No guardar argumentos de otra llamada
    // ni interpretar una pausa (incluida una llamada anidada) como retorno completo.
    template<class Original,class Before,class After>
    inline void observe(uint32_t &pc,uint32_t entry,uint32_t caller,Original original,Before before,After after) {
        const bool started=pc==entry;
        if(started) before();
        original();
        if(started && pc==caller) after();
    }
}
