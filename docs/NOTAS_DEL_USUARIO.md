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

## 4. Pendientes por decidir / datos que faltan

- Volumen real del pulmón, presión real de la calle, altura de cierre y caudal de la bomba (curva H‑Q).
- Valores iniciales implementados para la nueva detección (a calibrar en campo): `DELTA_P_CEBADO` 1.5 PSI, pendiente mínima 0.3 PSI/s, δ sobre la estática 1.0 PSI, `P_ESTATICA_MAX` 7.1 PSI (supuesto: medir con `E` con el tanque lleno).
- Falsas alarmas de "calle seca" cuando la casa consume durante el arranque: sin resolver (el simulador las reproduce).
- Medir en campo: `E` (tanque lleno y casi vacío), `L`, `F`, `S`, `C`.
