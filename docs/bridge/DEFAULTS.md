# Início rápido e padrões — versão 1.0 beta

## O que a instalação ativa

No primeiro boot, inclusive na primeira atualização a partir de uma revisão anterior, o
firmware ativa o relé Bitchat por Bluetooth e a troca de texto com um canal LoRa. A mudança é
aplicada uma vez e registrada: desligar a ponte ou escolher somente Bluetooth depois disso
continua valendo após reiniciar. Um canal de texto já escolhido e autorizado é preservado.

Se não houver canal escolhido, o firmware cria `Bitchat` como **canal secundário público**,
somente em uma posição desativada e vazia. Usa a API nativa de canais e persiste pelo NodeDB.
Não altera região, preset, slot RF, potência, canal primário ou chaves existentes (a única
exceção são os [pacotes por país](#pacotes-por-país), em rádio que nunca teve região). MQTT e
compartilhamento de posição ficam desativados nesse canal novo. Não formata armazenamento.

Chave pública de interoperabilidade, em Base64:

```text
xes1ntLxyD2pakZvNlqF1/6OzATHxMeQqndOjKBWZFs=
```

É SHA-256 da identificação pública `meshtastic-bitchat/public-text-channel/v1`. **Não é
segredo e não oferece privacidade.** O texto desse canal é publicado no `#mesh` local do
Bitchat, e **todo texto público do `#mesh` ao alcance da ponte é publicado nesse canal**,
mesmo de quem não sabe que a ponte existe. Nome igual sozinho não basta: um canal
preexistente com outra chave, papel ou MQTT não é escolhido nem sobrescrito.

O canal padrão faz parte da configuração de fábrica da ponte: se faltar depois (lista de
canais apagada, configuração restaurada, rádio que veio de uma revisão anterior), ele é
recriado em até 10 segundos. Isso só não acontece se o operador escolheu `Bitchat only` ou
outro canal no menu.

Sem espaço vazio, com nome conflitante, modo radioamador licenciado ou erro de configuração,
a criação não prossegue: o Bluetooth local continua e a tela mostra `No LoRa channel` e
`Free a channel in app`. Ao liberar uma posição no aplicativo Meshtastic, o canal é criado. Falha de escrita, de identidade ou falta de memória
bloqueia a ponte. Bluetooth desativado ou economia de energia stock não são reconfigurados
automaticamente: corrigir essas opções pelo aplicativo Meshtastic.

## Primeiro uso

1. Após backup, conferência e atualização, manter alimentação USB estável e conferir o boot.
2. Abrir o **Bitchat** perto do rádio, no `#mesh`. Se o rádio estiver sem hora, o primeiro
   telefone Bitchat que conecta fornece a hora. O aplicativo Meshtastic, o GPS e o RTC também
   fornecem e têm precedência. Enquanto falta hora, o rádio alterna o anúncio Bluetooth a
   cada 20 s entre os dois aplicativos.
3. No rádio remoto com firmware original, adicionar **somente o canal `Bitchat`**. O caminho
   mais curto é abrir [este link](https://meshtastic.org/e/?add=true#CisSIMXrNZ7S8cg9qWpGbzZahdf-jswEx8THkKp3ToygVmRbGgdCaXRjaGF0)
   no telefone com o aplicativo Meshtastic e confirmar a **adição** do canal; também serve
   digitar o nome e a chave pública acima, ou compartilhar o canal pela função do aplicativo.
   O link foi montado a partir do formato de canais do Meshtastic e não foi ensaiado: se o
   aplicativo propuser **substituir** os canais em vez de adicionar, cancelar e digitar a chave. Os dois rádios
   precisam de região, preset e frequência compatíveis.
4. Aguardar os 30 segundos iniciais (`Starting LoRa: 30 s`) e conversar:
   - texto no canal `Bitchat` do Meshtastic aparece no `#mesh` como `[MT nome do nó] texto`;
   - texto no `#mesh` aparece no canal como `apelido: texto`.

No sentido do LoRa, texto com mais de 200 bytes é dividido em até três partes; no sentido do
Bitchat, texto com 100 bytes ou mais chega em até cinco partes curtas. DMs, grupos privados, posição,
credenciais, telemetria, arquivos e voz não são traduzidos. O túnel de laboratório exige
ativação separada e não é necessário para a conversa entre Bitchat e Meshtastic.

## Canal comum e canal próprio

A chave padrão é a mesma em todo rádio com este firmware. Com poucas pontes, isso só facilita.
Com muitas na mesma região, o canal `Bitchat` vira uma sala pública comum: todas as pontes ao
alcance do LoRa trocam texto entre si, sem privacidade. Laços e duplicação entre pontes são
contidos, e há limite por pessoa e de tempo de transmissão, mas a conversa de um grupo se
mistura à dos outros. Para um grupo fechado:

1. no aplicativo Meshtastic, criar um canal secundário com nome e chave próprios, com MQTT
   desligado, no rádio da ponte e nos rádios remotos;
2. no rádio da ponte, `System → Bitchat bridge → Bitchat + LoRa`, escolher esse canal e
   confirmar. A ponte passa a usar somente ele.

Em local público, `Settings → LoRa: only !lora` limita o que sai pelo LoRa ao texto que a
pessoa marcou de propósito.

## Menu e tela

`System → Bitchat bridge` oferece `Bitchat only`, `Bitchat + LoRa`, `Turn bridge off`,
`Status` e `Settings`. Em `Settings`:

- `LoRa: all messages` / `LoRa: only !lora`: no segundo modo, só o texto iniciado por
  `!lora ` sai pelo LoRa. O prefixo é aceito e removido nos dois modos.
- `Battery saver: off / on`: menos transmissões LoRa e anúncio Bluetooth mais espaçado.
- `Find Meshtastic app`: anuncia o serviço Meshtastic por 5 minutos, para parear o aplicativo.
- `Advanced: tunnel`: laboratório, com confirmação própria.
- `Diagnostics`: relógio usado pela ponte e sua origem (`app`, `phone` ou `none`), pacotes
  recebidos do telefone (`rx`), aceitos (`ok`) e enviados (`tx`), e recusas por motivo:
  `old` (horário fora da tolerância de 2 minutos), `sig` (identidade ou assinatura), `fmt`
  (formato), `ver` (versão não suportada), `pol` (regra da ponte) e `full` (fila cheia).

- `Reconnect phones`: avisa os telefones para lerem de novo os serviços do rádio e, 3 segundos
  depois, derruba todas as conexões Bluetooth, inclusive a do aplicativo Meshtastic, que
  reconecta sozinho.

## Comando de teste

O comando é uma mensagem de texto comum; maiúsculas e minúsculas não importam.

| Onde digitar                                      | Texto                           | Efeito                                                                                                     |
| ------------------------------------------------- | ------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge` ou `!ponte`           | Três respostas `[bridge] …` naquele aplicativo: o relatório, as conexões e o que fazer. Nada é transmitido |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge reset`, `!ponte reset` | O mesmo que `Reconnect phones`                                                                             |
| Bitchat, no `#mesh`                               | `!bridge` ou `!ponte`           | Uma resposta `[bridge] …` assinada pela ponte, no máximo uma a cada 5 segundos                             |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge off`                   | Desliga a ponte (o mesmo que `Turn bridge off`)                                                            |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge on`                    | Liga a ponte de novo, no modo que estava salvo                                                             |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge local`                 | `Bitchat only`: só o relé por Bluetooth, nada vai ao LoRa                                                  |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge lora`                  | `Bitchat + LoRa` no canal em que o comando foi enviado; o canal precisa ser secundário, com nome e chave   |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge help`                  | Lista os comandos                                                                                          |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge link off`              | Deixa de levar ao LoRa o texto que pontes MeshCore trazem ao Bitchat                                       |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge link on`               | Volta a levar esse texto (é o padrão)                                                                      |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge economy on`            | Liga o `Battery saver` (anúncio Bluetooth lento e menos mensagens no LoRa)                                 |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge economy off`           | Desliga o `Battery saver` (é o padrão)                                                                     |
| Aplicativo Meshtastic conectado ao rádio da ponte | `!bridge guide`, `!ponte guia`  | Guia em quatro mensagens curtas, em inglês ou em português                                                 |
| Bitchat, no `#mesh`                               | `!bridge guide`, `!ponte guia`  | O mesmo guia, assinado pela ponte, no máximo uma vez a cada 30 segundos                                    |

Os comandos que mudam o estado (`on`, `off`, `local`, `lora`, `link`, `economy`, `reset`) só valem quando digitados
no aplicativo Meshtastic conectado ao próprio rádio. Vindos de outro rádio ou do Bitchat, não
mudam nada. `!ponte` aceita as mesmas palavras.

No relatório, `Bluetooth` é o número de conexões Bluetooth de qualquer aplicativo, `Bitchat` é
o número de telefones que enviaram algum dado do Bitchat nesta conexão e `people` é o número
de pessoas aceitas; as demais linhas repetem `Diagnostics`.

A segunda resposta descreve o Bluetooth. Na primeira linha, contadores desde a ligação do
rádio: `con` e `dis` (conexões e desconexões), `sub` (inscrições no serviço da ponte), `wr`
(escritas recebidas pela ponte), `ntf` (notificações enviadas pela ponte), `err` (envios
recusados pela pilha Bluetooth), `in` (quadros recebidos pelo serviço do próprio telefone) e
`out` (quadros escritos nele). Depois, uma linha por conexão: `mtu` (tamanho de pacote
negociado), `itv` (intervalo de conexão, em unidades de 1,25 ms), `enc` e `bond`
(criptografia e pareamento), `sub` (telefone inscrito), `wr` (o telefone já enviou algo) e
`path` (9: a ponte usa o serviço do telefone; 10: o serviço não foi encontrado e a ponte
espera as escritas do telefone; de 1 a 8: busca em andamento).

O comando não é exportado para o
LoRa pela ponte nem importado para o Bitchat. Digitado no aplicativo Meshtastic, o texto do
comando segue pelo LoRa como qualquer mensagem daquele canal; as outras pontes não respondem.
O comando funciona com a ponte desligada ou aguardando hora. A resposta no aplicativo
Meshtastic sai com o próprio nó como remetente e pode aparecer do lado das mensagens enviadas.

A tela de estado mostra uma frase (`Ready: Bitchat + LoRa`, `Clock not set`, `Standby: other
bridge`, `LoRa channel changed`) e, na última linha, o que fazer. `Phones 1/2` significa dois
telefones inscritos, dos quais um já enviou algum dado do Bitchat; `0/1` é um telefone que se
conectou e ficou mudo, ou a inscrição restaurada de um telefone pareado; `Not delivered` conta somente mensagens perdidas.

## Ligação com o MeshCore

A partir da `1.0.0-beta.6`. Quando esta ponte e uma ponte
[MeshCore–Bitchat](https://github.com/EduChacon/Meshcore-Bitchat) estão ao alcance Bluetooth dos
mesmos telefones Bitchat, as duas redes LoRa passam a conversar pelo Bitchat, sem ajuste algum:

a) o que a ponte MeshCore traz ao Bitchat, `[MC nome] texto`, sai no canal do Meshtastic como
`MC nome: texto`;

b) no sentido contrário, a ponte MeshCore leva `[MT nome] texto` à rede dela como
`MT nome: texto`.

Cada texto atravessa **uma vez só**. O que já veio da outra rede chega marcado (`MC …` ou
`MT …` antes do nome) e nenhuma ponte o leva de volta; ouvido no LoRa, ele chega aos telefones
com esse nome marcado, por exemplo `[MT MC Maria] oi`. É preciso haver ao menos um telefone com
o Bitchat aberto entre as duas pontes, porque elas não se conectam uma à outra.

`!bridge link off` desliga a passagem neste rádio e `!bridge link on` religa. No T1000-E a
escolha vale até o próximo reinício.

Limites: com mais de um lugar ligando as mesmas duas redes, o mesmo texto pode aparecer
repetido; um nome que comece por `MT ` ou `MC ` é tratado como marcação e não atravessa; e a
ligação **não foi ensaiada em aparelho**, só em simulação.

## Rádio sem tela

Os padrões automáticos não dependem de tela. O pareamento do aplicativo Meshtastic continua
exigindo o PIN ou um vínculo existente; esta versão não remove autenticação nem troca o PIN.
A [documentação oficial de Bluetooth](https://meshtastic.org/docs/configuration/radio/bluetooth/)
explica os modos de PIN. Em ESP32, Wi-Fi habilitado impede o Bluetooth nesta base.

A partir da `1.0.0-beta.5`:

a) os modos da ponte podem ser trocados pelos comandos `!bridge on`, `off`, `local` e `lora`,
descritos acima, sem tela;

b) o serviço do Meshtastic passou a constar da resposta de varredura do anúncio da ponte, para
que o aplicativo Meshtastic encontre o rádio sem a janela `Find Meshtastic app`, que depende
do menu. O nome anunciado ali é o do próprio rádio quando cabe em 11 caracteres; se não couber,
`Mesh_` e os quatro dígitos finais (por exemplo, `Mesh_1a2b`). Não foi verificado em aparelho
se todo telefone encontra um serviço anunciado só ali.

O uso diário não exige o aplicativo Meshtastic: o canal `Bitchat` é criado pela ponte e a hora
pode vir do primeiro telefone Bitchat. A exceção é a **região do LoRa**: um rádio que nunca foi
configurado não transmite até que a região seja definida, uma vez, por um cliente Meshtastic ou
por um [pacote do país](#pacotes-por-país). A instalação por `Update` preserva a região já
definida. Nenhum aparelho sem tela foi ensaiado.

## Histórico para quem chega depois

A partir da `1.0.0-beta.10`. O Bitchat guarda as mensagens públicas recentes e entrega a quem
chega as que faltam; é o que o aplicativo mostra como "ouvido aqui antes". A ponte participa
disso com o texto que traz do LoRa.

a) O texto do canal é assinado e guardado mesmo sem nenhum telefone conectado. Quando um telefone
chega, o próprio Bitchat pergunta o que falta e a ponte entrega, três mensagens por vez, da mais
nova para a mais antiga; o telefone repete a pergunta a cada 15 segundos, e o restante segue.

b) Ficam guardadas as últimas 24 mensagens (8 no T1000-E), por até seis horas, só na memória do rádio. Desligar
o rádio ou a ponte apaga tudo. Nada é gravado na memória permanente por causa de um telefone
ausente.

