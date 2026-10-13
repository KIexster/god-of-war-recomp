// GOW-Port: transporte propio de bytes GIF para la interfaz Transfer de PCSX2.
// No decodifica GIF ni sustituye el renderer. El consumidor se ejecuta antes
// de retornar: no puede retener el puntero a la copia alineada.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace gow_pcsx2_gs
{
    enum class Path : unsigned { Path1=1, Path2=2, Path3=3 };
    struct Sink {
        void *opaque=nullptr;
        // Cantidad en quadwords de 16 bytes, como GSState::Transfer / GSgifTransfer2/3.
        bool (*transfer)(void *,Path,const uint8_t *,uint32_t)=nullptr;
    };

    class GifBridge {
        struct Fragment { std::array<uint8_t,16> bytes{}; size_t size=0; };
        std::array<Fragment,3> fragments{};
        Sink sink;
        bool failed=false;
        static constexpr size_t capacity=4096;
        static bool validPath(Path path) { const auto n=unsigned(path); return n>=1 && n<=3; }
        bool emit(Path path,const uint8_t *data,size_t bytes) {
            alignas(16) std::array<uint8_t,capacity> aligned;
            std::memcpy(aligned.data(),data,bytes);
            if(!sink.transfer(sink.opaque,path,aligned.data(),uint32_t(bytes/16))) {
                // El destino puede haber consumido un prefijo. No reintentar ni
                // seguir como si renderer y emisor conservaran el mismo estado.
                failed=true; return false;
            }
            return true;
        }
    public:
        explicit GifBridge(Sink target):sink(target) {}
        GifBridge(const GifBridge&)=delete;
        GifBridge &operator=(const GifBridge&)=delete;
        bool submit(Path path,const uint8_t *data,size_t size) {
            if(!validPath(path) || (!data && size) || !sink.transfer || failed) return false;
            auto &pending=fragments[unsigned(path)-1];
            if(pending.size) {
                const auto take=std::min(16-pending.size,size);
                if(take) std::memcpy(pending.bytes.data()+pending.size,data,take);
                pending.size+=take;
                if(take) data+=take;
                size-=take;
                if(pending.size<16) return true;
                pending.size=0;
                if(!emit(path,pending.bytes.data(),16)) return false;
            }
            while(size>=16) {
                const auto bytes=std::min(size-size%16,capacity);
                if(!emit(path,data,bytes)) return false;
                data+=bytes; size-=bytes;
            }
            if(size) {
                std::memcpy(pending.bytes.data(),data,size);
                pending.size=size;
            }
            return true;
        }
        size_t pendingBytes(Path path) const {
            return validPath(path)?fragments[unsigned(path)-1].size:0;
        }
        // Solo comprobar al cerrar el flujo completo; VSync no termina un paquete GIF.
        bool complete() const {
            if(failed || !sink.transfer) return false;
            for(const auto &p:fragments) if(p.size) return false;
            return true;
        }
        // El propietario debe resetear también el GS real antes de descartar fragmentos.
        void discardAfterGsReset() { fragments={}; failed=false; }
    };
}
