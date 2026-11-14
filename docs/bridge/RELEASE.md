# Meshtastic ↔ Bitchat 1.0.0-beta.4

**Versão beta, com um aparelho relatado em funcionamento.** Em um Heltec V3 com Bitchat iOS
1.8.0, as versões até a `1.0.0-beta.3` não trocavam mensagens com o aplicativo. Com esta
versão, o operador relatou o telefone reconhecido (`Phones 1/1`, `People 2`) e texto nos dois
sentidos (`LoRa out 2 in 1`). O relatório `!bridge` dessa sessão mostrou o caminho novo em
uso: `path 9`, `in 230 out 59`, `wr 0`. É uma sessão em um aparelho, sem ensaio de duração. Os outros nove alvos foram apenas compilados. Fazer backup privado antes de
instalar. Não há garantia de risco zero.

**O que foi medido no aparelho antes desta versão.** O Bitchat se conecta ao rádio e se inscreve no serviço da
ponte, mas a ponte não recebe escrita alguma dele (`rx 0`), com o aplicativo Meshtastic aberto
ou fechado. O relógio estava certo, a ponte enviava o anúncio a cada 20 s e nada era recusado,
porque nada chegava. A `experimental.5`, anterior a estas mudanças, falhou do mesmo modo no
mesmo telefone. A causa **não está determinada**: o aplicativo só escreve quando o iOS informa
que a conexão aceita escrita sem resposta, e o código dele não tem outro modo de entregar
dados a um periférico.

**O que a `1.0.0-beta.4` muda em relação à `1.0.0-beta.3`:**

- **Segundo caminho de dados.** Depois que o telefone se inscreve, a ponte procura, na mesma
  conexão, o serviço Bitchat que o próprio aplicativo publica e se inscreve nele. Os pacotes
  do telefone passam a chegar como notificações, e os da ponte saem como escritas de um quadro
  inteiro. É o caminho pelo qual dois telefones Bitchat trocam dados. **Não está comprovado em
  aparelho que o iOS expõe esse serviço a um periférico conectado.**
- **Pedido imediato de MTU.** No MTU padrão de 23 bytes nada do que o aplicativo envia cabe em
  uma escrita; a ponte pede a troca ao conectar, em vez de esperar o telefone.
- **A ponte só fala com quem responde.** Sem o serviço do telefone, ela só envia por
  notificação a um telefone cujas escritas já chegaram, e repete a busca a cada 10 s enquanto
  ele estiver mudo.
- **Texto do LoRa em partes de até 96 bytes**, para que o quadro assinado caiba em uma escrita
  no MTU de 185 bytes.
- **Medições por conexão no `!bridge`**: MTU, criptografia, pareamento, inscrição, escritas e
  o caminho em uso. A tela mostra `Phones 0/1` para um telefone conectado e mudo.
- **`Starting LoRa` com contagem regressiva.**

**Correção de uma nota anterior:** o marcador de ponte de 22 bytes no anúncio já existia na
`experimental.5`; não foi introduzido na `1.0.0-beta`, como as notas da `1.0.0-beta.3`
afirmavam. A redução para 8 bytes, feita na `.3`, continua válida.

**O que a `1.0.0-beta.3` mudou:** comando de teste `!bridge` (ou `!ponte`), `!bridge reset` e
`Settings → Reconnect phones`, e o anúncio da ponte reduzido de 196 para 182 bytes.

**O que a `1.0.0-beta.2` mudou:** um único ZIP para todos os aparelhos; canal padrão `Bitchat`
recriado sempre que faltar; texto longo do Meshtastic entregue ao Bitchat em partes curtas; e
`Settings → Diagnostics`.

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

[Mudanças, motivos, limites e evidência](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.4/docs/bridge/CHANGES.md).

**Atenção ao padrão novo:** qualquer pessoa que escreva no `#mesh` ao alcance da ponte tem o
texto publicado no canal LoRa `Bitchat`, cuja chave é pública.

## Baixar para instalar