c) Só entra no histórico o que veio do LoRa. O que os telefones escrevem entre si não é guardado
pela ponte; eles o trocam diretamente.

d) Depois de religado sem ninguém por perto, o rádio fica sem hora e não consegue assinar: o
texto que chegar até o primeiro telefone (ou o aplicativo Meshtastic) aparecer se perde.

e) O relatório `!bridge` mostra `kept N`, o número de mensagens guardadas.

O formato do pedido foi escrito a partir do código do aplicativo e conferido com valores
calculados à parte, mas **não foi ensaiado com o aplicativo real**.

## Pacotes por país

A partir da `1.0.0-beta.9`. Servem para entregar um rádio pronto a quem só vai usar o Bitchat.
Ao lado do pacote normal, a página da versão traz um arquivo por país:

| Arquivo                      | Região do Meshtastic | Faixa              |
| ---------------------------- | -------------------- | ------------------ |
| `…-Brasil.zip`               | `ANZ`                | 915 a 928 MHz      |
| `…-EUA.zip`                  | `US`                 | 902 a 928 MHz      |
| `…-Europa-e-Reino-Unido.zip` | `EU_868`             | 869,4 a 869,65 MHz |

a) O programa é o mesmo do pacote normal, com uma diferença: na partida, se o rádio **nunca teve
região**, a ponte grava a do país, liga a transmissão e segue como se a região tivesse sido
definida pelo aplicativo. Rádio que já tem região não é alterado, qualquer que seja ela.

