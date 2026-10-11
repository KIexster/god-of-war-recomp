// GOW-Port: entradas procedurales, sin datos del juego ni reloj real.
#include "../src/gow_pad_script.h"
#include "../src/gow_pad2_packet.h"
#include <array>
#include <cstdio>

bool test() {
    using namespace gow_pad_script;
    const auto sample=[](const std::vector<Press> &script,double seconds) {
        uint8_t state[32]={1,0x73,0xFF,0xFF,128,128,128,128};
        apply(script,seconds,state);
        return gow_pad2::makePacket(state);
    };
    // La compatibilidad de botones usa todos los bits del paquete original.
    constexpr const char *names[]={"select","start","arriba","derecha","abajo","izquierda",
        "l2","r2","l1","r1","triangulo","circulo","x","cuadrado"};
    constexpr uint16_t bits[]={0x0001,0x0008,0x0010,0x0020,0x0040,0x0080,
        0x0100,0x0200,0x0400,0x0800,0x1000,0x2000,0x4000,0x8000};
    for(size_t i=0;i<std::size(names);++i) {
        const auto script=parse(std::string("5:")+names[i]);
        if(script.size()!=1 || script[0].duration!=0.7) return false;
        const auto packet=sample(script,5.2);
        // CONCAT11 del juego coloca primero el byte bajo del estado host.
        const auto host=uint16_t(packet[0] | (uint16_t(packet[1])<<8));
        if(host!=uint16_t(0xFFFFu^bits[i])) return false;
        for(size_t k=2;k<6;++k) if(packet[k]!=128) return false;
        if(sample(script,4.999)!=sample({},0) || sample(script,5.7)!=sample({},0)) return false;
    }
    if(sample(parse("12:x"),12.7)!=sample({},0)) return false;
    // D-pad y stick no se confunden; duración explícita se libera al terminar.
    const auto script=parse("10:mover_izquierda:3,11:mover_arriba:1,11:cuadrado:0.5");
    const auto neutral=sample({},0), left=sample(script,10), combined=sample(script,11.1);
    if(left[0]!=255 || left[1]!=255 || left[4]!=0 || left[5]!=128) return false;
    if(combined[4]!=0 || combined[5]!=0 || combined[1]!=127 || combined[13]!=255) return false;
    if(sample(script,12)[4]!=0 || sample(script,12)[5]!=128 || sample(script,13)!=neutral) return false;
    if(sample(parse("10:izquierda:3"),11)[4]!=128) return false;
    if(sample(parse("1:mover_derecha,1:mover_abajo"),1.1)[4]!=255 ||
       sample(parse("1:mover_derecha,1:mover_abajo"),1.1)[5]!=255) return false;
    // En un mismo eje prevalece la última entrada activa; no afecta al otro.
    if(sample(parse("1:mover_izquierda:4,2:mover_derecha:1"),2.2)[4]!=255 ||
       sample(parse("1:mover_izquierda:4,2:mover_derecha:1"),3.2)[4]!=0) return false;
    if(!parse("nan:x,inf:x,-1:x,1oops:x,1:x:0,1:x:-2,1:x:nan,1:x:inf,1:x:2oops,1:x:1:2,1:unknown,:x,1:").empty()) return false;
    if(parse(",5:x,,7:mover_abajo:2,").size()!=2) return false;
    // Aplicar el guion modifica solo las entradas activas, conserva el host.
    uint8_t state[32]={1,0x73,0xEF,0xFF,12,34,56,78};
    apply(parse("1:mover_derecha,1:x"),1.2,state);
    if(state[0]!=1 || state[1]!=0x73 || state[2]!=0xEF || state[3]!=0xBF ||
       state[4]!=12 || state[5]!=34 || state[6]!=255 || state[7]!=78) return false;
    return true;
}
int main() {
    if(!test()) { std::fputs("Fallo del guion de entradas\n",stderr); return 1; }
    std::puts("Guion digital/analogico y duraciones: correcto"); return 0;
}
