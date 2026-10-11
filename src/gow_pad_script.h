// GOW-Port: guion opcional de botones y stick izquierdo para controles funcionales.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace gow_pad_script {
    struct Press { double seconds=0, duration=0.7; uint8_t byte=0, value=0; bool analog=false; };
    inline bool number(std::string_view text,double &value) {
        if(text.empty()) return false;
        const std::string copy(text);
        char *end=nullptr;
        value=std::strtod(copy.c_str(),&end);
        return end==copy.c_str()+copy.size() && end!=copy.c_str() && std::isfinite(value);
    }
    inline std::vector<Press> parse(std::string_view text) {
        struct Button { std::string_view name; uint8_t byte, value; bool analog; };
        constexpr Button buttons[]={
            {"select",2,0x01,false},{"start",2,0x08,false},
            {"arriba",2,0x10,false},{"derecha",2,0x20,false},
            {"abajo",2,0x40,false},{"izquierda",2,0x80,false},
            {"l2",3,0x01,false},{"r2",3,0x02,false},
            {"l1",3,0x04,false},{"r1",3,0x08,false},
            {"triangulo",3,0x10,false},{"circulo",3,0x20,false},
            {"x",3,0x40,false},{"cuadrado",3,0x80,false},
            // Stick izquierdo en el estado anfitrión; no son bits del D-pad.
            {"mover_izquierda",6,0,true},{"mover_derecha",6,255,true},
            {"mover_arriba",7,0,true},{"mover_abajo",7,255,true}
        };
        std::vector<Press> result;
        while(!text.empty()) {
            const auto comma=text.find(',');
            const auto item=text.substr(0,comma);
            if(comma==std::string_view::npos) text={}; else text.remove_prefix(comma+1);
            const auto colon=item.find(':');
            if(colon==std::string_view::npos) continue;
            const auto rest=item.substr(colon+1);
            const auto durationColon=rest.find(':');
            const auto name=rest.substr(0,durationColon);
            Press press;
            if(!number(item.substr(0,colon),press.seconds) || press.seconds<0) continue;
            if(durationColon!=std::string_view::npos &&
               (!number(rest.substr(durationColon+1),press.duration) || press.duration<=0)) continue;
            for(const auto &button:buttons) if(name==button.name) {
                press.byte=button.byte; press.value=button.value; press.analog=button.analog;
                result.push_back(press); break;
            }
        }
        return result;
    }
    inline void apply(const std::vector<Press> &presses,double seconds,uint8_t (&state)[32]) {
        for(const auto &press:presses) if(seconds>=press.seconds && seconds<press.seconds+press.duration) {
            if(press.analog) state[press.byte]=press.value;
            else state[press.byte]&=static_cast<uint8_t>(~press.value);
        }
    }
}
