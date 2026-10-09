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

La primera integración del arreglo pasó **610/610 pruebas nativas**, incluidas
**25 OpenGL efectivas**, antes de añadir la corrección de capacidad del
fallback. Quedan pendientes el control positivo GL de esa capacidad, la
compilación oficial del parche final, la suite final y la prueba funcional del juego.
Los resultados se registrarán aquí cuando terminen. No se atribuye a este
parche una mejora de FPS.
