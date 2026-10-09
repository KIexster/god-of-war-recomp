# Feedback bilineal: reducción procedural

El generador `tools/render/generar_feedback_gs.cpp` acepta `--altura 1..416`.
Reduce solo la geometría de sus dos sprites de 32 píxeles de ancho; conserva
los texels, freeze y registros iniciales. El caso por defecto sigue en 416
filas y sus tres dumps `.gs` coinciden byte a byte con la opción explícita 416.
Las opciones inválidas se rechazan antes de crear archivos.

La captura `.bin` y el dump `.gs` comparten la misma altura. El verificador
`tests/gs_feedback_dump_test.cpp` recibe `--altura` como valor esperado
independiente: comprueba el freeze exacto, los vértices, el GIF PATH3 y los
4 MiB del End CPU. Una altura esperada distinta debe fallar. No cambia el
formato de los archivos ni el tamaño de la presentación (512×448).

## Reproducción en Windows

Después de `scripts\compilar_replay_gs.cmd`, desde la raíz del checkout:

```powershell
logs/generar_feedback_gs.exe logs/feedback_1 --pcsx2 --altura 1
logs/gs_feedback_dump_test.exe logs/feedback_1 --altura 1
logs/repetir_gs.exe logs/feedback_1/feedback_gs_self.bin hardware --repeticiones 3 --pausa-ms 1000 logs/feedback_1/live
logs/repetir_gs.exe logs/feedback_1/feedback_gs_self.bin hardware --snapshot-feedback --repeticiones 3 --pausa-ms 1000 logs/feedback_1/snapshot
```

Los dos replays devuelven 1 por diferencia respecto a CPU; el generador y el
verificador devuelven 0. Un resultado 1 no equivale a captura inválida: aquí
el End reproduce CPU exactamente y el candidato GPU es el que difiere.
Confirmar que las pasadas calientes no usan tiles compute antes de comparar.
No interpretar sus tiempos como FPS del juego.

## Resultado observado en RX 5700 XT

Shader anterior (`PS2X_GS_HW_DISCARD_UNCOVERED=0`), tres pasadas por política,
restauración inicial completa. La tabla usa la pasada 3 y compara 2/3 para
estabilidad; en ambas son dos primitivas hardware y cero tiles compute.

| Altura | CPU/hardware directo: bytes | Variación 2/3 directa | CPU/snapshot: bytes | Variación 2/3 snapshot |
|---|---|---|---|---|
| 1 | 3 | 0 | 3 | 0 |
| 31 | 93 | 0 | 93 | 0 |
| 32 | 96 | 0 | 96 | 0 |
| 33 | 188 | 0 | 188 | 0 |
| 64 | 378 | 0 | 378 | 0 |
| 416 | 4.580 | 27 | 4.557 | 0 |

La variación es una observación de esas repeticiones; cero bytes en una muestra
no garantiza ausencia de carreras. La reducción de una fila sí deja una
diferencia estable de un único píxel: `(32,0)`, byte CT32 4096. CPU da RGB
`(141,90,98)` y hardware `(132,87,86)`; el alfa coincide.

El CPU mantiene su página de textura de 8 KiB entre los dos Submit. Sus
texels iniciales `(31,0)` y `(32,0)` son `(123,85,197)` y `(160,96,0)`;
su media entera produce `(141,90,98)`. El primer sprite escribe en `(31,0)`
`(104,79,172)`: releer ese valor en el segundo produce `(132,87,86)`, igual al
GPU observado. Esto orienta hacia la persistencia de caché entre primitivas.
Congelar por Submit conserva el valor ya escrito por el anterior y tampoco
reproduce esa persistencia. El verificador CLI fija además el RGBA CPU de
ese píxel como control independiente.

La frontera de 32/33 filas cambia el primer byte distinto de 4096 a 65540.
Es una pista para estudiar reemplazos de página y accesos bilineales vecinos;
no justifica congelar toda la textura hasta TEXFLUSH ni cambiar por defecto
la política del renderer. La siguiente corrección debe conservar la salida
CPU de estos casos y los controles de fuente disjunta/nearest.

## Controles sin el juego

