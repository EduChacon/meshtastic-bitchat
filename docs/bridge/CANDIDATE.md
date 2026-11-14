# Revisão candidata posterior à experimental .5

**Estado: não publicada e não validada em aparelho.** Este documento descreve o código da
árvore depois da `.5`. Os binários da release `.5` continuam com o comportamento descrito
em [DEFAULTS.md](DEFAULTS.md) na revisão da tag. Nenhuma entrega entre aparelhos está
classificada como confirmada em hardware; os gates de [TESTING.md](TESTING.md) continuam valendo.

## O que muda para quem usa

| Assunto                | `.5`                                               | Candidata                                                          |
| ---------------------- | -------------------------------------------------- | ------------------------------------------------------------------ |
| Bitchat → LoRa         | Somente texto iniciado por `!lora `                | Todo texto público do `#mesh`; `!lora ` aceito e removido do texto |
| Texto visto no rádio   | `[BC 3fa9…] texto [mb1:…:…]`                       | `apelido: texto`                                                   |
| Texto visto no Bitchat | `[MT !a1b2c3d4] texto`                             | `[MT nome do nó] texto`                                            |
| Tamanho                | 120 bytes; acima disso, recusa silenciosa          | Até 200 bytes por pacote; texto maior dividido em até três partes  |
| Ritmo por pessoa       | Uma mensagem a cada 30 s (120 s no modo econômico) | Rajada de seis; depois, uma a cada 6 s (30 s no modo econômico)    |
| Duas pontes próximas   | As duas bloqueiam até intervenção no menu          | A de maior número de nó espera; retoma sozinha em cerca de 2 min   |
| Tela de estado         | Siglas (`BLE1 P2 D37`, `TX3 RX5 Q0`)               | Frases (`Phones 2  People 5`, `LoRa out 3  in 5`) e orientação     |
| Hora após falta de luz | Parada até o aplicativo Meshtastic conectar        | O primeiro telefone Bitchat que conecta fornece a hora             |
| Entrega no canal LoRa  | Uma transmissão, sem repetição                     | Repetição nativa do Meshtastic, como no texto digitado no app      |

O prefixo continua disponível como opção: `System → Bitchat bridge → Settings → LoRa: all
messages` alterna para `LoRa: only !lora`. A escolha é persistida.

### Motivos do prefixo e do novo padrão

O `!lora ` cumpria três funções: a) intenção explícita de quem escreve, porque o texto passa
do alcance do Bluetooth para o alcance do LoRa; b) economia de tempo de transmissão; c) barreira
contra laço, pois o texto importado do LoRa não começava por `!lora `. A função c) passou a ser
cumprida por duas regras independentes do prefixo: texto de outra ponte nunca é exportado (a
ponte se identifica pelo TLV `0x7f` do anúncio) e texto iniciado por `[MT ` nunca é exportado.
As funções a) e b) ficam atendidas pela opção do menu e pelos limites de transmissão abaixo.

Com o padrão novo, **toda pessoa que escreve no `#mesh` ao alcance da ponte tem o texto
publicado no canal LoRa escolhido**, mesmo sem saber da ponte. O `#mesh` já é público, mas o
alcance aumenta. Quem preferir o comportamento anterior deve selecionar `LoRa: only !lora`.

## Causas de perda corrigidas