b) O pacote normal continua sem definir região.

c) A instalação é a mesma, por `Update` no flasher oficial, sobre um rádio que já tem o
Meshtastic. Com o pacote do país, um rádio saído da caixa passa a funcionar como ponte sem
abrir o aplicativo Meshtastic: a ponte cria o canal e toma a hora do primeiro telefone Bitchat.

d) Para o Brasil foi usada a região `ANZ`, que a comunidade [Meshbrasil](https://www.meshbrasil.com/meshtastic-frequencia-brasil.html)
descreve como o padrão adotado no país; o Meshtastic também tem a região `BR_902` (902 a
907,5 MHz), que não se comunica com quem usa `ANZ`. As faixas são as da
[tabela oficial](https://meshtastic.org/docs/configuration/radio/lora/).

e) Transmitir em faixa não permitida no lugar de uso pode ser proibido. O cumprimento da regra
local é de quem usa o rádio.

f) Para o T1000-E há um arquivo `EXPERIMENTAL-…-<país>.uf2` por país, com o mesmo comportamento.

**Só compilado; a definição automática da região não foi ensaiada em aparelho.**

## Testes e limites

`python3 tools/bridge/test_defaults.py` e `python3 tools/bridge/test_flow.py` executam os
métodos de produção de inicialização, criação do canal, persistência, hora e conversa com
canais, armazenamento, relógio, telefones e rádio simulados. São testes de host, não ensaio de
flash, falta de energia, Bluetooth ou rádio real. O que mudou desde a `experimental.5`, com os
motivos e os custos assumidos, está em [CHANGES.md](CHANGES.md).

## English

On first boot, including an upgrade, the firmware enables the Bluetooth relay and text exchange
with a LoRa channel, once. Later OFF or Bluetooth-only choices survive a reboot, and a
previously authorized text channel is kept. Otherwise it creates the secondary **Bitchat**
channel in an empty disabled slot with the public Base64 key shown above. That key is public
on purpose: channel text is published in local Bitchat `#mesh`, and **every public `#mesh`
message within reach of the bridge is published on that channel**. Existing channels, the
primary, RF settings and keys are never overwritten.

Open Bitchat near the radio. A radio without a clock takes the time from the first Bitchat
phone that connects; the Meshtastic app, GPS and RTC also supply it and take precedence. Add
only the same named channel and key to the remote stock radio, wait for the initial 30 seconds
and chat: channel text appears in `#mesh` as `[MT node name] text`, `#mesh` text appears on
the channel as `nick: text`. Text above 200 bytes is split in up to three parts. Under
`System → Bitchat bridge → Settings`, `LoRa: only !lora` restricts export to text typed with
that prefix. DMs, private groups, location and media are not translated. Host-tested and
compiled only; see [CHANGES.md](CHANGES.md#english) for what changed and the accepted costs.
