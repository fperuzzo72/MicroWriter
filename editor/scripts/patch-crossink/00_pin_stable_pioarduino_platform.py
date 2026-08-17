"""Patch em crossink/platformio.ini: pinar uma versão anterior conhecida do
pioarduino/platform-espressif32.

A versão que o CrossInk 1.3.4 traz pinada (55.03.37, lançada em 12/fev/2026)
tem um bug real de empacotamento: o pacote `framework-arduinoespressif32`
não é resolvido/instalado (só o companion `-libs` aparece), e a build falha
em BuildFrameworks() com `FRAMEWORK_DIR` None antes mesmo de tocar em
qualquer arquivo do projeto. Confirmado reproduzível (não é flakiness de
rede) em duas execuções seguidas do Action.

Fixamos a release imediatamente anterior (55.03.36-1, de 10/fev/2026), que
não tem esse problema — é o menor downgrade possível a partir do que o
CrossInk já testou e pinou.
"""

path = "crossink/platformio.ini"
src = open(path).read()

old = "platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip"
new = "platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.36-1/platform-espressif32.zip"

assert old in src, "linha do platform pioarduino não encontrada — CrossInk pode ter mudado a versão pinada"
src = src.replace(old, new)

open(path, "w").write(src)
print("platformio.ini: pioarduino platform fixado em 55.03.36-1 (workaround do bug da 55.03.37)")
