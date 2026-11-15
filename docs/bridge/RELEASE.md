# Meshtastic ↔ Bitchat 1.0.0-beta.9

**Versão beta, com um aparelho relatado em funcionamento na versão anterior.** Em um Heltec V3
com Bitchat iOS 1.8.0, a `1.0.0-beta.4` teve o telefone reconhecido (`Phones 1/1`, `People 2`)
e texto nos dois sentidos (`LoRa out 2 in 1`); o relatório `!bridge` dessa sessão mostrou o
caminho novo em uso: `path 9`, `in 230 out 59`, `wr 0`. É uma sessão em um aparelho, sem ensaio
de duração. **As mudanças da `1.0.0-beta.5` à `1.0.0-beta.9` foram simuladas e compiladas, sem ensaio
em aparelho.** Os outros nove alvos foram apenas compilados. Fazer backup privado antes de
instalar. Não há garantia de risco zero.

**O que a `1.0.0-beta.9` muda em relação à `1.0.0-beta.8`:**

- **Pacotes por país, ao lado do pacote normal.** `meshtastic-bitchat-1.0.0-beta.9-Brasil.zip`,
  `…-EUA.zip` e `…-Europa-e-Reino-Unido.zip`. São para entregar um rádio pronto a quem só vai usar
  o Bitchat: se o rádio **nunca teve região**, a ponte grava a do país na primeira partida
  (`ANZ`, `US` ou `EU_868`) e o aplicativo Meshtastic deixa de ser necessário. Rádio que já tem
  região não é alterado. O pacote normal continua sem definir região.
  [Detalhes e limites](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.9/docs/bridge/DEFAULTS.md#pacotes-por-país).
  Transmitir em faixa não permitida no lugar de uso pode ser proibido. **Só compilado; não foi
  ensaiado em aparelho.**

**O que a `1.0.0-beta.8` mudou em relação à `1.0.0-beta.7`:**

- **Guia dentro da ponte.** `!bridge guide` (em inglês) ou `!ponte guia` (em português) explica a
  ponte em quatro mensagens curtas: o que atravessa, como o texto do LoRa aparece, os limites e
  como ver o estado. Vale no aplicativo Meshtastic conectado ao rádio e também no `#mesh` do
  Bitchat, para quem só usa o Bitchat; ali a resposta é assinada pela ponte e sai no máximo uma
  vez a cada 30 segundos. A primeira linha acompanha o modo em uso (todo o texto, só o iniciado
  por `!lora`, ou sem LoRa). **Só simulado; não foi ensaiado em aparelho.**
- `!bridge help` foi encurtado para caber em uma mensagem e cita o guia.

**O que a `1.0.0-beta.7` mudou em relação à `1.0.0-beta.6`:**

- **`Battery saver` por texto.** O ajuste que já existia no menu da tela passa a ter comando,
  para rádios sem tela: `!bridge economy on` liga e `!bridge economy off` desliga, pelo
  aplicativo Meshtastic conectado ao próprio rádio. A escolha fica gravada e continua desligada
  de fábrica. O relatório `!bridge` passa a mostrar `Saver on` ou `Saver off`. O que o ajuste
  faz não mudou: anúncio Bluetooth cerca de uma vez por segundo, 1% de tempo de transmissão em
  vez de 10%, um texto por pessoa a cada 30 s depois da rajada e transmissão única no LoRa.
  Com ele ligado, o Bitchat e o aplicativo Meshtastic podem demorar mais para encontrar o
  rádio. O ganho de bateria não foi medido. **Só simulado; não foi ensaiado em aparelho.**

**O que a `1.0.0-beta.6` mudou em relação à `1.0.0-beta.5`:**

- **Ligação com o MeshCore pelo Bitchat.** Quando esta ponte e uma ponte
  [MeshCore–Bitchat](https://github.com/EduChacon/Meshcore-Bitchat) estão ao alcance Bluetooth
  dos mesmos telefones Bitchat, o que a ponte MeshCore traz ao Bitchat, `[MC nome] texto`, sai
  no canal do Meshtastic como `MC nome: texto`. Cada texto atravessa uma vez só: o que já veio
  da outra rede chega marcado e nenhuma ponte o leva de volta. Vem ligada; `!bridge link off`
  desliga e `!bridge link on` religa, pelo aplicativo Meshtastic conectado ao próprio rádio.
  Com mais de um lugar ligando as mesmas duas redes, o mesmo texto pode aparecer repetido.
  **Só simulada; não foi ensaiada em aparelho.**

**O que a `1.0.0-beta.5` mudou em relação à `1.0.0-beta.4`:**

- **Rádio sem tela.** Os modos do menu passam a ter comandos de texto, aceitos somente do
  aplicativo Meshtastic conectado ao próprio rádio: `!bridge off`, `!bridge on`,
  `!bridge local` (só Bluetooth) e `!bridge lora` (Bitchat + LoRa no canal em que o comando é
  enviado, que precisa ser secundário, com nome e chave). `!bridge help` lista os comandos.
  Vindos de outro rádio ou do Bitchat, não mudam nada.
- **O aplicativo Meshtastic encontra a ponte sem o menu.** O serviço do Meshtastic passa a
  constar da resposta de varredura do anúncio da ponte, com o nome do próprio rádio (o nome
  inteiro quando cabe em 11 caracteres; senão, `Mesh_` e os quatro dígitos finais). Antes, ele
  só aparecia na janela `Find Meshtastic app`, que depende de tela. **Isso muda o anúncio
  Bluetooth do Heltec V3 já ensaiado e não foi verificado em aparelho.**
- **`!bridge` com a ponte desligada** passa a orientar o comando `!bridge on`.
- **SenseCAP T1000-E, experimental.** O arquivo `EXPERIMENTAL-t1000-e-bitchat-1.0.0-beta.9.uf2`
  acompanha esta versão, à parte. Compila, mas **nunca foi ligado em aparelho**, não entra no
  ZIP de instalação nem nas verificações da versão, e grava-se por UF2, não pelo flasher. Antes
  de usar, ler [estado, riscos e roteiro](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.9/docs/bridge/T1000E.md).

O uso diário não exige o aplicativo Meshtastic: o canal `Bitchat` é criado pela ponte e a hora
pode vir do primeiro telefone Bitchat. A exceção é a região do LoRa, que um rádio nunca
configurado precisa receber uma vez de um cliente Meshtastic.

**O que foi medido no aparelho antes da `1.0.0-beta.4`.** O Bitchat se conecta ao rádio e se inscreve no serviço da
ponte, mas a ponte não recebe escrita alguma dele (`rx 0`), com o aplicativo Meshtastic aberto
ou fechado. O relógio estava certo, a ponte enviava o anúncio a cada 20 s e nada era recusado,
porque nada chegava. A `experimental.5`, anterior a estas mudanças, falhou do mesmo modo no
mesmo telefone. A causa **não está determinada**: o aplicativo só escreve quando o iOS informa
que a conexão aceita escrita sem resposta, e o código dele não tem outro modo de entregar
dados a um periférico.

**O que a `1.0.0-beta.4` mudou em relação à `1.0.0-beta.3`:**

- **Segundo caminho de dados.** Depois que o telefone se inscreve, a ponte procura, na mesma
  conexão, o serviço Bitchat que o próprio aplicativo publica e se inscreve nele. Os pacotes
  do telefone passam a chegar como notificações, e os da ponte saem como escritas de um quadro
  inteiro. É o caminho pelo qual dois telefones Bitchat trocam dados. Foi o caminho em uso na
  sessão relatada acima (`path 9`), em um aparelho.
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

[Mudanças, motivos, limites e evidência](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.9/docs/bridge/CHANGES.md).

**Atenção ao padrão novo:** qualquer pessoa que escreva no `#mesh` ao alcance da ponte tem o
texto publicado no canal LoRa `Bitchat`, cuja chave é pública.

## Baixar para instalar

Baixar **`meshtastic-bitchat-1.0.0-beta.9.zip`**. É um arquivo só, para todos os aparelhos. No
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
[roteiro opcional de backup](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.9/docs/bridge/WINDOWS_RECOVERY.md).

## Usar

A ponte sai configurada; os ajustes do menu são opcionais. Abrir o Bitchat perto do rádio e
conversar no `#mesh`. O canal padrão é comum a todas as pontes ao alcance do LoRa; para um
grupo fechado, escolher no menu um canal com chave própria. No rádio remoto com firmware original,
adicionar somente o canal `Bitchat` com a [chave pública](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.9/docs/bridge/DEFAULTS.md). O texto do canal
aparece no `#mesh` como `[MT nome do nó] texto`. Para mudar o modo: `System → Bitchat bridge`.
Em rádio sem tela valem os padrões automáticos.

## Verificação e arquivos técnicos

A publicação depende, para os dez alvos, de compilação, reconstrução idêntica, análise
estática e dos testes de host: protocolo com 19 vetores oficiais e 100.000 entradas
malformadas, adaptador Bluetooth, memória, menu, padrões, inicialização, pacotes e o fluxo
completo com três telefones e um rádio simulados. Nada disso equivale a ensaio com
controlador Bluetooth, iOS ou rádio reais.

`technical-package-1.0.0-beta.9.tar.gz` reúne fontes correspondentes, dependências,
licenças, símbolos, manifestos e resultados dos testes. Não é arquivo para o flasher.
A base permanece Meshtastic 2.7.26.54e0d8d.

## English

**One device reported working, on the previous version.** With `1.0.0-beta.4` on a Heltec V3 and Bitchat iOS 1.8.0, the operator reported the phone recognised (`Phones 1/1`, `People 2`) and text in both directions (`LoRa out 2 in 1`); the `!bridge` report of that session showed the new path in use (`path 9`, `in 230 out 59`, `wr 0`). It is one session on one device, with no endurance test. Versions up to `1.0.0-beta.3` did not exchange messages with the app: the phone connects and subscribes to the bridge service, but the bridge receives no write from it, with the Meshtastic app open or closed. The earlier `experimental.5` fails the same way on the same phone. The cause is not determined; the app only writes when iOS reports that the link accepts writes without response, and it has no other way to deliver data to a peripheral.

**New in `1.0.0-beta.9` (compiled, not tested on a device):** one package per country beside the normal one (`…-Brasil.zip`, `…-EUA.zip`, `…-Europa-e-Reino-Unido.zip`), meant for handing a ready radio to someone who will only use Bitchat. If the radio never had a region, the bridge stores the country's one on first start (`ANZ`, `US` or `EU_868`), so the Meshtastic app is not needed; a radio that already has a region is left alone, and the normal package still sets none. Transmitting on a band not allowed where the radio is used may be illegal.

**Added in `1.0.0-beta.8` (simulated and compiled, not tested on a device):** a guide inside the bridge. `!bridge guide` (English) or `!ponte guia` (Portuguese) explains it in four short messages, from the Meshtastic app connected to the radio or from Bitchat `#mesh` itself, where the signed answer is sent at most every 30 seconds. The first line follows the mode in use. `!bridge help` now fits one message and names the guide.

**Added in `1.0.0-beta.7` (simulated and compiled, not tested on a device):** the screen menu's `Battery saver` can be chosen by text, for radios without a screen: `!bridge economy on` and `!bridge economy off`, from the Meshtastic app connected to the radio itself. The choice is stored and stays off by default; `!bridge` shows `Saver on` or `Saver off`. What the setting does is unchanged (Bluetooth advertising about once a second, 1% extra airtime instead of 10%, one text per person every 30 s after the burst, single LoRa transmission); with it on, apps may take longer to find the radio. The battery gain was not measured.

**Added in `1.0.0-beta.6` (simulated and compiled, not tested on a device):** text that a [MeshCore–Bitchat bridge](https://github.com/EduChacon/Meshcore-Bitchat) brings to Bitchat (`[MC name] text`) is taken to the Meshtastic channel once, as `MC name: text`, when both bridges are within Bluetooth range of the same Bitchat phones; text that already crossed is marked and never crosses again; on by default, `!bridge link off` / `!bridge link on`.

**Since `1.0.0-beta.4` (simulated and compiled, not tested on a device):** text commands for radios without a screen, accepted only from the Meshtastic app connected to the radio itself (`!bridge off`, `!bridge on`, `!bridge local`, `!bridge lora` on the channel where it is sent, `!bridge help`); the Meshtastic service and the radio's own name in the scan response of the bridge advertisement, so that app finds the radio without the screen menu (this changes the advertisement of the Heltec V3 already tried); and a separate, experimental `EXPERIMENTAL-t1000-e-bitchat-1.0.0-beta.9.uf2` for the SenseCAP T1000-E, which compiles but has never run on a device and is outside the release checks.

**From `1.0.0-beta.3` to `1.0.0-beta.4`:** a second data path, in which the bridge looks for the Bitchat service the app itself publishes, over the same link, and subscribes to it, so the phone's packets arrive as notifications and the bridge's leave as whole-frame writes (the path in use in the session reported above); an MTU exchange requested on connect; notifications only to phones whose writes have arrived; LoRa text in parts of at most 96 bytes; per-link measurements in `!bridge`; `Phones talking/subscribed` on the screen; a real countdown for `Starting LoRa`. Correction: the 22-byte bridge marker was already present in `experimental.5`, not introduced in `1.0.0-beta` as the `1.0.0-beta.3` notes said.

This version was checked by host simulation and compilation only. The other nine targets were only compiled; eight never ran any version of the bridge.

Changes since `experimental.5`: `!lora` is optional (every public `#mesh` message goes to the LoRa channel
as `nick: text`; `Settings → LoRa: only !lora` restores the prefix); group chat losses fixed
(deeper Bluetooth queues, relayed copies recognised before signature checks, a burst of six
messages per person, Meshtastic's own retransmission, no permanent block between two bridges);
long text is split in up to three parts; a radio without a trusted clock takes the time from
the first Bitchat phone that connects (a nearby phone can push that clock ahead until a
trusted source returns); plain-language menu and status; eight more ESP32-S3 devices.

Download the single `meshtastic-bitchat-1.0.0-beta.9.zip`, select the board in the official flasher,
load the ZIP and use **Update**. Do not erase flash. The bridge comes configured; menu
adjustments are optional. With the new default, anyone writing
in `#mesh` within reach of the bridge has that text published on the public `Bitchat` LoRa
channel. [Details in English](https://github.com/EduChacon/meshtastic-bitchat/blob/v1.0.0-beta.9/docs/bridge/CHANGES.md#english).
