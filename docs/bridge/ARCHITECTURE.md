# Arquitetura e decisões

## Caminhos de dados

```mermaid
flowchart LR
  IP1[Bitchat oficial no iPhone] <-->|BLE| BLE[Adaptador BLE periférico]
  BLE --> CODEC[Codec + assinatura + limites]
  CODEC --> A[A: relé BLE]
  A --> BLE
  CODEC -->|texto público| B[B: tradução + journal]
  B <-->|TEXT_MESSAGE_APP 1| M[Router Meshtastic stock]
  CODEC -->|opt-in separado| C[C: envelope opaco + fragmentos]
  C <-->|PRIVATE_APP 256| M
  M <-->|LoRa| STOCK[Rádio stock + app Meshtastic]
  M <-->|LoRa| BR2[Segunda ponte C]
  BR2 <-->|BLE| IP2[Segundo grupo Bitchat]
```

Um fork basta para B. O T1000-E permanece stock nessa primeira bancada. Dois terminadores
modificados são necessários para C. Intermediário stock não termina o túnel. Nenhum caminho
usa Wi-Fi, MQTT, Nostr, servidor ou telefone dedicado. As funções stock herdadas não são
dependências da ponte. A função A não transmite LoRa; o rádio ainda pode transmitir tráfego
stock se o operador não o desativar no controle negativo.

## Mapa de código

| Arquivo                          | Responsabilidade                                                                |
| -------------------------------- | ------------------------------------------------------------------------------- |
| `BridgeCodec.*`                  | Framing v1/v2, tamanho, UTF-8, assinatura canônica, TTL e TLVs                  |
| `BridgeCrypto.h`                 | SHA-256, HMAC e Ed25519 da biblioteca upstream; validação estrita de entradas   |
| `BridgeState.*`                  | Filas fixas, limites por origem, TOFU volátil, dedup e remontagem BLE           |
| `BridgeBle.*`                    | GATT, anúncios, callbacks curtos, filas por conexão e notificações conforme MTU |
| `BridgeText.*`                   | Atribuição por apelido, divisão de texto longo e prova de outra ponte           |
| `BridgeTunnel.*`                 | Envelope versionado, MAC, fragmentação e remontagem limitada                    |
| `BitchatBridgeModule.*`          | MeshModule/Router, perfis, NVS, relógio e contadores                            |
| `BridgeMenu.cpp`                 | Controles locais e avisos antes da exportação                                   |
| `BridgeDefaults.h`               | Nome e chave pública do canal dedicado de interoperabilidade                    |
| `variants/bridge/platformio.ini` | Dois alvos separados; ambientes stock não habilitam a função                    |

`TextMessageModule` continua recebendo texto normal. Não há campos protobuf customizados
que os apps originais precisariam reconhecer. B usa a porta nativa 1; C usa a porta privada
256 de laboratório. Coordenar essa porta antes de uma distribuição com outras aplicações.

## BLE e iOS

Serviço release: `F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C`.
Característica: `A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D`, READ/WRITE/WRITE_NR/NOTIFY.
Não é o UUID testnet. O rádio implementa o papel **periférico**, sem busca como central.
Isso limita background e ligação entre repetidores sem telefone intermediário.

Sobre a conexão aberta pelo telefone há dois caminhos de dados. No primeiro, o telefone
escreve na característica da ponte e a ponte responde por notificação. No segundo, a ponte
age como cliente GATT na mesma conexão: procura o serviço que o próprio aplicativo publica,
inscreve-se nele, recebe os pacotes do telefone como notificações e envia os seus como
escritas de um quadro inteiro. O segundo caminho existe porque o aplicativo só escreve quando
o iOS informa que a conexão aceita escrita sem resposta, e em aparelho real essa escrita não
chegou. Quando o serviço do telefone é encontrado, a ponte usa só o segundo caminho e não
anuncia a si mesma por notificação: um anúncio direto recebido pela característica da ponte
ligaria a identidade dela à conexão de escrita do telefone, e o aplicativo passaria a preferir
essa conexão. Sem o serviço, a ponte só fala por notificação com o telefone cujas escritas já
chegaram, e repete a busca a cada 10 s enquanto ele estiver mudo. Ao conectar, a ponte pede a
troca de MTU: no MTU padrão de 23 bytes nada do que o aplicativo envia cabe em uma escrita.
Em um Heltec V3 com Bitchat iOS 1.8.0, uma sessão medida usou só o segundo caminho (`path 9`),
com o aplicativo Meshtastic na mesma conexão: nesse aparelho o iOS expôs o serviço do
aplicativo ao rádio, e o telefone continuou sem escrever no serviço da ponte. Outros modelos
de telefone, Android e sessões longas não foram ensaiados.