`tests/gs_feedback_height_cli_test.py` cubre alturas 1/31/32/33/64/416,
separaciones TEXFLUSH/scissor, alturas esperadas erróneas, límites y opciones
duplicadas. Comprueba freezes invariantes y el default exacto; la versión
anterior del generador falla al solicitar la primera altura reducida.
Los dos ejecutables y este control se integran en CI. Las herramientas se
compilan y prueban en Windows sin recompilar el juego: no se modifica el
runtime, la FPU, EE, IOP ni VU1 en este cambio. Los patrones son propios y
procedurales; no contienen bytes del juego.

## Sonda causal de persistencia (2026-10-09)

El replay añade `--invalidar-cache-submit`, solo con candidato `cpu`. Antes de
cada Submit exporta su estado, invalida únicamente la etiqueta de página y lo
restaura; mantiene los bytes de caché, como hace TEXFLUSH. La referencia CPU y
el End capturado permanecen intactos. Es una intervención de diagnóstico,
no una corrección del renderer ni una emulación de la caché en GPU.

```powershell
logs/generar_feedback_gs.exe logs/feedback_cache --altura 1 --separaciones
logs/repetir_gs.exe logs/feedback_cache/feedback_gs_self.bin cpu --invalidar-cache-submit --repeticiones 2 logs/feedback_cache/sonda
```

Devuelve 1 por diferencia del candidato. La referencia debe seguir dando
`CPU vs captura final: bytes=0`. Si no reproduce End/estado, el replay devuelve
3 y no acepta ese caso como resultado causal. La opción se rechaza con GPU,
si está duplicada o si se combina con `--snapshot-feedback`.

En 54 patrones propios (seis alturas × tres fuentes/filtros × tres separaciones)
se repite cada candidato dos veces, incluyendo restauración inicial completa:

| Altura | Bilinear self/scissor: bytes distintos por invalidar | Con TEXFLUSH entre sprites | Fuente disjunta o nearest |
|---|---|---|---|
| 1 | 3 | 0 | 0 |
| 31 | 93 | 0 | 0 |
| 32 | 96 | 0 | 0 |
| 33, 64, 416 | 0 | 0 | 0 |

El caso mínimo intervenido da RGB `(132,87,86)`, igual al GPU observado; la
referencia conserva `(141,90,98)`. Scissor no equivale a TEXFLUSH. Las bases
exportadas tras cada sprite confirman el reemplazo de página: para CT32 con
ancho 512 son `floor((altura-1)/32) * 65536`, más 2 MiB en fuente disjunta.
Ambos candidatos terminan en la misma base; en los casos pequeños de self y
scissor los bytes de esa página difieren después del segundo sprite.

Desde 33 filas la primera primitiva termina leyendo otra página y la segunda
ya provoca un miss natural. Invalidar entre Submit no cambia su salida;
esta intervención no explica ni corrige la divergencia GPU dentro de la
primitiva grande. La implementación futura debe conservar la persistencia
y el orden de reemplazo, incluyendo lecturas bilineales, antes de ampliarse
a múltiples páginas o activarse en el juego.

`tests/gs_texture_cache_causality_test.py` integra estas comprobaciones en CI:
VRAM, página/bytes de caché, RGB, imágenes y repetibilidad. Comprueba además
que un End adulterado sigue fallando con código 3 y que opciones inválidas no
crean archivos. En Windows pasa contra el runtime oficial de 77 parches;
el replay anterior falla al solicitar la nueva sonda. No cambia el runtime
ni requiere recompilar el ejecutable del juego. No se atribuye ganancia de FPS.

Se repite también el caso de una fila en la RX 5700 XT con el descarte default:
tres pasadas, pausa de 1 s, pasadas 2/3 con dos primitivas hardware y cero tiles
compute. Ambas mantienen tres bytes distintos de CPU, sin variación entre
ellas. Su imagen PPM coincide exactamente con la del candidato CPU intervenido.
La primera pasada usa cuatro tiles compute y se excluye de la confirmación
hardware. No se extrapola esta coincidencia al patrón de múltiples páginas.

La suite Windows de herramientas GS, CLI anterior, control de alturas,
configuración y sintaxis PowerShell pasa. Un mutante privado conserva la etiqueta
(acepta la opción pero no invalida); el control causal falla en la primera altura,
por lo que comprueba la intervención y no solo la presencia de la opción.