Baixar **`meshtastic-bitchat-1.0.0-beta.4.zip`**. É um arquivo só, para todos os aparelhos. No
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
[roteiro opcional de backup](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.4/docs/bridge/WINDOWS_RECOVERY.md).

## Usar

A ponte sai configurada; os ajustes do menu são opcionais. Abrir o Bitchat perto do rádio e
conversar no `#mesh`. O canal padrão é comum a todas as pontes ao alcance do LoRa; para um
grupo fechado, escolher no menu um canal com chave própria. No rádio remoto com firmware original,
adicionar somente o canal `Bitchat` com a [chave pública](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.4/docs/bridge/DEFAULTS.md). O texto do canal
aparece no `#mesh` como `[MT nome do nó] texto`. Para mudar o modo: `System → Bitchat bridge`.
Em rádio sem tela valem os padrões automáticos.

## Verificação e arquivos técnicos

A publicação depende, para os dez alvos, de compilação, reconstrução idêntica, análise
estática e dos testes de host: protocolo com 19 vetores oficiais e 100.000 entradas
malformadas, adaptador Bluetooth, memória, menu, padrões, inicialização, pacotes e o fluxo
completo com três telefones e um rádio simulados. Nada disso equivale a ensaio com
controlador Bluetooth, iOS ou rádio reais.

`technical-package-1.0.0-beta.4.tar.gz` reúne fontes correspondentes, dependências,
licenças, símbolos, manifestos e resultados dos testes. Não é arquivo para o flasher.
A base permanece Meshtastic 2.7.26.54e0d8d.

## English

**One device reported working.** With `1.0.0-beta.4` on a Heltec V3 and Bitchat iOS 1.8.0, the operator reported the phone recognised (`Phones 1/1`, `People 2`) and text in both directions (`LoRa out 2 in 1`); the `!bridge` report of that session showed the new path in use (`path 9`, `in 230 out 59`, `wr 0`). It is one session on one device, with no endurance test. Versions up to `1.0.0-beta.3` did not exchange messages with the app: the phone connects and subscribes to the bridge service, but the bridge receives no write from it, with the Meshtastic app open or closed. The earlier `experimental.5` fails the same way on the same phone. The cause is not determined; the app only writes when iOS reports that the link accepts writes without response, and it has no other way to deliver data to a peripheral.

**Since `1.0.0-beta.3`:** a second data path, in which the bridge looks for the Bitchat service the app itself publishes, over the same link, and subscribes to it, so the phone's packets arrive as notifications and the bridge's leave as whole-frame writes (not yet shown on hardware that iOS exposes that service to a connected peripheral); an MTU exchange requested on connect; notifications only to phones whose writes have arrived; LoRa text in parts of at most 96 bytes; per-link measurements in `!bridge`; `Phones talking/subscribed` on the screen; a real countdown for `Starting LoRa`. Correction: the 22-byte bridge marker was already present in `experimental.5`, not introduced in `1.0.0-beta` as the `1.0.0-beta.3` notes said.

This version was checked by host simulation and compilation only. The other nine targets were only compiled; eight never ran any version of the bridge.

Changes since `experimental.5`: `!lora` is optional (every public `#mesh` message goes to the LoRa channel
as `nick: text`; `Settings → LoRa: only !lora` restores the prefix); group chat losses fixed
(deeper Bluetooth queues, relayed copies recognised before signature checks, a burst of six
messages per person, Meshtastic's own retransmission, no permanent block between two bridges);
long text is split in up to three parts; a radio without a trusted clock takes the time from
the first Bitchat phone that connects (a nearby phone can push that clock ahead until a
trusted source returns); plain-language menu and status; eight more ESP32-S3 devices.

Download the single `meshtastic-bitchat-1.0.0-beta.4.zip`, select the board in the official flasher,
load the ZIP and use **Update**. Do not erase flash. The bridge comes configured; menu
adjustments are optional. With the new default, anyone writing
in `#mesh` within reach of the bridge has that text published on the public `Bitchat` LoRa
channel. [Details in English](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.4/docs/bridge/CHANGES.md#english).
