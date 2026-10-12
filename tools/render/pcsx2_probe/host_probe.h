// GOW-Port: servicios de la prueba aislada; no son una API del runtime.
#pragma once
bool gsProbeOpenWindow(unsigned width, unsigned height);
void gsProbeCloseWindow();
bool gsProbeHasWindow();
void gsProbeBeginUiFrame();
void gsProbeSkipUiFrame();
void gsProbeEndUiFrame();
extern void (*gsProbeBeforePresent)();
extern unsigned gsProbeGpuPresents;
