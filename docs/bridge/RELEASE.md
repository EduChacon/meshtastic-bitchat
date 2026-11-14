# Meshtastic ↔ Bitchat 1.0.0-beta.3

**Versão beta, sem aceite em aparelho.** Os testes desta versão são simulações em computador e
compilação. Nenhum dos dez alvos foi ligado com este firmware, e oito deles nunca receberam
versão alguma da ponte. Fazer backup privado antes de instalar. Não há garantia de risco zero.

**O que a `1.0.0-beta.3` muda em relação à `1.0.0-beta.2`:** comando de teste `!bridge` (ou
`!ponte`), digitado no aplicativo Meshtastic conectado ao rádio da ponte ou no `#mesh` do
Bitchat, com resposta `[bridge] …`; `!bridge reset` e `Settings → Reconnect phones`, que
derrubam as conexões Bluetooth e mandam os telefones lerem de novo os serviços do rádio; e
`Phones` passa a contar só telefones que enviaram algum dado do Bitchat, porque a inscrição
de um telefone pareado é restaurada sozinha quando o aplicativo Meshtastic reconecta.

**O que a `1.0.0-beta.2` mudou em relação à `1.0.0-beta`:** um único ZIP para todos os
aparelhos, em vez de um pacote por modelo; e o canal padrão `Bitchat` passa a ser recriado
sempre que faltar; texto longo vindo do Meshtastic chega ao Bitchat em partes curtas, porque o
aplicativo recusava a assinatura de mensagens de 100 bytes ou mais; e o menu ganhou
`Settings → Diagnostics`, que mostra o relógio usado pela ponte e por que pacotes do telefone
foram recusados.

**Problema em aberto, do primeiro teste em aparelho:** em um Heltec V3 com a `1.0.0-beta`, a
tela mostrou `Ready: Bitchat + LoRa`, `Phones 1` e `People 0`, e a ponte não apareceu na lista
de pessoas do Bitchat. Nenhuma mensagem passou. A causa não foi determinada. Hipótese em
teste, não confirmada: o Bitchat não chegou a se conectar e o `Phones 1` era a inscrição
restaurada do telefone pareado pelo aplicativo Meshtastic. O comando `!bridge` desta versão
mostra `Bluetooth` e `Bitchat` em separado para confirmar ou afastar essa hipótese.

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

[Mudanças, motivos, limites e evidência](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.3/docs/bridge/CHANGES.md).

**Atenção ao padrão novo:** qualquer pessoa que escreva no `#mesh` ao alcance da ponte tem o
texto publicado no canal LoRa `Bitchat`, cuja chave é pública.

## Baixar para instalar

Baixar **`meshtastic-bitchat-1.0.0-beta.3.zip`**. É um arquivo só, para todos os aparelhos. No
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
[roteiro opcional de backup](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.3/docs/bridge/WINDOWS_RECOVERY.md).

## Usar

A ponte sai configurada; os ajustes do menu são opcionais. Abrir o Bitchat perto do rádio e
conversar no `#mesh`. O canal padrão é comum a todas as pontes ao alcance do LoRa; para um
grupo fechado, escolher no menu um canal com chave própria. No rádio remoto com firmware original,
adicionar somente o canal `Bitchat` com a [chave pública](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.3/docs/bridge/DEFAULTS.md). O texto do canal
aparece no `#mesh` como `[MT nome do nó] texto`. Para mudar o modo: `System → Bitchat bridge`.
Em rádio sem tela valem os padrões automáticos.

## Verificação e arquivos técnicos

A publicação depende, para os dez alvos, de compilação, reconstrução idêntica, análise
estática e dos testes de host: protocolo com 19 vetores oficiais e 100.000 entradas
malformadas, adaptador Bluetooth, memória, menu, padrões, inicialização, pacotes e o fluxo
completo com três telefones e um rádio simulados. Nada disso equivale a ensaio com
controlador Bluetooth, iOS ou rádio reais.

`technical-package-1.0.0-beta.3.tar.gz` reúne fontes correspondentes, dependências,
licenças, símbolos, manifestos e resultados dos testes. Não é arquivo para o flasher.
A base permanece Meshtastic 2.7.26.54e0d8d.

## English

**Since `1.0.0-beta.2`: a self-test command, `!bridge` (or `!ponte`), typed in the Meshtastic app connected to the bridge radio or in Bitchat `#mesh`, answered as `[bridge] …`; `!bridge reset` and `Settings → Reconnect phones` drop the Bluetooth links and tell phones to read the radio's services again; `Phones` counts only phones that sent Bitchat data, because a bonded phone's subscription is restored by itself when the Meshtastic app reconnects.**

**Since `1.0.0-beta`: one ZIP for every board; the default `Bitchat` channel is created again whenever it is missing; long LoRa text reaches Bitchat in short parts; `Settings → Diagnostics` shows the bridge clock and refusal reasons. Open problem from the first device test: bridge and phone connected but did not recognise each other (`Phones 1`, `People 0`); cause not yet determined, and the hypothesis under test is that Bitchat never connected.** This version was checked by host simulation and compilation only.
None of the ten targets has been run with it; eight never ran any version of the bridge.

Changes since `experimental.5`: `!lora` is optional (every public `#mesh` message goes to the LoRa channel
as `nick: text`; `Settings → LoRa: only !lora` restores the prefix); group chat losses fixed
(deeper Bluetooth queues, relayed copies recognised before signature checks, a burst of six
messages per person, Meshtastic's own retransmission, no permanent block between two bridges);
long text is split in up to three parts; a radio without a trusted clock takes the time from
the first Bitchat phone that connects (a nearby phone can push that clock ahead until a
trusted source returns); plain-language menu and status; eight more ESP32-S3 devices.

Download the single `meshtastic-bitchat-1.0.0-beta.3.zip`, select the board in the official flasher,
load the ZIP and use **Update**. Do not erase flash. The bridge comes configured; menu
adjustments are optional. With the new default, anyone writing
in `#mesh` within reach of the bridge has that text published on the public `Bitchat` LoRa
channel. [Details in English](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.3/docs/bridge/CHANGES.md#english).
