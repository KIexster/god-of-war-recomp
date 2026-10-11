# Referencia externa: Persona 4 (2026-10-11)

Revisión solicitada por el usuario de
[Raikaru/Persona4-Decompilation](https://github.com/Raikaru/Persona4-Decompilation),
fijada al commit `6d3b9e362cc43eff2cb67b5ec5a289f38bb77025`.
Se leen documentación, herramientas y ejemplos de matrices/VU0/skinning;
no se ejecutan su instalación, compiladores ni juego y no se importa código.

Es una reconstrucción para Persona 4 USA `SLUS_217.82`, con C que busca
reproducir los bytes del ejecutable PS2 y fallbacks de ensamblador. Su
[README](https://github.com/Raikaru/Persona4-Decompilation/blob/6d3b9e362cc43eff2cb67b5ec5a289f38bb77025/README.md)
distingue coincidencia de bytes, recuperación C y enlace con código original.
Sus direcciones, estructuras, compiladores y datos no corresponden a GoW.

Material útil para nuestra investigación:

- [effect_vu0_internal.h](https://github.com/Raikaru/Persona4-Decompilation/blob/6d3b9e362cc43eff2cb67b5ec5a289f38bb77025/include/effect_vu0_internal.h)
  hace explícitas operaciones VU0 ordenadas, registros que permanecen vivos,
  cargas/almacenamientos alineados, transferencias de bits y máscaras de
  carriles. Sirve para orientar revisión de semántica EE/VU0 y construir
  casos procedurales propios; no compila directamente como implementación PC.
- [fndiff.py](https://github.com/Raikaru/Persona4-Decompilation/blob/6d3b9e362cc43eff2cb67b5ec5a289f38bb77025/tools/fndiff.py)
  y [verify.py](https://github.com/Raikaru/Persona4-Decompilation/blob/6d3b9e362cc43eff2cb67b5ec5a289f38bb77025/tools/verify.py)
  comparan instrucciones/bytes y comprueban relocaciones. La herramienta
  evita presentar ensamblador original incluido como una recuperación C
  validada; tampoco debe ocultar instrucciones R5900 no decodificadas bajo
  un mismo texto desconocido. El método es útil al contrastar una rutina GoW
  con su ELF, aunque nuestra recompilación a PC necesita prueba de semántica.
- [RenderWare.md](https://github.com/Raikaru/Persona4-Decompilation/blob/6d3b9e362cc43eff2cb67b5ec5a289f38bb77025/wiki/RenderWare.md)
  distingue núcleo y driver PS2: hay funciones todavía en ensamblador y la
  fuente de núcleo no contiene el driver sky2. Los archivos rpskin y
  skinskycommon revisados pertenecen a ese middleware; no proporcionan una
  corrección demostrada del camino de skinning o GS de GoW.

El árbol revisado no muestra una licencia general del repositorio. Hay
licencias de componentes concretos, que no se extienden automáticamente al
resto. Se conservan enlaces y metodología; cualquier reutilización de código
necesitaría identificar sus condiciones. No se incorporan fuentes de
RenderWare, ensamblador retail o datos de Persona 4.

Aplicación inmediata: revisar transferencias de bits y máscaras de carriles
si una entrada/salida GoW o su envío VIF/VU muestra una discrepancia. No
atribuirle al repositorio externo una solución del encuadre, animaciones o
rendimiento sin reproducirla. La sonda actual de GoW solo confirma productos
finales en una muestra; no descarta poses de entrada incorrectas.
