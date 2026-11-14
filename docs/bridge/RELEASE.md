# Meshtastic ↔ Bitchat 1.0.0-beta

**Versão beta, sem aceite em aparelho.** Os testes desta versão são simulações em computador e
compilação. Nenhum dos dez alvos foi ligado com este firmware, e oito deles nunca receberam
versão alguma da ponte. Fazer backup privado antes de instalar. Não há garantia de risco zero.

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

[Mudanças, motivos, limites e evidência](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta/docs/bridge/CANDIDATE.md).

**Atenção ao padrão novo:** qualquer pessoa que escreva no `#mesh` ao alcance da ponte tem o
texto publicado no canal LoRa `Bitchat`, cuja chave é pública.

## Baixar para instalar

Escolher nos anexos o ZIP do modelo exato:

| Aparelho                         | ZIP começa por                                |
| -------------------------------- | --------------------------------------------- |
| Heltec V3                        | `meshtastic-bitchat-heltec-v3-`               |
| Heltec V4 (OLED)                 | `meshtastic-bitchat-heltec-v4-`               |
| Heltec Wireless Stick Lite V3    | `meshtastic-bitchat-heltec-wsl-v3-`           |
| Heltec Wireless Tracker V1.1     | `meshtastic-bitchat-heltec-wireless-tracker-` |
| Heltec Wireless Paper            | `meshtastic-bitchat-heltec-wireless-paper-`   |
| LILYGO T-Beam Supreme            | `meshtastic-bitchat-tbeam-s3-core-`           |
| Seeed XIAO ESP32-S3 + Wio-SX1262 | `meshtastic-bitchat-seeed-xiao-s3-`           |
| LILYGO T-Deck                    | `meshtastic-bitchat-t-deck-`                  |
| Station G2                       | `meshtastic-bitchat-station-g2-`              |
| Cardputer ADV + Cap LoRa-1262    | `meshtastic-bitchat-m5stack-cardputer-adv-`   |

Fazer backup privado e conferência pelo [roteiro Windows](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta/docs/bridge/WINDOWS_RECOVERY.md). No
[flasher oficial](https://flasher.meshtastic.org/), selecionar o modelo, carregar o ZIP e usar
**Atualizar / Update**. Não usar **Apagar Flash e Instalar**. O ZIP contém só a aplicação; o
`-update.bin` do mesmo modelo é a alternativa. Conferir os hashes em `SHA256SUMS`.

Não há binário para T1000-E, RAK4631, T-Echo, T-Beam clássico, T-LoRa32, T3-S3, Heltec V2,
Cardputer original nem para as variantes com interface colorida (`-tft`). Um ZIP só é aceito
pelo verificador para o próprio modelo.

## Usar

Abrir o Bitchat perto do rádio e conversar no `#mesh`. No rádio remoto com firmware original,
adicionar somente o canal `Bitchat` com a [chave pública](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta/docs/bridge/DEFAULTS.md). O texto do canal
aparece no `#mesh` como `[MT nome do nó] texto`. Para mudar o modo: `System → Bitchat bridge`.
Em rádio sem tela valem os padrões automáticos.

## Verificação e arquivos técnicos

A publicação depende, para os dez alvos, de compilação, reconstrução idêntica, análise
estática e dos testes de host: protocolo com 19 vetores oficiais e 100.000 entradas
malformadas, adaptador Bluetooth, memória, menu, padrões, inicialização, pacotes e o fluxo
completo com três telefones e um rádio simulados. Nada disso equivale a ensaio com
controlador Bluetooth, iOS ou rádio reais.

`technical-package-1.0.0-beta.tar.gz` reúne fontes correspondentes, dependências,
licenças, símbolos, manifestos e resultados dos testes. Não é arquivo para o flasher.
A base permanece Meshtastic 2.7.26.54e0d8d.

## English

**No hardware acceptance.** This version was checked by host simulation and compilation only.
None of the ten targets has been run with it; eight never ran any version of the bridge.

Changes since `experimental.5`: `!lora` is optional (every public `#mesh` message goes to the LoRa channel
as `nick: text`; `Settings → LoRa: only !lora` restores the prefix); group chat losses fixed
(deeper Bluetooth queues, relayed copies recognised before signature checks, a burst of six
messages per person, Meshtastic's own retransmission, no permanent block between two bridges);
long text is split in up to three parts; a radio without a trusted clock takes the time from
the first Bitchat phone that connects (a nearby phone can push that clock ahead until a
trusted source returns); plain-language menu and status; eight more ESP32-S3 devices.

Choose the ZIP for the exact model from the table above, make a private backup, and use
**Update** in the official flasher. Do not erase flash. With the new default, anyone writing
in `#mesh` within reach of the bridge has that text published on the public `Bitchat` LoRa
channel. [Details in English](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta/docs/bridge/CANDIDATE.md#english).
