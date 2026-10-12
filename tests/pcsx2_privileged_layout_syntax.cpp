// GOW-Port: contrastar offsets del espejo con los headers originales fijados.
#include "GS/GSRegs.h"
#include <cstddef>
static_assert(sizeof(GSPrivRegSet)==8192);
static_assert(offsetof(GSPrivRegSet,PMODE)==0x0000);
static_assert(offsetof(GSPrivRegSet,SMODE1)==0x0010);
static_assert(offsetof(GSPrivRegSet,SMODE2)==0x0020);
static_assert(offsetof(GSPrivRegSet,SRFSH)==0x0030);
static_assert(offsetof(GSPrivRegSet,SYNCH1)==0x0040);
static_assert(offsetof(GSPrivRegSet,SYNCH2)==0x0050);
static_assert(offsetof(GSPrivRegSet,SYNCV)==0x0060);
static_assert(offsetof(GSPrivRegSet,DISP[0].DISPFB)==0x0070);
static_assert(offsetof(GSPrivRegSet,DISP[0].DISPLAY)==0x0080);
static_assert(offsetof(GSPrivRegSet,DISP[1].DISPFB)==0x0090);
static_assert(offsetof(GSPrivRegSet,DISP[1].DISPLAY)==0x00a0);
static_assert(offsetof(GSPrivRegSet,EXTBUF)==0x00b0);
static_assert(offsetof(GSPrivRegSet,EXTDATA)==0x00c0);
static_assert(offsetof(GSPrivRegSet,EXTWRITE)==0x00d0);
static_assert(offsetof(GSPrivRegSet,BGCOLOR)==0x00e0);
static_assert(offsetof(GSPrivRegSet,CSR)==0x1000);
static_assert(offsetof(GSPrivRegSet,IMR)==0x1010);
static_assert(offsetof(GSPrivRegSet,BUSDIR)==0x1040);
static_assert(offsetof(GSPrivRegSet,SIGLBLID)==0x1080);
