# Presentación compartida de OpenGL

El parche `ps2recomp-gs-shared-host.patch` conecta las texturas del productor
GS con el consumidor raylib del juego. Se aplica después de las PR #31 y
#32 de Opus, sin modificar su VU1 ni el EE, FPU, IOP o los decodificadores FMV.

## Selección y propiedad

La ruta es opcional: `PS2X_GS_DIRECT_PRESENT=1`, junto con el backend GPU,
solicita compartir el contexto WGL en Windows. La ventana raylib se crea
antes de inicializar el GS. Si el backend efectivo no comparte, el host
conserva la presentación por RAM. El renderer CPU sigue disponible como
referencia. El registro `[gs-present] host mode` indica la ruta efectiva;
seleccionar la variable por sí solo no demuestra que se esté utilizando.

`GSHostPresentation` realiza el mismo dibujo en producción y en las pruebas:
adquiere la textura publicada, conserva sus dimensiones, dibuja con filtro
NEAREST y mantiene la relación de aspecto y las franjas. Las texturas
compartidas son prestadas y nunca se pasan a `UnloadTexture`. El cuadro
anterior termina su `EndDrawing` antes de adquirir otro slot. El worker GS
se destruye con el contexto host aún vivo; después se descarga la textura
propia de fallback y se cierra la ventana.

El display apagado conserva el placeholder magenta de la presentación por
RAM, de 640 × 448. Al reactivar PMODE se recupera la textura del productor.
Los metadatos y el tick de depuración corresponden al frame adquirido, que
puede ser anterior a la solicitud de presentación más reciente.

## Cola y capturas

El latch compartido encola Present después de los dibujos previos, sin
Flush/Sync bloqueante en el host. La capacidad se consulta en la instancia
del backend, no solo mediante el estado global de WGL. FINISH, lecturas de
VRAM, sustitución del backend y cierre conservan sus barreras.

Las capturas de diagnóstico usan `forceReadback` y se ejecutan en el hilo
propietario del backend. Componen la **VRAM actual con el CRTC guardado en
el último latch**, con caché por generación; no son una copia histórica de
la textura mostrada. Esta lectura solo se hace cuando se solicita una
captura, incluso si está habilitado el readback asíncrono habitual. El
camino de dibujo compartido no pide píxeles a CPU en cada cuadro.

## Controles sin archivos del juego

Se añaden doce controles: tres del compositor compartido, cinco del
frontend/cola y cuatro del consumidor raylib real. Cubren formatos de
color, offsets y unidades de FBP, mezcla de los dos circuitos, campos,
capturas explícitas, FIFO con worker retenido, FINISH, fallback RAM,
metadatos, escalas 1/2/no entera y transición PMODE.

En Windows se ejecutan con `GOW_GS_GPU_TEST=1`. Requieren un host OpenGL 3.3
de raylib y un productor OpenGL 4.6 realmente compartido; no sustituyen
OpenGL por mocks. Los cinco controles de cola también se ejecutan sin GL.
La cadena integrada contiene 75 parches sobre el commit fijado de
PS2Recomp. La base incluye el ring de uploads y las optimizaciones de VU1
de Opus.

Los doce controles específicos pasan localmente. Restaurar LINEAR en las
texturas publicadas hace fallar tres de ellos por diferencias de imagen.
La suite nativa reconstruida pasa **623/623**, con 33 controles OpenGL
reales, y la CI inicial pasa en Linux. Forzar Flush en el latch compartido
hace fallar dos de los cinco controles de cola; su espera acotada permite
terminar también con esa regresión. La comprobación del juego sigue en
curso; todavía no se atribuye a esta ruta una mejora de FPS.

En compilaciones incrementales con MSVC en español, comprobar que Ninja
registre dependencias de headers. En este equipo registraba cero: al
cambiar el tamaño de `PS2Runtime`, objetos de tests antiguos causaban una
violación de acceso. Se reconstruyeron los objetos del runtime y de pruebas
antes de repetir la suite. El PCH antiguo del runner también provocó C3668
al ver la interfaz GS anterior. `compilar.ps1` descarta los objetos del
runtime/tests y tanto el `.pch` como su objeto de creación antes de compilar
el runner; borrar solo el `.pch` no fuerza su generación en este Ninja.
`2_compilar.cmd` devuelve ahora el código real de compilación tras el pause.
Las ejecuciones con objetos antiguos no se cuentan como validación de la
integración.

El backend OpenGL conserva el crédito y la licencia GPL-3.0 del código de
Taylor N. Albarnaz / LightVelox, `sotc-port` en
`ac9efa070638ad3b3accd284de6f898d5ab271d1`. La conexión del host y sus
controles específicos llevan comentarios `// GOW-Port:`.
