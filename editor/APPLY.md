# Revertendo o suporte X3, mantendo as correções de layout

Este pacote volta o firmware ao estado de antes da portabilidade para o X3
(X4 puro, como funcionava bem), mas mantém as correções de espaçamento de
cabeçalho/rodapé/listas (menu principal, arquivos, configurações, Bluetooth,
redes Wi-Fi) e a correção da quebra de linha cedo demais no editor — essas
não têm nenhuma relação com X3/X4, são melhorias válidas para o X4 sozinho
também.

## 1. Copiar os arquivos

```bash
cd ~/caminho/para/microslate-firmware-US-International

unzip -o ~/Downloads/revert-to-x4.zip -d /tmp/revert-to-x4
cp -r /tmp/revert-to-x4/. .
```

Isso restaura `lib/EInkDisplay`, `lib/BatteryMonitor`, `lib/InputManager`,
`lib/SDCardManager` (as libs originais, removidas durante a portabilidade),
`platformio.ini` (volta pro ambiente único `xteink_x4`), `HalGPIO`/
`HalDisplay`/`GfxRenderer` (voltam à versão de constantes fixas do X4),
`main.cpp` (volta ao original, só com a correção do `charsPerLine`),
`file_manager.cpp` (volta a usar `SdMan.sleep()`), e os workflows do
`.github` (voltam ao formato anterior, sem menção ao freeink-sdk).

`src/ui_renderer.cpp` **não está no zip** — ele já está correto no seu
repositório atual (essas correções continuam exatamente como estavam,
sem mudança nenhuma agora).

## 2. Remover o que não existe mais nessa versão

```bash
# Arquivos/scripts que só existiam pra portabilidade X3
rm -f README-X3-PORT.md
rm -rf scripts/patch-freeink-sdk
rm -f .github/workflows/build-all.yml
rm -f sdkconfig.xteink_x3_x4

# O submódulo freeink-sdk não é mais usado
git submodule deinit -f freeink-sdk
git rm -f freeink-sdk
rm -rf .git/modules/freeink-sdk
```

## 3. Revisar e commitar

```bash
git status
git diff --stat

git add -A
git commit -m "Revert X3 hardware support, keep UI spacing fixes

Reverts platformio.ini, HalGPIO, HalDisplay, GfxRenderer, main.cpp's
device-detection changes, and CI workflows back to the X4-only setup
that was working well. Keeps the menu/list/header/footer spacing
fixes (main menu, file browser, settings, bluetooth, wifi list) and
the editor's charsPerLine fix — none of those are X3/X4-specific."

git push
```

## 4. Build

Ambiente volta a ser `xteink_x4` (não mais `xteink_x3_x4`):

```bash
pio run -e xteink_x4
```

Ou pelo `build-all`/`build.yml` normalmente — eles já apontam pro ambiente
certo depois do revert.