O anúncio principal contém flags e UUID Bitchat; o nome fica em SCAN_RSP. A administração
anuncia o UUID Meshtastic por cinco minutos. Não presume dois UUIDs de 128 bits no anúncio
legado. Serviços simultâneos não comprovam descoberta/operação simultânea dos apps.
O código separa a desconexão Bitchat da sessão nativa de outro identificador de conexão.
Sem hora, o anúncio alterna a cada 20 s entre Meshtastic e Bitchat. A hora confiável vem do
aplicativo Meshtastic, do GPS ou do RTC; na falta dela, vale a do primeiro anúncio Bitchat
assinado e direto, nunca anterior à última hora confiável gravada, e depois só para a frente.
O [padrão de instalação](DEFAULTS.md) habilita BLE/texto dedicado uma vez, preservando
escolhas posteriores e canais existentes. Não depende de display, mas exige pareamento.

Configuração: até três links, incluindo a eventual conexão nativa. Quantidade utilizável,
MTU, justiça e reconexão não foram medidos. Há seis posições de recepção e seis de envio por
conexão, sobre 16 frames globais alocados sob demanda no tamanho exato, com teto de 16 KiB e
payload de broadcast compartilhado. Cada ciclo envia até duas notificações por conexão, porque
o aplicativo descarta um frame cujos pedaços chegam com mais de 250 ms de intervalo.
O núcleo é alocado em blocos separados; remontagens só consomem memória quando usadas.
A reserva interna de 24 KiB e o bloco livre mínimo de 8 KiB continuam obrigatórios.
Uma conexão lenta expira sua própria fila após dez segundos e é desconectada
para reiniciar o enquadramento; isso não confirma entrega ao usuário.

Intervalos iniciais: 152,5 ms fixo e 1022,5 ms econômico, valores listados pela Apple.
Não são resultados de otimização energética. Janela renovável por botão e anúncios rápidos
transitórios ficam para depois da linha de base. A economia atual mantém descoberta disponível.

## Assinaturas e limites

Bitchat 1.8.0 assina a representação com TTL zero, sem assinatura e sem flag RSR, **com padding**.
O texto público transmitido normalmente não tem padding. Rotas, origem, destinatário, timestamp,
tipo e payload permanecem autenticados. O relé altera somente TTL; não reescreve rotas.
Os 19 vetores usam codec oficial/CryptoKit e incluem padding, compressão, v2 e Noise sintético.
Isso não é ensaio de sessão Noise entre aparelhos.

RSR precisa de contexto de sincronização que a ponte não mantém: é rejeitado. `REQUEST_SYNC`
é local ao link e não é propagado. Não há histórico ou correio durável nem chaves privadas
de clientes. O rádio tem identidade própria para assinar conversões, mas não implementa
caixa de entrada DM: enviar DM **à identidade da ponte** não é suportado.

| Recurso                    | Limite inicial                                                              |
| -------------------------- | --------------------------------------------------------------------------- |
| Frame BLE remontado        | 2048 bytes; payload e tamanho expandido declarado até 1792                  |
| Identidades em RAM         | 24; cheia, cede a vaga de quem está em silêncio há 5 min                    |
| Remontagem BLE             | 3 mensagens, até 32 fragmentos, 30 s sem renovação                          |
| Dedup volátil              | 256 hashes SHA-256, 125 s; cópia é reconhecida antes da assinatura          |
| Texto para o LoRa          | 200 bytes por pacote com `apelido: `; até três partes; nunca truncado       |
| Texto Meshtastic importado | Até 233 bytes, precedido de `[MT nome do nó] `                              |
| Journal persistente        | 64 IDs de 128 bits e timestamps, 10 min; cheio, substitui o mais antigo     |
| Fila LoRa                  | 8 pacotes, prazo de 120 s; descarte contado                                 |
| C embarcado                | Frame até 512 bytes; codec testável até 2048                                |
| Remontagem C               | 3 mensagens; 45 s; fragmento útil até 160 bytes                             |
| Orçamento adicional        | Reposição de 10% do tempo no fixo ou 1% no econômico, rajada máxima de 12 s |
| Ritmo por pessoa           | Rajada de seis textos; depois um a cada 6 s (30 s no econômico)             |

