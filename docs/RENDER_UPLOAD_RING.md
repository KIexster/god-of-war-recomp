# Protección del ring de uploads OpenGL

El backend utiliza un buffer persistente de 64 MiB dividido en cuatro chunks
de 16 MiB. Antes de reutilizar un chunk, espera la fence del trabajo anterior.
La implementación original ignoraba el resultado de `glClientWaitSync`:
un timeout o un fallo permitían sobrescribir datos que la GPU todavía podía
leer. También dejaba sin eliminar el buffer y sus fences al destruir el
backend. Si el contexto del host seguía vivo, el share group conservaba
esos 64 MiB.

`ps2recomp-gs-upload-ring.patch` se aplica después de
`ps2recomp-vu1-fast-flags.patch`, como parche 69. No altera shaders, VU1,
EE, FPU ni IOP. Conserva el renderer CPU de referencia.

## Comportamiento

Solo `GL_ALREADY_SIGNALED` y `GL_CONDITION_SATISFIED` permiten reutilizar
un chunk. Se conserva la espera acotada de cinco segundos. Ante timeout,
fallo de espera o fence nula, se desactiva permanentemente el ring de ese
backend, sin copiar el payload ni avanzar su cursor. Los uploads siguientes
usan los buffers streaming existentes. Se emite una sola línea con el motivo:

```text
[gs-gpu] persistent uploads disabled (...); using streaming buffers
```

Esta degradación protege la memoria pendiente; no certifica recuperación
de un contexto OpenGL perdido. No añade un bucle de espera ilimitado ni
`glFinish` al render. Los uploads sobredimensionados y la ausencia de mapping
también conservan el fallback existente.

Al liberar el backend se eliminan todas las fences retenidas y el buffer
persistente antes de desmontar su contexto. La liberación del ring también
se ejecuta si la compilación de shaders falla después de crear el buffer,
aunque el backend no haya llegado a marcarse como inicializado. OpenGL
conserva internamente los datos que sigan referenciados por comandos pendientes.

La revisión del fallback detectó otra inconsistencia: un upload streaming
puede encoger `dataBuffer`, que también se usa como salida de presentación
y copias. Se actualiza `dataCapacity` con el tamaño realmente asignado para
que la siguiente reserva vuelva a ampliar el buffer cuando corresponda.

## Regresiones y evidencia

Las doce pruebas independientes de OpenGL cubren timeout y fallo de espera,
memoria pendiente sin cambios, degradación de uploads posteriores, fence
nula, liberación única, alineación, padding, payload vacío/nulo y límites
de tamaño. Pasan con MSVC, C++20 y `/O2 /W4 /WX`. Un control negativo que
ignora el timeout en una copia temporal compila y falla en dos de las doce
pruebas: detecta la sobrescritura y la reutilización insegura.

El control OpenGL mantiene un contexto host vivo mientras crea y destruye
el backend. Exige un ring realmente mapeado de 64 MiB, renderiza un sprite
procedural de color no nulo y compara los 4 MiB de VRAM exactamente con CPU.
El runtime anterior falla porque el buffer sigue existiendo después de
destruir el backend; la versión corregida pasa. No usa assets del juego
ni getters añadidos al producto. El caso de inicialización parcial se revisa
en el código; esta prueba no fuerza un error de compilación de shaders.

La regresión GL del fallback usa dos lecturas de 33.546.240 bytes y, entre
ellas, un upload de 16.785.408 bytes más sus descriptores. Este supera el
chunk y reduce realmente el buffer mediante streaming. Usa la API pública
del GS y colores procedurales que hacen idempotentes los aliases de VRAM.
Compara ambas lecturas completas y los 4 MiB de VRAM con CPU; observa
también el ID y el tamaño reales del objeto OpenGL. La biblioteca anterior
falla en tres comprobaciones: no amplía el buffer, difiere del CPU al leer
y conserva bytes del color anterior. La primera lectura y el upload sí
coinciden, por lo que el fallo queda aislado a la reserva posterior.

La biblioteca corregida pasa también el control positivo de capacidad:
**1/1**, con las dos lecturas completas y la VRAM exactas frente a CPU.
La integración final de `e1a9270` pasó **611/611 pruebas nativas**, incluidas
**26 OpenGL efectivas**, después de la compilación oficial con
`scripts\2_compilar.cmd`, código 0 y las 501 micromemorias locales.
El ejecutable tiene 70.229.504 bytes, SHA256
`4066E704CDF1357FE74ECC21D7FF4E0834811B2812B240389BB147676D4530A3`.
Las 97 fuentes de los parches coinciden con el runtime compilado, normalizando
solo CRLF y el include del runner añadido por el script. Los dos jobs de la
CI de la PR #30 pasan.

La base incorporó después los cambios publicados por Opus en `ca69e9b`.
Se repite la compilación oficial con ese generador VU1 para verificar la
versión conjunta antes de terminar la PR. La prueba funcional del juego
queda pendiente de revisar; un primer intento terminó a los 40,24 s,
todavía en las pantallas iniciales. No se cuenta como prueba de partida.
No se atribuye a este parche una mejora de FPS.