Cada item abaixo foi identificado por leitura do código e comparado com o Bitchat iOS 1.8.0
(commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`). São defeitos demonstrados em teste de
host; **não há medição em aparelho que atribua a eles a instabilidade observada em campo**.

1. **Filas Bluetooth de dois frames por telefone.** A importação de um texto do LoRa gerava
   anúncio e mensagem em sequência; com qualquer frame ainda em envio, a mensagem era
   descartada. Agora são seis frames por telefone em cada sentido, com buffers do tamanho
   exato de cada frame e o mesmo teto de memória anterior (16 KiB).
2. **Uma notificação a cada 20 ms.** O aplicativo descarta um frame cujos pedaços chegam com
   mais de 250 ms de intervalo (`bleAssemblerStallResetMs`). Um atraso do laço principal
   acima disso quebrava frames de três ou mais pedaços. Agora cada ciclo envia dois pedaços
   por telefone, metade dos 12 buffers que o NimBLE divide com o aplicativo Meshtastic.
3. **Cópias verificadas uma a uma.** Em grupo, cada telefone retransmite o que ouve. A ponte
   conferia a assinatura Ed25519 de cada cópia antes de reconhecê-la como repetida, e a fila
   de entrada enchia. Agora a cópia é reconhecida antes da assinatura.
4. **Uma mensagem por pessoa a cada 30 s.** A segunda mensagem era contada como duplicata e
   sumia. Substituído por rajada com reposição.
5. **Journal de 64 entradas por 10 min.** Cheio, recusava mensagens novas nos dois sentidos.
   Agora a entrada mais antiga cede lugar.
6. **Tabela de 24 identidades sem saída.** Depois de 24 pessoas vistas, nenhuma outra era
   aceita até reiniciar. Agora cede a vaga de quem está em silêncio há 5 min.
7. **Prioridade `BACKGROUND` no LoRa.** O texto da ponte era o primeiro a ser descartado
   por uma fila de transmissão cheia. Texto usa agora a prioridade padrão.
8. **Conflito permanente entre pontes.** Descrito na tabela acima.
9. **Frame parcial preso.** Um pedaço incompleto fazia o frame seguinte ser colado a ele e os
   dois se perdiam. Um frame inteiro em uma escrita agora substitui o parcial.
10. **Texto sem repetição no LoRa.** A ponte enviava com `want_ack` desligado. O texto digitado
    no aplicativo Meshtastic sai com `want_ack`, e o roteador repete até três vezes quando não
    ouve outro nó retransmitir. O texto da ponte passa a usar o mesmo mecanismo no perfil fixo;
    com `Battery saver`, continua em transmissão única.

Eficiência: as chaves de canal eram derivadas a cada 20 ms e agora só quando o canal muda; a
ponte deixa de assinar anúncios e de registrar importações quando nenhum telefone está inscrito.

## Limites desta revisão

| Recurso                  | Valor                                                                       |
| ------------------------ | --------------------------------------------------------------------------- |
| Texto por pacote LoRa    | 200 bytes, incluído `apelido: `; até três partes `apelido (1/3): …`         |
| Fila LoRa                | 8 pacotes, prazo de 120 s                                                   |
| Tempo de transmissão     | Reposição de 10% no perfil fixo e 1% com `Battery saver`; rajada de 12 s    |
| Filas Bluetooth          | 6 frames por telefone em cada sentido; 16 posições; 16 KiB no total         |
| Memória inicial modelada | 24.496 bytes no modelo de host (antes 23.392); reserva de 24 KiB inalterada |
| Espera diante de outra   | 90 s sem prova da outra ponte e 30 s de observação                          |
| Relógio                  | Hora confiável do Meshtastic; na falta dela, hora do telefone (ver abaixo)  |

O marcador `mb1` deixou de ser emitido e lido. Ele reconhecia o mesmo texto exportado por duas
pontes sobrepostas. A sobreposição passa a ser tratada pela espera automática; se a detecção
falhar (pontes que não se ouvem por Bluetooth), pode haver texto duplicado, mas não laço.
Uma ponte com revisão anterior na mesma sala continua bloqueando a si mesma ao ver a prova
desta; a mistura de revisões não foi ensaiada.

## Aparelhos

Além do Heltec V3 e do Cardputer ADV, a candidata tem alvos de compilação para Heltec V4,
Heltec Wireless Stick Lite V3, Heltec Wireless Tracker, Heltec Wireless Paper, LILYGO T-Beam
Supreme, Seeed XIAO ESP32-S3, LILYGO T-Deck e Station G2. Todos são ESP32-S3 com 8 ou 16 MB e
usam a interface padrão; as variantes com interface `MUI` não têm o menu da ponte. O
empacotador, o verificador e o backup passaram a aceitar o leiaute de 16 MB, com a primeira
partição de aplicação de `0x640000` em `0x10000`. A [matriz](COMPATIBILITY.md) lista as
condições de cada um e os aparelhos que ficaram de fora. Nenhum alvo novo foi ensaiado.

## Menu

```text
System → Bitchat bridge
  Bitchat only        relé Bluetooth, sem LoRa
  Bitchat + LoRa      escolha do canal e confirmação de publicação
  Turn bridge off
  Status              estado, orientação, telefones conectados, canal
  Settings
    LoRa: all messages / only !lora
    Battery saver: off / on
    Find Meshtastic app (5 min) / Back to Bitchat now
    Help
    Advanced: tunnel  laboratório, com confirmação própria
```

A tela de estado mostra, na última linha, o que fazer quando há algo a fazer (`Open
Bitchat nearby`, `Choose it again in menu`, `Restart the radio`). `Not delivered` conta
somente mensagens perdidas; cópias e recusas de política deixaram de entrar nessa conta.

## Evidência

Executado nesta revisão, sem rádio: 565 verificações de protocolo com os 19 vetores oficiais e
100.000 entradas malformadas; adaptador Bluetooth; modelo de memória; menu; padrões;
inicialização; pacote; e o teste novo `tools/bridge/test_flow.py`, com 709 verificações. Esse
teste compila o módulo e o adaptador de produção e simula três telefones e um rádio, com
assinaturas reais: conversa em grupo, cópias, texto com e sem prefixo, divisão, importação do
LoRa, segunda ponte, local lotado, limite de transmissão, reinício e retorno sem relógio. É
simulação: não há controlador Bluetooth, iOS, RF nem temporização real.

## Hora após falta de energia

Sem RTC, o Heltec V3 volta sem hora. Na `.5`, a ponte ficava parada até o aplicativo
Meshtastic conectar. Nesta revisão, por decisão do operador, a ponte aceita a hora de um
telefone Bitchat enquanto não houver fonte confiável (aplicativo Meshtastic, GPS ou RTC):

- a hora vem do primeiro anúncio assinado e direto (TTL 7) recebido por Bluetooth;
- nunca é aceita hora anterior à última hora confiável conhecida, gravada a cada hora, nem
  anterior à data de compilação do firmware;
- depois de definida, a hora só avança; anúncio mais antigo é ignorado;
- quando uma fonte confiável reaparece, ela prevalece;
- enquanto não há hora, o anúncio Bluetooth alterna a cada 20 s entre o serviço Bitchat e o
  serviço Meshtastic, para que qualquer um dos aplicativos encontre o rádio.

Custo assumido: essa hora não é autenticada por terceiro. Um telefone ao alcance do Bluetooth
pode adiantar o relógio e fazer a ponte recusar as mensagens dos demais até a hora confiável
voltar. Mensagens antigas continuam recusadas, porque o relógio não retrocede. A hora do
telefone nunca é gravada como piso, para que um adiantamento não sobreviva ao reinício.

## Decisões em aberto

- **Padrão de envio.** `all messages` foi adotado como padrão a pedido do operador. Em local
  público, `only !lora` preserva a intenção de quem escreve.

## English

Unreleased candidate after `.5`, host-tested only. Every public `#mesh` message is exported
to the selected LoRa channel as `nick: text`; `!lora ` is still accepted and can be made
mandatory again under `Settings`. Loop protection no longer depends on the prefix: text from
another bridge and text starting with `[MT ` are never exported. Long text is split in up to
three parts. LoRa text reaches Bitchat as `[MT node name] text`.

Fixed loss causes, found by code reading against Bitchat iOS 1.8.0 and reproduced on host:
two-frame BLE queues, one notification per 20 ms against the app's 250 ms assembler reset,
signature checks spent on relayed copies, one LoRa message per person per 30 s, a journal and
an identity table that refused new entries when full, `BACKGROUND` LoRa priority, a permanent
block when two bridges met, and a stalled partial frame swallowing the next one. No device
measurement attributes the field instability to these defects. Chat text now uses Meshtastic's
own retransmission (`want_ack`) outside battery saver. After a power loss, a radio without a
trusted time takes it from the first directly received signed Bitchat announcement, never
before the last trusted time and only forward afterwards; a nearby phone can therefore push
the clock ahead until a trusted source returns. This was an operator decision.
