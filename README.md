# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

Ponte embarcada derivada do firmware oficial Meshtastic. O objetivo é usar
os aplicativos originais Bitchat e Meshtastic, sem computador, MQTT, Nostr ou internet na
operação. Computador continua necessário para compilar, instalar e diagnosticar.

**Versão atual (beta): [v1.0.0-beta.10](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta.10).** Dispensa o `!lora`, corrige perdas
de mensagem em conversa de grupo, recupera a hora sem o aplicativo Meshtastic, simplifica o
menu e acrescenta oito aparelhos ESP32-S3. [O que mudou e por quê](docs/bridge/CHANGES.md).

**Estado: beta, com um aparelho relatado em funcionamento.** Em um Heltec V3 com Bitchat
iOS 1.8.0, as versões até a `1.0.0-beta.3` não trocavam mensagens com o aplicativo: o telefone
se conectava e se inscrevia, mas não enviava nada ao rádio. Com a `1.0.0-beta.4`, o operador
relatou o telefone reconhecido (`Phones 1/1`, `People 2`) e texto nos dois sentidos
(`LoRa out 2 in 1`). O relatório `!bridge` dessa sessão mostrou o caminho novo em uso
(`path 9`, 230 quadros recebidos e 59 enviados pelo serviço do telefone, nenhuma escrita do
telefone no serviço da ponte), com o aplicativo Meshtastic na mesma conexão. É uma sessão em
um aparelho, sem ensaio de duração. Os demais nove alvos só foram compilados.
A `1.0.0-beta.5` acrescentou os comandos para rádios sem tela e mudou o anúncio Bluetooth; a
`1.0.0-beta.6` acrescentou a [ligação com pontes MeshCore](docs/bridge/DEFAULTS.md#ligação-com-o-meshcore);
a `1.0.0-beta.7` acrescentou o comando de texto do `Battery saver` (`!bridge economy on` e `off`);
a `1.0.0-beta.8` acrescentou o guia dentro da ponte (`!bridge guide`, `!ponte guia`); a
`1.0.0-beta.9` acrescentou os [pacotes por país](docs/bridge/DEFAULTS.md#pacotes-por-país); a
`1.0.0-beta.10` acrescenta o [histórico para quem chega depois](docs/bridge/DEFAULTS.md#histórico-para-quem-chega-depois).
Essas mudanças foram simuladas e compiladas, **sem ensaio em aparelho**.
Fazer backup antes de instalar e ler os [limites](docs/bridge/CHANGES.md) e o [roteiro de ensaio](docs/bridge/TESTING.md).

## Como funciona

| Modo                     | Comportamento                                                                                           | Rádios modificados                                   |
| ------------------------ | ------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| A — Bitchat only         | Relé de pacotes Bitchat por Bluetooth, com limites, assinatura, TTL e deduplicação                      | Um por área Bluetooth; não usa LoRa                  |
| B — Bitchat + LoRa       | Texto público do `#mesh` ↔ texto nativo em um canal Meshtastic dedicado                                 | Um; os demais rádios continuam com firmware original |
| C — túnel de laboratório | Pacotes Bitchat preservados em porta privada 256, entre duas pontes; ativação e consentimento separados | Dois                                                 |

No primeiro boot, a ponte ativa A e B e cria o canal público `Bitchat` em uma posição vazia,
sem tocar no canal primário nem nos canais existentes. A chave desse canal é pública e está
no [guia](docs/bridge/DEFAULTS.md): **o texto desse canal aparece no `#mesh` do Bitchat, e todo
texto público do `#mesh` ao alcance da ponte é publicado nesse canal LoRa.**

B não traduz DMs, sessões Noise, grupos privados, posição, telemetria, arquivos, voz nem
comandos administrativos.

## Aparelhos

| Aparelho                         | Alvo no flasher           | Flash |
| -------------------------------- | ------------------------- | ----- |
| Heltec V3                        | `heltec-v3`               | 8 MB  |
| Heltec V4 (OLED)                 | `heltec-v4`               | 16 MB |
| Heltec Wireless Stick Lite V3    | `heltec-wsl-v3`           | 8 MB  |
| Heltec Wireless Tracker V1.1     | `heltec-wireless-tracker` | 8 MB  |
| Heltec Wireless Paper            | `heltec-wireless-paper`   | 8 MB  |
| LILYGO T-Beam Supreme            | `tbeam-s3-core`           | 8 MB  |
| Seeed XIAO ESP32-S3 + Wio-SX1262 | `seeed-xiao-s3`           | 8 MB  |
| LILYGO T-Deck                    | `t-deck`                  | 16 MB |
| Station G2                       | `station-g2`              | 16 MB |
| Cardputer ADV + Cap LoRa-1262    | `m5stack-cardputer-adv`   | 8 MB  |

O firmware é compilado **por modelo de placa**. Processador igual não garante pinagem, rádio,
flash, partições ou bootloader iguais; instalar o arquivo de outro modelo pode impedir o boot.
RAK4631, T-Echo, T-Beam clássico, T-LoRa32 e T3-S3 **não têm binário da ponte**, mas servem
como nós com firmware original no modo B. [Matriz e condições](docs/bridge/COMPATIBILITY.md).

O **SenseCAP T1000-E** tem um arquivo **experimental** à parte, anexado à versão: compila, mas
nunca foi ligado em aparelho e está fora das verificações da versão.
[Estado, instalação e roteiro](docs/bridge/T1000E.md).

## Instalar

1. Em [v1.0.0-beta.10](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta.10), baixar
   **`meshtastic-bitchat-1.0.0-beta.10.zip`**. É um arquivo só, para os dez aparelhos. Para um rádio
   que nunca teve região definida, há também um arquivo por país (`…-Brasil.zip`, `…-EUA.zip`,
   `…-Europa-e-Reino-Unido.zip`), que dispensa o aplicativo Meshtastic.
2. No [Web Flasher oficial](https://flasher.meshtastic.org/), escolher a placa, carregar o ZIP
   e usar **Atualizar / Update**. Não selecionar **Apagar Flash e Instalar**. Se a placa
   oferecer a interface colorida (MUI), deixar desmarcada.

O flasher grava somente a aplicação da placa escolhida; escolher a placa errada pode impedir
o boot. Nada mais é necessário: a ponte sai configurada e os ajustes são opcionais. Quem
quiser guardar uma cópia do rádio antes de gravar tem o
[roteiro de backup](docs/bridge/WINDOWS_RECOVERY.md), opcional. Para voltar ao firmware
original, usar o Meshtastic 2.7.26 oficial do mesmo modelo.

## Usar

Depois de gravar, a ponte já está ligada, com o canal público `Bitchat` criado:

1. Abrir o **Bitchat** perto do rádio e conversar no `#mesh`. Se o rádio estiver sem hora, o
   primeiro telefone Bitchat que conecta fornece a hora.
2. No rádio remoto com firmware original, adicionar o canal `Bitchat`: abrir
   [este link](https://meshtastic.org/e/?add=true#CisSIMXrNZ7S8cg9qWpGbzZahdf-jswEx8THkKp3ToygVmRbGgdCaXRjaGF0) no telefone com o aplicativo Meshtastic e confirmar a **adição** do
   canal, ou digitar nome e chave do [guia](docs/bridge/DEFAULTS.md). É o único passo fora
   da ponte, porque esse rádio não tem o firmware da ponte.
3. O texto do `#mesh` chega ao canal como `apelido: texto`; o texto do canal chega ao `#mesh`
   como `[MT nome do nó] texto`.

**O canal padrão é comum a todas as pontes.** A chave é pública: toda ponte ao alcance do LoRa
compartilha a mesma sala. Para um grupo fechado, criar um canal secundário com chave própria
no aplicativo Meshtastic e escolhê-lo em `System → Bitchat bridge → Bitchat + LoRa`.

**Testar a ponte.** No aplicativo Meshtastic conectado ao rádio da ponte, enviar `!bridge` (ou
`!ponte`) no canal `Bitchat`: a ponte responde ali mesmo, só para esse aplicativo, com o
estado, as medições de cada conexão Bluetooth, quantos telefones Bitchat estão de fato falando
com ela e o que fazer. No Bitchat,
enviar `!bridge` no `#mesh`: a resposta `[bridge] …` prova que o Bluetooth funciona nos dois
sentidos. `!bridge reset`, só pelo aplicativo Meshtastic, derruba as conexões Bluetooth e
manda os telefones lerem de novo os serviços do rádio. O texto do comando segue pelo LoRa como
qualquer mensagem do canal, e as demais pontes o ignoram.

Ajustes opcionais, no rádio com tela: `Bitchat only`, `Bitchat + LoRa`, `Turn bridge off`,
`Status` e `Settings` (`LoRa: all messages` ou `LoRa: only !lora`, `Battery saver`,
`Find Meshtastic app`, `Reconnect phones`). A tela de estado mostra em frases o que acontece e o que fazer.

**Rádio sem tela.** Os mesmos modos, por texto, no aplicativo Meshtastic conectado ao rádio:
`!bridge off`, `!bridge on`, `!bridge local` (só Bluetooth) e `!bridge lora` (Bitchat + LoRa no
canal em que o comando é enviado). `!bridge economy on` e `!bridge economy off` ligam e desligam
o `Battery saver`. `!bridge help` lista os comandos. `!ponte guia` (ou `!bridge guide`, em inglês)
explica a ponte em quatro mensagens curtas e vale também no `#mesh` do Bitchat. Vindos de outro rádio ou
do Bitchat, esses comandos não mudam nada. O uso diário não exige o aplicativo Meshtastic; a
exceção é a região do LoRa, que um rádio nunca configurado precisa receber uma vez, pelo
aplicativo ou por um [pacote do país](docs/bridge/DEFAULTS.md#pacotes-por-país).

## Construção e documentação

Base: Meshtastic **2.7.26.54e0d8d**, commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
Bitchat de referência: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

```sh
git fetch origin tag v1.0.0-beta.10
git checkout --detach v1.0.0-beta.10
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

Os comandos constroem software; não fazem upload.

- [Índice da documentação](docs/bridge/README.md)
- [Mudanças, limites e evidência desta versão](docs/bridge/CHANGES.md)
- [Padrão de instalação e chave pública do canal](docs/bridge/DEFAULTS.md)
- [Compatibilidade e cobertura funcional](docs/bridge/COMPATIBILITY.md)
- [Arquitetura e decisões](docs/bridge/ARCHITECTURE.md)
- [Instalação, verificador e recuperação](docs/bridge/INSTALL.md)
- [Ameaças, privacidade e limitações](docs/bridge/SECURITY.md)
- [Testes, bancada e critérios](docs/bridge/TESTING.md)
- [Fontes, proveniência e licenças](docs/bridge/SOURCES.md)
- [Comparação com outras pontes](docs/bridge/COMPARISON.md)
- [Registro do trabalho](docs/bridge/WORKLOG.md) e [investigação de boot da `.2`](docs/bridge/BOOT_INCIDENT.md)
- [README original do firmware](docs/bridge/history/README.upstream.md)

Projeto independente, sem vínculo com a Meshtastic LLC nem com o projeto Bitchat, e sem
endosso de nenhum dos dois. Meshtastic® é marca registrada da Meshtastic LLC. Não é um projeto
MeshCore. Licença do firmware: [GPL-3.0](LICENSE); as dependências mantêm suas próprias
licenças.
