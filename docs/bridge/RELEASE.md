# Meshtastic ↔ Bitchat 1.0.0-beta.2

**Versão beta, sem aceite em aparelho.** Os testes desta versão são simulações em computador e
compilação. Nenhum dos dez alvos foi ligado com este firmware, e oito deles nunca receberam
versão alguma da ponte. Fazer backup privado antes de instalar. Não há garantia de risco zero.

**`1.0.0-beta.2` e `1.0.0-beta` têm o mesmo firmware.** A `.2` muda a entrega: um único ZIP
para todos os aparelhos, em vez de um pacote por modelo.

## O que muda em relação à `experimental.5`

- **`!lora` opcional.** Todo texto público do `#mesh` segue para o canal LoRa como
  `apelido: texto`. Em `Settings`, `LoRa: only !lora` volta a exigir o prefixo.
- **Conversa em grupo.** Filas Bluetooth maiores, cópias reconhecidas antes da conferência de
  assinatura, rajada de seis mensagens por pessoa em vez de uma a cada 30 s, repetição nativa
  do Meshtastic no canal e fim do bloqueio permanente entre duas pontes.
- **Texto longo** é dividido em até três partes em vez de ser recusado.
- **Hora após falta de energia.** Sem hora confiável, o primeiro telefone Bitchat que conecta
  fornece a hora. Um telefone próximo pode adiantar esse relógio até a hora confiável voltar.
- **Menu e tela de estado** em frases, com orientação do que fazer.
- **Oito aparelhos novos**, todos ESP32-S3.

[Mudanças, motivos, limites e evidência](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.2/docs/bridge/CANDIDATE.md).

**Atenção ao padrão novo:** qualquer pessoa que escreva no `#mesh` ao alcance da ponte tem o
texto publicado no canal LoRa `Bitchat`, cuja chave é pública.

## Baixar para instalar

Baixar **`meshtastic-bitchat-1.0.0-beta.2.zip`**. É um arquivo só, para todos os aparelhos. No
[flasher oficial](https://flasher.meshtastic.org/), escolher a placa, carregar o ZIP e usar
**Atualizar / Update**. Não usar **Apagar Flash e Instalar**. O flasher grava somente a
aplicação da placa escolhida; escolher a placa errada pode impedir o boot. Nas placas com
opção de interface colorida (MUI), deixar desmarcada.

Aparelhos: Heltec V3, Heltec V4 (OLED), Heltec Wireless Stick Lite V3, Heltec Wireless Tracker
V1.1, Heltec Wireless Paper, LILYGO T-Beam Supreme, Seeed XIAO ESP32-S3 com Wio-SX1262, LILYGO
T-Deck, Station G2 e Cardputer ADV com Cap LoRa-1262.

Não há binário para T1000-E, RAK4631, T-Echo, T-Beam clássico, T-LoRa32, T3-S3, Heltec V2,
Cardputer original nem para as variantes com interface colorida (`-tft`).

O `technical-package` traz os pacotes por modelo, o verificador e o
[roteiro opcional de backup](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.2/docs/bridge/WINDOWS_RECOVERY.md).

## Usar

A ponte sai configurada; os ajustes do menu são opcionais. Abrir o Bitchat perto do rádio e
conversar no `#mesh`. O canal padrão é comum a todas as pontes ao alcance do LoRa; para um
grupo fechado, escolher no menu um canal com chave própria. No rádio remoto com firmware original,
adicionar somente o canal `Bitchat` com a [chave pública](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.2/docs/bridge/DEFAULTS.md). O texto do canal
aparece no `#mesh` como `[MT nome do nó] texto`. Para mudar o modo: `System → Bitchat bridge`.
Em rádio sem tela valem os padrões automáticos.

## Verificação e arquivos técnicos

A publicação depende, para os dez alvos, de compilação, reconstrução idêntica, análise
estática e dos testes de host: protocolo com 19 vetores oficiais e 100.000 entradas
malformadas, adaptador Bluetooth, memória, menu, padrões, inicialização, pacotes e o fluxo
completo com três telefones e um rádio simulados. Nada disso equivale a ensaio com
controlador Bluetooth, iOS ou rádio reais.

`technical-package-1.0.0-beta.2.tar.gz` reúne fontes correspondentes, dependências,
licenças, símbolos, manifestos e resultados dos testes. Não é arquivo para o flasher.
A base permanece Meshtastic 2.7.26.54e0d8d.

## English

**Same firmware as `1.0.0-beta`; `.2` ships one ZIP for every board. No hardware acceptance.** This version was checked by host simulation and compilation only.
None of the ten targets has been run with it; eight never ran any version of the bridge.

Changes since `experimental.5`: `!lora` is optional (every public `#mesh` message goes to the LoRa channel
as `nick: text`; `Settings → LoRa: only !lora` restores the prefix); group chat losses fixed
(deeper Bluetooth queues, relayed copies recognised before signature checks, a burst of six
messages per person, Meshtastic's own retransmission, no permanent block between two bridges);
long text is split in up to three parts; a radio without a trusted clock takes the time from
the first Bitchat phone that connects (a nearby phone can push that clock ahead until a
trusted source returns); plain-language menu and status; eight more ESP32-S3 devices.

Download the single `meshtastic-bitchat-1.0.0-beta.2.zip`, select the board in the official flasher,
load the ZIP and use **Update**. Do not erase flash. The bridge comes configured; menu
adjustments are optional. With the new default, anyone writing
in `#mesh` within reach of the bridge has that text published on the public `Bitchat` LoRa
channel. [Details in English](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.2/docs/bridge/CANDIDATE.md#english).
