# Notas del usuario (registro de lo que dice)

Archivo vivo: se agrega aquí lo que el usuario va indicando (hechos de la instalación, criterios de diseño, decisiones y pedidos). Se transcribe con ortografía corregida; lo que es estimación mía se marca como *(estimado)*.

## 1. Hechos de la instalación (dichos por el usuario)

- Tanque elevado **cilíndrico**, ~**1.5 m de alto**. Del fondo (0 %) al borde superior (100 % lleno). Altura de agua estimada hasta el sensor: ~5 m → ~7.1 PSI lleno *(estimado)*.
- Para medir la estática mínima: consumir el agua **con la bomba apagada** hasta dejar el tanque casi vacío, **a ~tres dedos del fondo**.
- El **sensor de presión está en el pulmón**, y **antes del pulmón hay un check** (Check #1).
- La **tubería de llenado entra por la parte alta del tanque**. Su presión estática de llenado es la de la altura de esa tubería; es única y determinada con el tanque lleno.
- Sin flujo, la presión de llenado se iguala a la estática de esa columna: se considera que el flujo se detuvo.
- Con flujo, la presión de llenado es una presión **constante**, parecida a tener **muchas válvulas abiertas dentro de la casa**; está relacionada con la estática.
- **Presión estática = un rango**: [estática con el tanque vacío, estática con el tanque lleno].
- Al apagar la bomba durante el llenado, el flujo se detiene y la presión queda en el rango de agua estática.
- Cuando el tanque se llena y cierra el flotador de llenado, la presión sube a **20 PSI** y se apaga la bomba. Al descender la presión y descargarse el pulmón, enciende la bomba.
- Con la **calle seca y el tanque totalmente vacío**, la presión en el sensor baja a **~1 PSI** (dato del usuario, pedido 63).
- **Bomba degradada**: antes apenas hacía cortar el presostato (termostato) a **30 PSI**; ahora solo llega a **25 PSI**. Ha perdido potencia.
- Se usará **Arduino** (Nano) y **VS Code** para el proyecto.
- Chrome del usuario con depuración remota en `127.0.0.1:9222` (sin túnel); sesión de GitHub iniciada por el usuario. Cuenta GitHub: `alexfuente156253-dev`.

## 2. Criterios de diseño del usuario

1. La **presión estática es un rango** (tanque vacío ↔ tanque lleno), no un único valor.
2. Para calibrar umbrales inferiores se usa el valor **mínimo** (tanque casi vacío). Si la estática real es mayor, no hay problema (más margen).
3. Para detectar si hay agua al arrancar la bomba hay que **medir la aceleración (velocidad de subida) de la presión** justo en el arranque: si la subida se **frena o se estanca**, se presume que **no hay fluido**.
4. Matiz del usuario: la presión puede estar **por encima de la estática pero sin aceleración** (pulmón retenido por el Check #1); eso también debe contar como "sin fluido". El nivel sobre la estática solo no basta.
5. Aprobó implementar la detección por **subida sostenida de presión** ("ok procede con lo demás") con los valores iniciales propuestos: pendiente mínima 0.3 PSI/s, +1.5 PSI sobre la base, δ = 1.0 PSI sobre la estática máxima (sin objeción a los valores).
6. La corrección de la detección de bomba en vacío (cruce de `P_ARRANQUE` en la micro-pausa) fue aprobada ("sí").

## 2b. Concepto de control por aceleración de presión y modos (dicho por el usuario)

Transcripción ordenada de lo que el usuario explicó (nada de esto está implementado todavía):

1. La **lógica de aceleración del aumento de presión será "el timón"**: sirve para estimar la **fuerza de la bomba**, que **depende de la presión de la calle**.
2. Ejemplo: con **tuberías vacías** la presión puede estar **cercana a cero** y debe ir **acelerando hasta llegar a la estática de la columna de llenado**, y **aún más, a una presión estable durante el llenado**.
3. Esa **presión estable de llenado hay que conocerla: se debe calibrar**, y luego darle una **histéresis de ± 0.5 PSI**.
4. Cuando llena y **llega a 20 PSI**, el sistema **deja de monitorear** (el flujo) y **empieza a monitorear cuando enciende otra vez**.
5. Si **detecta presión de llenado → entra en modo "llenando tanque"**.
6. Si **llenó el tanque → entra en modo "hidroneumático"**.
7. En **modo hidroneumático la aceleración puede disminuir** por **válvulas abiertas** (consumo en la casa); por eso hay que **extender el tiempo o el umbral** para detectar que no hay flujo. → **Revertido después por el usuario (pedido 24): se usa una única pendiente, sin extender umbral ni tiempo.**
8. Caso de bomba que pierde fuerza por falta de agua: si **solo alcanza 15 PSI**, **apagará porque pensará que no hay flujo**; pero **al volver a 10 PSI iniciará otra vez**, y **si hay aceleración de presión seguirá encendida** hasta llegar a la presión que **en ese momento** pueda alcanzar, **que puede ser menor (algo como 13 PSI)**.
9. Se considera que **se fue el flujo total cuando no suba de 11 PSI**.
10. **Nota del usuario:** tomó **10 PSI como referencia solo para el ejemplo** (ese 10 PSI es `P_ARRANQUE`, confirmado en el pedido 32) (los números 10, 11, 13 y 15 son ilustrativos, no valores definitivos).

### Interpretación del asistente (SIN confirmar por el usuario)

- **Modos**: `MONITOREO_ARRANQUE` (se evalúa la aceleración desde que enciende la bomba), `LLENANDO_TANQUE` (P estable ≈ presión de llenado calibrada ± 0.5 PSI) e `HIDRONEUMATICO` (tanque lleno, la presión sube hacia 20 PSI y se corta; con umbral/tiempo de aceleración más tolerantes por el consumo de la casa).
- **Presión alcanzable adaptativa**: la bomba puede cortar por estancamiento en una presión menor a 20 PSI (ej. 15), reiniciar al bajar al umbral de arranque (ej. 10) y volver a cortar más abajo (ej. 13) mientras siga acelerando; el **flujo total se da por perdido cuando no supera un piso** (ej. 11 PSI, es decir, cerca del umbral de arranque).
- Esto reemplazaría/ampliaría los criterios actuales (sondeo de 4 s, `P_SIN_FLUJO` de 10 s, micro-pausa).
- **Preguntas abiertas:** respondidas en la sección 2c (queda pendiente solo la tolerancia extra del modo hidroneumático).

## 2c. Respuestas del usuario a las preguntas abiertas de 2b

1. **El piso de "flujo total perdido" es siempre `P_ARRANQUE` + 1 PSI** → respuesta: **sí**. (Con `P_ARRANQUE` = 9.5 PSI el piso sería 10.5 PSI; los 10/11 PSI del ejemplo eran ilustrativos.)
2. **Tolerancia extra en modo hidroneumático**: el usuario preguntó "¿a qué tolerancia te refieres?". Se refiere al margen que se da al criterio de "sin flujo" cuando hay válvulas abiertas en la casa (la aceleración baja): ampliar el **tiempo** (hoy `T_SIN_FLUJO` = 10 s) y/o bajar el **umbral de pendiente** (hoy `PENDIENTE_MIN_CEBADO` = 0.3 PSI/s) en ese modo. *Propuesta inicial del asistente, pendiente de confirmar:* tiempo ×2 (20 s) y pendiente mínima ÷2 (0.15 PSI/s), a ajustar con pruebas de consumo.
   - ~~Decisión del usuario: para la tolerancia basta trabajar con la pendiente mínima de subida, bajando el umbral en hidroneumático (0.15 PSI/s).~~ **Revertido por el usuario (pedido 24):** no hay tolerancia distinta; se usa **una única pendiente** (`PENDIENTE_MIN_SUBIDA` = 0.3 PSI/s) en el arranque y en hidroneumático. `T_SIN_FLUJO` sigue en 10 s.
3. **¿La presión de llenado calibrada se mide con el comando `L`?** → el usuario no sabía y pidió **deducirlo del código**. Deducción: **sí**. `L` (flotador abierto, grifos cerrados) enciende B1, deja 15 s de ajuste y promedia 10 s la presión estable de llenado (`P_LLENADO_REAL`); sugiere `P_ARRANQUE` (prom − 1.0) y `P_TECHO_CEBADO_DINAMICO` (prom − 0.3). La histéresis de ±0.5 PSI se aplica sobre ese promedio.
4. **Las micro-pausas de verificación de flujo se mantienen.**
5. **Requisito de arranque**: **al energizar la bomba debe empezar siempre en modo "verificación de flujo"**; **cuando se confirma que hay flujo, continúa con el funcionamiento ya planteado** (modo llenando tanque / modo hidroneumático, secciones 2 y 2b).

## 2d. Respuestas del usuario a la revisión de escenarios (pedido 26)

1. **Rango de flujo**: hay que poner un rango. Ejemplo del usuario: si la estática es 7.5 PSI, **se considera que hay flujo cuando la presión de llenado está por encima de 8.5 PSI** (estática + 1.0 PSI; coincide con `P_ESTATICA_MAX + DELTA_SOBRE_ESTATICA`).
2. Sobre "si el llenado sube a 11 PSI (más de 10.7) el código lo toma como hidroneumático": el usuario dice que **es correcto que entre en modo hidroneumático**; **hay que verificarlo en sitio** (y se puede simular).
3. **La presión de llenado es ligeramente superior a la estática y puede subir más**; **si sobrepasa el umbral de trabajo del hidroneumático, ahí sí es un problema** → el usuario pide **una solución** para ese caso.
4. **Acepta** medir la pendiente en **ventanas de 2–3 s en modo hidroneumático** (implementado: 3 s).
5. Sobre la incongruencia micro-pausa/estancamiento: **si la presión no sube más y queda en presiones intermedias, ese punto se toma como tope máximo por software** *(el usuario corrigió después este concepto, pedido 30: es la "presión de estancamiento", no un tope)* y **se arranca de nuevo a la presión de arranque** (ej. 10 PSI). Implementado: la micro-pausa usa el mismo piso (`P_ARRANQUE` + 1); sobre el piso → tope por software (`pTopeSoftware`) y REPOSO; se eliminó `P_CASI_LLENO_PAUSA`.
6. **Acepta** limitar el umbral del sondeo al piso (`PISO_FLUJO_TOTAL`). Implementado; se eliminó `P_TECHO_CEBADO_DINAMICO` (redundante) y el comando `L` ya no lo sugiere.
7. Sobre la consecuencia de la pendiente única con consumo en hidroneumático: **"si esto pasa, se aumenta el umbral"**. Aclarado en el pedido 53: se refería a **aumentar el umbral de la pendiente, es decir, dar más tiempo para dar más margen a la velocidad de llenado**. **Ya no aplica**: el estancamiento en bombeo ahora es "la presión no sube" (pedido 38). Cerrado.

## 2e. Micro-pausa en cada estancamiento (dicho por el usuario, pedido 34)

1. **La micro-pausa se debe aplicar cada vez que la aceleración llega a 0**, para detectar:
   - **llenando** (la presión cae en la pausa),
   - **llenado** (la presión llega a 20 PSI),
   - **bomba con poca potencia** (la presión se queda, por ejemplo, en 13 PSI).
2. Caso a cubrir: **en "llenando", el tanque se llena y la presión sube pero no logra alcanzar los 20 PSI**; el sistema pensaría que sigue llenando. **Al haber un nuevo nivel superior de presión se debe hacer una micro-pausa** para detectar si sigue llenando o terminó de llenar.
3. "Vamos a ver qué se puede solucionar con esto."

## 2f. Respuestas del usuario a la revisión de lógica (pedido 37)

1. **Caso A (flotador que cierra poco a poco):** "simplemente la presión aceleraría hasta 20 PSI, no hay problema". **Si se queda estancada en, por ejemplo, 17 PSI, se aplica la micro-pausa.** (Es decir: una subida lenta sigue siendo subida; solo un estancamiento real dispara la prueba.)
2. **Nuevo requisito:** puede ocurrir que **durante el llenado el sistema se quede sin flujo y la presión baje** (porque **alguien abrió una válvula** o **se agota el agua de la calle**). **Se debe aplicar una lógica**, porque **si durante el llenado la presión baja, quiere decir que el flujo se está deteniendo.**
3. **Caso B (falsas alarmas al arrancar con consumo):** no se cambia el código; **se instruirá a los operadores para que esperen el apagado de la bomba antes de abrir las válvulas.**
4. **Precisión del usuario (pedido 38):** si **con la bomba encendida la presión desciende hasta la presión estática** ("la presión baja durante el llenado"), **se considera sin flujo, sea en el tiempo que fuere.**
5. **Precisión del usuario (pedido 38):** en la opción A, **si durante el llenado la presión se estanca entre 18 y 20 PSI, se considera tanque lleno.**

## 3. Pedidos y decisiones (cronología)

1. Proyecto nuevo: hacer commit y subirlo a un repositorio nuevo.
2. Entregó el firmware v10.3 completo y los diagramas hidráulico y eléctrico (quedaron en el repositorio).
3. Preguntó cómo usar VS Code para calibrar las presiones → modo `MODO_CALIBRACION` y monitor serie.
4. Observó que `T_CARGA_PULMON` también depende de la presión de la calle.
5. Quiere dar órdenes como "calibra P estática real" y que el sistema registre la presión; preguntó cuántos parámetros se pueden automatizar → comandos `E L F S C X` y script `tools/calibrar.py`.
6. Pidió comentar y registrar todos los hallazgos y el contexto → `docs/CALIBRACION.md`.
7. "Procede con todo" → implementación de los comandos, script y CSV.
8. Pidió subir el proyecto a git; indicó que git está instalado; indicó que la conexión debe ser **sin túnel**; dio la vía de Playwright/CDP a Chrome (9222). Inició sesión en GitHub y pidió revisar los repos existentes y crear el nuevo → `zoraida-bomba-de-agua` (privado).
9. Pidió dejar comentado cómo se descargó y usó PlatformIO para compilar (porque `downloads.arduino.cc` devolvió 403).
10. Pidió revisar la lógica operativa de la bomba por incongruencias → revisión con hallazgos.
11. Preguntó si existe software de simulación hidráulica → "ok, impleméntala" → simulador en lazo cerrado en `sim/`.
12. Aprobó aplicar la corrección de la bomba en vacío ("sí").
13. Preguntó qué es la curva de la bomba (datos pendientes: altura de cierre, caudal).
14. Informó la pérdida de potencia de la bomba (30 → 25 PSI).
15. Explicó la física de la presión de llenado y propuso medir la aceleración de la presión al arrancar; después matizó el caso "sobre la estática pero sin aceleración".
16. Pidió registrar todo lo que dice en un archivo aparte (este archivo).
17. Pidió proceder con lo demás → se implementó la detección por subida sostenida y se probó en el simulador.

18. Pidió **descartar "bomba en vacío" y "sin flujo" como casos separados**: son lo mismo y el código duplicado es redundante → un único estado/criterio "sin flujo" (se eliminó `P_CAIDA_SECO`; estado `ESPERA_SIN_FLUJO`).

19. **Regla de trabajo**: compilar solo cuando el usuario lo ordene, y subir a git (commit + push) solo cuando el usuario lo ordene. Los cambios quedan locales hasta entonces.
20. Explicó el **concepto de control por aceleración de presión, presión de llenado calibrada con histéresis ±0.5 PSI y modos "llenando tanque" / "hidroneumático"** (sección 2b) y pidió registrarlo todo.
21. Respondió las preguntas abiertas de 2b (sección 2c): piso = `P_ARRANQUE` + 1, se mantienen las micro-pausas, la presión de llenado se calibra con `L` (deducido del código), y la bomba debe arrancar siempre en modo verificación de flujo.
22. Decidió que la tolerancia del modo hidroneumático se maneje **solo con la pendiente mínima de subida** (sin extender el tiempo).
23. Pidió **modificar el código según todo lo recomendado** → implementados los modos LLENANDO TANQUE / HIDRONEUMÁTICO, la detección por aceleración con pendiente mínima reducida en hidroneumático, el piso `P_ARRANQUE` + 1 para "sin flujo total" y el arranque siempre en verificación de flujo. **Sin compilar ni subir** (regla de trabajo: solo bajo orden).
24. **Revirtió** la extensión del umbral en modo hidroneumático: en hidroneumático hay una pendiente de velocidad y se usa **una única pendiente** (la misma del arranque). Se eliminó `PENDIENTE_MIN_HIDRO`; la constante única es `PENDIENTE_MIN_SUBIDA` (0.3 PSI/s). Sin compilar.
25. Pidió **revisar posibles escenarios** de la lógica nueva (revisión por lectura del código; sin compilar ni simular, por la regla de trabajo).
26. Respondió la revisión de escenarios (sección 2d): rango de flujo = estática + 1 PSI; aceptó ventana de 2–3 s en hidroneumático, el tope por software al estancarse y limitar el sondeo al piso; pidió una solución para cuando la presión de llenado supere el umbral de trabajo del hidroneumático. Implementados los puntos aceptados, **sin compilar**.
27. Sobre la solución para presión de llenado alta: **el caso B (llenado que llega o supera 20 PSI) no va a pasar** en esta instalación (descartado). **Le gusta la solución A** (micro-pausa de prueba al estancarse: si la presión cae → está llenando a mayor presión, se aprende como `P_LLENADO` actual y sigue bombeando; si no cae → flotador cerrado, tope por software y reposo). Preguntó qué significa "la presión cae".
28. **Aprobó y pidió aplicar la solución A** con la regla de que la presión de llenado aprendida se reinicia al valor calibrado en cada arranque. Implementado (`pLlenadoActual`, micro-pausa de prueba de modo al estancarse en hidroneumático). **Sin compilar.**
29. Preguntó si queda **alguna otra incongruencia por resolver** (revisión por lectura del código, sin compilar).
30. Pidió **aplicar las mejoras** 1, 2 y 3 de la revisión (espera sin reinicio de intentos, umbral único de flujo = estática + 1.0, `P_LLENADO` por defecto coherente) y aclaró el punto 4: **"tope máximo por software" es cuando la bomba pierde fuerza, la presión llega a valores intermedios (ej. 15 PSI) y la aceleración se hace 0; ahí apaga y enciende de nuevo al llegar a la mínima de arranque. Dijo "por software" porque no es a 20 PSI (el tope máximo); reconoce que fue un mal concepto suyo** y autorizó corregirlo → renombrado a **presión de estancamiento** (`pEstancamiento`). Aplicado, **sin compilar**.
31. Preguntó qué es `P_ARRANQUE` y en qué etapa está.
32. Confirmó que **`P_ARRANQUE` es la presión que en sus ejemplos ponía como 10 PSI** (la mínima de arranque). Afirmó que **`P_ARRANQUE` siempre quedará por encima de la estática**, porque es una presión superior a ella.
33. Preguntó qué más queda: bugs o incongruencias (revisión completa por lectura, incluido el código de calibración; sin compilar).
34. Respondió el punto 1 (punto ciego en la banda): **micro-pausa en cada estancamiento (aceleración 0) y en cada nuevo nivel superior de presión** (sección 2e).
35. Aprobó y pidió aplicar la regla de la sección 2e → implementada la **micro-pausa en cada estancamiento en un nivel nuevo** con aprendizaje de la meseta (`pMeseta`); se eliminó `P_LLENADO` del control. **Sin compilar.**
36. Preguntó qué otra cosa de lógica queda por resolver (revisión por lectura tras la regla de mesetas; sin compilar).
37. Respondió la revisión de lógica (sección 2f): caso A sin problema (sube hasta 20; si se estanca, p. ej. en 17, micro-pausa); pidió lógica para **presión que baja durante el llenado** (flujo deteniéndose); caso B se resuelve con **instrucción a los operadores**.
38. Precisó: con la bomba encendida, **bajar hasta la estática = sin flujo** (en cualquier momento) y **estancada entre 18 y 20 PSI = tanque lleno**. Aplicado junto con "estancada = la presión no sube" y la nota para operadores en el README. **Sin compilar.**
39. Preguntó otra vez qué queda por resolver de lógica (revisión por lectura; sin compilar).
40. Indicó que **las micro-pausas periódicas deben ser solo durante el llenado**. Aplicado: el intervalo 90/180/300 s solo corre con meseta confirmada y P2 en ella (no en la subida inicial ni en hidroneumático). **Sin compilar.**
41. Preguntó cuánto dura una micro-pausa (1.5 s mínimo, 3.0 s máximo según el código).
42. Preguntó qué hace el sistema al encenderse por primera vez.
43. Pidió **guardar en EEPROM** lo que se pierde con los cortes de luz → se guardan intentos fallidos, arranques en la hora y enfriamiento (registro rotativo de 64 posiciones para no gastar la EEPROM). **Sin compilar.**
44. Preguntó a qué se refiere el enfriamiento (estado PAUSA_ENFRIAMIENTO_MOTOR).
45. Pidió **eliminar la lógica de enfriamiento** → eliminados el estado `PAUSA_ENFRIAMIENTO_MOTOR`, el límite de 45 min de marcha y el presupuesto de 18 arranques/h (que solo servían para disparar el enfriamiento). Quedan el anti-ciclado de 3 min y el retardo de 15 s; la protección térmica la da el relé térmico. La EEPROM guarda solo los intentos fallidos. **Sin compilar.**
46. Preguntó qué hace el sistema cuando detecta que no hay flujo después de que había flujo.
47. Preguntó qué significa presión bajo el piso (PISO_FLUJO_TOTAL).
48. Pidió poner el piso a 10 PSI como referencia (mismo valor que P_ARRANQUE) y luego **lo retiró**: no tomarlo en cuenta. Sin cambios; el piso sigue en P_ARRANQUE + 1.
49. Preguntó otra vez qué queda por resolver de lógica (revisión tras eliminar el enfriamiento; sin compilar).
50. Pidió aplicar: (a) medir la caída de la micro-pausa desde la presión al apagar (golpe de ariete); (b) al recuperarse de una falla de sensor, volver a la espera si hay intentos pendientes; (c) **sin flujo: apagar apenas la presión toque la estática** (no esperar 10 s); (d) **aplicar la regla de los 45 minutos** como tiempo máximo de bombeo sin llegar a lleno. Aplicado; en (c) se mantienen 10 s de plazo solo tras arrancar o reanudar, para que la bomba suba la presión; en (d) apaga y espera 20 min. **Sin compilar.**
51. Preguntó otra vez qué queda por resolver de lógica (revisión tras el pedido 50; sin compilar).
52. Preguntó el contexto de la pregunta pendiente sobre se aumenta el umbral (sección 2d, punto 7).

53. Aclaró que "se aumenta el umbral" significaba **aumentar el umbral de la pendiente (más tiempo, más margen para la velocidad de llenado)** y confirmó que **ya no aplica**. Punto cerrado.

54. Preguntó si se aplicó "la presión no sube" → sí (pedido 38), mostrado en el código.
55. **Ordenó compilar y subir.** Compilado (se corrigió un error: `RegistroPersistente` debía ir antes de las funciones) y simulado: 10 de 14 escenarios pasan; las 4 fallas son el caso de consumo durante el arranque (instrucción a operadores). Subido a GitHub.
56. Pidió graficar las presiones en los diferentes puntos de operación: tablero en claude.ai (Presiones de la bomba B1, https://claude.ai/artifact/LHdutAguvTTPhEvkx9T5CU) con seis escenarios del simulador, umbrales del firmware y tabla de presión por punto de operación.
57. Al ver la gráfica preguntó por qué, si el corte es en 20 PSI, la presión sube a 27 PSI (sobrepico por los 2 s de confirmación del corte).
58. Pidió aplicar la **opción 1**: **cortar al llegar a 20 PSI, sin los 2 s de espera**. Aplicado (se eliminaron `T_CORTE_20PSI` y `t_corteAlta`). **Sin compilar.**
59. Al ver que el tablero seguía mostrando 27 PSI (datos viejos), ordenó **proceder**: compilado (producción 5.656 B), simulado (presión máxima 20.1–20.2 PSI, antes 27.2) y **tablero actualizado** con los datos nuevos. Cambios en el repositorio sin subir.
60. Pidió analizar la gráfica del escenario bomba que pierde fuerza (falsa espera sin flujo al final por consumo durante la verificación al arrancar).
61. Preguntó por qué la presión siempre empieza en (o cerca de) la estática antes de cada arranque.
62. Pidió **añadir archivos de depuración** al programa; eligió **ambas** opciones: registro de eventos en la EEPROM (sobrevive a cortes de luz) y bitácora por USB guardada en archivos de la PC (`MODO_DEPURACION`, entorno `nano_depuracion`, script `tools/depurar.py`). **Sin compilar.**

63. Señaló que con la **calle seca desde el inicio la presión bajaría a ~1 PSI con el tanque totalmente vacío** (la gráfica la mostraba fija en 7.1). Dato del usuario: tanque vacío ≈ 1 PSI en el sensor. Se corrigió el modelo del simulador (subida y bajada que se drenan, consumo en el escenario) y se agregó el escenario `vuelve_tanque_vacio`. **Sin compilar.**
64. Preguntó si la gráfica de la bomba que pierde fuerza está en modo hidroneumático o llenado.

65. Aprobó el **modo de bombeo con tres valores** (subiendo / llenando / hidroneumático), para que un ciclo con el tanque lleno no se etiquete como llenado. Aplicado. **Sin compilar.**

66. Ordenó **compilar, graficar y subir**. Compilados los tres entornos (producción 6.482 B, calibración 13.878 B, depuración 9.048 B); simulador 11 de 15 (fallas = consumo al arrancar); tablero actualizado con el modo en el cursor y la gráfica nueva "Vuelve el agua con el tanque vacío"; subido a GitHub.
67. Preguntó si los valles de la gráfica de la bomba que pierde fuerza son micro-pausas.
68. Concluyó que el proyecto quedó listo; se resumió lo terminado y lo que falta en campo.
69. Preguntó si la verificación de 4 s se hace al principio (al arrancar la bomba).

70. Pidió que la verificación al arrancar, **en vez de 4 segundos, use la misma función de aceleración de presión**. Aplicado: sigue mientras la presión suba y declara sin flujo solo si se estanca (10 s sin subir 0.25 PSI) bajo el umbral; se eliminaron el plazo de 4 s y la pendiente de 0.3 PSI/s. **Sin compilar.**
71. Preguntó cuál es el umbral y qué es la presión de partida de la verificación al arrancar.
72. Planteó que con la lógica de aceleración se puede hacer todo sin establecer tiempos fijos.

73. Aprobó reemplazar el plazo de 10 s tras arrancar o reanudar por la **función de aceleración**. Aplicado: toda decisión de flujo depende ya de la aceleración; los tiempos fijos que quedan son protecciones del motor, la espera sin flujo, el máximo de 45 min, la ventana de medida y filtros de ruido. **Sin compilar.**
74. Preguntó por qué en la gráfica la presión baja hasta cerca de la línea de flujo (8.1 PSI) antes de arrancar, si el arranque es 9.5 PSI.
75. Decidió que la bomba **encienda en el momento en que la presión toca P_ARRANQUE**, sin la espera de confirmación de 1.2 s. Aplicado (se eliminó T_DEMANDA). **Sin compilar.**

76. Ordenó **simular, graficar, compilar y subir**. Compilados los tres entornos (producción 6.046 B); simulador **12 de 15** (las 3 fallas: consumo igual a la capacidad de la bomba); tablero actualizado; subido a GitHub.

## 4. Pendientes por decidir / datos que faltan

- Volumen real del pulmón, presión real de la calle, altura de cierre y caudal de la bomba (curva H‑Q).
- Valores iniciales implementados para la nueva detección (a calibrar en campo): `DELTA_P_CEBADO` 1.5 PSI, pendiente mínima 0.3 PSI/s, δ sobre la estática 1.0 PSI, `P_ESTATICA_MAX` 7.1 PSI (supuesto: medir con `E` con el tanque lleno).
- Valor de la pendiente mínima única `PENDIENTE_MIN_SUBIDA` (0.3 PSI/s, a calibrar) y pedir (o no) la implementación de lo descrito en 2b/2c; calibrar la presión de llenado estable (comando `L`).
- Solución A implementada (falta compilar y simular). Caso B descartado por el usuario.
- Puntos menores de la revisión: los de presupuesto/enfriamiento quedaron sin objeto (pedido 45); `T_SIN_FLUJO` compartido ya resuelto con `T_ESTANCAMIENTO`.
- Caso B (falsas alarmas al arrancar con consumo): decisión del usuario = instruir a los operadores (esperar el apagado de la bomba antes de abrir válvulas); no se cambia el código.
- Falsas alarmas de "calle seca" cuando la casa consume durante el arranque: sin resolver (el simulador las reproduce).
- Medir en campo: `E` (tanque lleno y casi vacío), `L`, `F`, `S`, `C`.