O orçamento usa a estimativa do RadioInterface para o pacote completo. Controles regionais,
ocupação e fila stock continuam valendo. Airtime dos intermediários deve ser medido à parte.
Há limite adicional por origem. O texto sai com prioridade padrão e `want_ack`, como o texto
digitado no aplicativo; no econômico, em transmissão única. O contador `LoRa out` do visor
conta submissões ao Router, não chegada ao destino.

## Eco, reinício e sobreposição

B exporta texto público do `#mesh` com assinatura válida contra a chave anunciada, para o
canal secundário selecionado, como `apelido: texto`; apelidos iguais recebem o início do
PeerID. Em `LoRa: only !lora`, só o texto iniciado por `!lora ` é exportado. O sentido contrário
leva `[MT nome do nó]`, sob a identidade da ponte. Nome e apelido não autenticam ninguém: é
atribuição entre redes, não verificação Noise/PKI do remetente.

Duas regras impedem laço, independentemente do prefixo: texto de um peer cujo anúncio traz o
TLV de ponte (`0x7f`) nunca é exportado, e texto iniciado por `[MT ` também não. O journal é
gravado antes da exportação; falha de gravação bloqueia a ponte. Evitar repetição pode perder
uma entrega se a energia cair entre journal e transmissão. Não há fila persistida de payload.

Só um exportador B por malha Bluetooth sobreposta fica ativo. A prova no TLV do anúncio,
assinada e autenticada pela chave de canal, identifica a outra ponte: a de maior número de nó
entra em espera (`Standby: other bridge`) e retoma sozinha 90 s depois de deixar de ver a prova,
mais 30 s de observação. **Isso não é consenso distribuído:** pontes que não se ouvem por
Bluetooth podem duplicar texto no LoRa, sem laço. A configuração sobreposta não foi ensaiada.

C leva versão, domínio de canal, origem, boot, sequência, hash do frame, índice, quantidade,
tamanho e TTL de túnel 1, com HMAC. Recebido de LoRa não é reexportado por LoRa. TTL Bitchat
é consumido pelos relés sem invalidar assinatura. B e C usam canais diferentes e prefixos
exclusivos. Perda leva a expiração/contagem; **não há ARQ próprio**. O cliente mantém seus
limites de repetição; a ponte não inventa ACK de aplicação.

## Hipóteses refutáveis

| Hipótese                        | Evidência atual                              | Experimento que a refuta                                                             |
| ------------------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------ |
| A liga dois iPhones             | Codec e caminho compilados                   | Não entregam com ponte ligada, fora do alcance direto, LoRa/internet desligados      |
| B usa um fork e um stock        | Porta 1 e caminho nativo                     | Texto não aparece em algum sentido nas condições do baseline                         |
| C passa por intermediário stock | Porta privada normal                         | Funciona direto, mas falha ao inserir stock com a mesma configuração                 |
| Dois apps coexistem             | Serviços e conexões separados                | Descoberta, autorização ou desconexão interfere no outro; usar alternância           |
| Retorno automático após energia | Preferências persistidas; hora pelo telefone | Bitchat conecta e a ponte não volta a operar sem o aplicativo Meshtastic             |
| C permite DMs                   | Bytes Noise podem ser opacos                 | Handshake expira, rota falha ou cliente exige estado ausente; não enfraquecer sessão |
| Economia poupa bateria          | Intervalos/orçamento menores                 | Energia por mensagem não melhora ou disponibilidade cai abaixo da necessidade        |

Em LongFast (SF11/BW250/CR4/5, preâmbulo 16), estimativas brutas: 0,72 s/64 bytes,
1,13 s/120, 1,75 s/200, 2,03 s/240. São cálculos, não medidas. Um KiB fragmentado já
exige vários pacotes e dezenas de segundos com contenção/hops. Voz ao vivo de cerca de
16 kbit/s excede a taxa bruta aproximada de 1,07 kbit/s desse preset. Voz contínua, arquivos
e histórico não são funções LoRa aceitas. Noise opaco pode conter mídia, indistinguível sem
descriptografar: a opção privada C é somente opt-in de laboratório com limites rígidos.
