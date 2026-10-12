// GOW-Port: productor/cola/DLL real del cliente, con receptor procedural sin dispositivo.
#include "../src/gow_gs_pcsx2_client.h"

int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    const auto path=std::filesystem::absolute(argv[1]);
    // Mantener el receptor cargado después de que el hilo cierre su propia referencia.
    const auto module=LoadLibraryExW(path.c_str(),nullptr,0x1100);
    if(!module)return 2;
    const auto result=reinterpret_cast<uint32_t(*)()>(GetProcAddress(module,"gow_vsync_contract_result"));
    bool opened=false;
    {
        gow_pcsx2_client::Client client(path,{}, {},0,0);
        opened=client.initialized();
        if(opened){
            GSRegisters regs{};
            // FIELD cambia, permanece y vuelve. Bits ajenos y tick no determinan el campo.
            constexpr uint64_t csr[]={0x12340000,0x12342000,0x43212015,0x43210015};
            constexpr uint64_t ticks[]={91,94,100,101};
            constexpr uint32_t runtimeFields[]={0,1,1,0};
            for(unsigned i=0;i<4;++i){regs.csr.store(csr[i]);client.vsync(regs,ticks[i],runtimeFields[i]);}
        }
    } // El cierre drena los cuatro eventos y destruye la instancia en el hilo dueño.
    const bool okay=opened && result && result()==1u;
    FreeLibrary(module);
    if(!okay)std::fprintf(stderr,"Campo PCSX2 distinto del CSR empaquetado o drenaje incompleto\n");
    return okay?0:1;
}
