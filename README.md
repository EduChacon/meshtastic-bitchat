# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

Ponte embarcada derivada do firmware oficial Meshtastic. O objetivo é usar
os aplicativos originais Bitchat e Meshtastic, sem computador, MQTT, Nostr ou internet na
operação. Computador continua necessário para compilar, instalar e diagnosticar.

**Versão atual (beta): [v1.0.0-beta](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta).** Dispensa o `!lora`, corrige perdas
de mensagem em conversa de grupo, recupera a hora sem o aplicativo Meshtastic, simplifica o
menu e acrescenta oito aparelhos ESP32-S3. [O que mudou e por quê](docs/bridge/CHANGES.md).

**Estado: beta, sem aceite em aparelho.** Os testes desta versão são simulações em
computador e compilação. Nenhum dos dez alvos foi ligado com este firmware; oito deles nunca
receberam versão alguma da ponte. Fazer backup antes de instalar e ler os
[limites](docs/bridge/CHANGES.md) e o [roteiro de ensaio](docs/bridge/TESTING.md).

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

| Aparelho                         | Arquivo começa por                            | Flash |
| -------------------------------- | --------------------------------------------- | ----- |
| Heltec V3                        | `meshtastic-bitchat-heltec-v3-`               | 8 MB  |
| Heltec V4 (OLED)                 | `meshtastic-bitchat-heltec-v4-`               | 16 MB |
| Heltec Wireless Stick Lite V3    | `meshtastic-bitchat-heltec-wsl-v3-`           | 8 MB  |
| Heltec Wireless Tracker V1.1     | `meshtastic-bitchat-heltec-wireless-tracker-` | 8 MB  |
| Heltec Wireless Paper            | `meshtastic-bitchat-heltec-wireless-paper-`   | 8 MB  |
| LILYGO T-Beam Supreme            | `meshtastic-bitchat-tbeam-s3-core-`           | 8 MB  |
| Seeed XIAO ESP32-S3 + Wio-SX1262 | `meshtastic-bitchat-seeed-xiao-s3-`           | 8 MB  |
| LILYGO T-Deck                    | `meshtastic-bitchat-t-deck-`                  | 16 MB |
| Station G2                       | `meshtastic-bitchat-station-g2-`              | 16 MB |
| Cardputer ADV + Cap LoRa-1262    | `meshtastic-bitchat-m5stack-cardputer-adv-`   | 8 MB  |

O firmware é compilado **por modelo de placa**. Processador igual não garante pinagem, rádio,
flash, partições ou bootloader iguais; instalar o arquivo de outro modelo pode impedir o boot.
T1000-E, RAK4631, T-Echo, T-Beam clássico, T-LoRa32 e T3-S3 **não têm binário da ponte**, mas
servem como nós com firmware original no modo B. [Matriz e condições](docs/bridge/COMPATIBILITY.md).

## Instalar

1. Em [v1.0.0-beta](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta), baixar o ZIP do modelo exato, `prepare_flash.py`,
   `verify_artifact.py` e `SHA256SUMS`.
2. Seguir o [roteiro Windows de backup privado](docs/bridge/WINDOWS_RECOVERY.md) e conferir o hash.
3. No [Web Flasher oficial](https://flasher.meshtastic.org/), selecionar a placa e o ZIP e
   usar **Atualizar / Update**. Não selecionar **Apagar Flash e Instalar**.

O `-update.bin` do mesmo modelo é a alternativa ao ZIP; **Source code** não é firmware.
Para voltar ao firmware original, usar o Meshtastic 2.7.26 oficial do mesmo modelo.

## Usar

1. Abrir o **Bitchat** perto do rádio. Se o rádio estiver sem hora (por exemplo, após falta
   de energia), o primeiro telefone Bitchat que conecta fornece a hora; o aplicativo
   Meshtastic e o GPS também servem e têm precedência.
2. No rádio remoto com firmware original, adicionar somente o canal `Bitchat`, com a chave
   pública do [guia](docs/bridge/DEFAULTS.md), e manter região, preset e frequência compatíveis.
3. Conversar normalmente: o texto do `#mesh` chega ao canal como `apelido: texto`; o texto
   do canal chega ao `#mesh` como `[MT nome do nó] texto`.

No rádio com tela, `System → Bitchat bridge` oferece `Bitchat only`, `Bitchat + LoRa`,
`Turn bridge off`, `Status` e `Settings`. Em `Settings`, `LoRa: all messages` pode ser trocado
por `LoRa: only !lora`, para que só o texto iniciado por `!lora ` saia pelo LoRa. A tela de
estado mostra em frases o que está acontecendo e o que fazer. Sem tela, valem os padrões.

## Construção e documentação

Base: Meshtastic **2.7.26.54e0d8d**, commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
Bitchat de referência: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

```sh
git fetch origin tag v1.0.0-beta
git checkout --detach v1.0.0-beta
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

Não é um projeto MeshCore. Licença do firmware: [GPL-3.0](LICENSE); as dependências mantêm
suas próprias licenças.
