# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

Ponte embarcada experimental, derivada do firmware oficial Meshtastic. O objetivo é usar
os aplicativos originais Bitchat e Meshtastic, sem computador, MQTT, Nostr ou internet na
operação. Computador continua necessário para compilar, instalar e diagnosticar.

**Pré-release de correção disponível: [v0.1.0-experimental.3](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.3).**
Ela corrige a inicialização e passou pelos testes de software; boot físico continua sem validação.
As pré-releases anteriores foram retiradas dos downloads após a falha de boot reportada no Heltec V3.
O Cardputer também continua sem teste físico.

**A branch `main` ainda contém o firmware anterior.** Para testar ou compilar as correções,
use os arquivos e o fonte da tag `v0.1.0-experimental.3`; o merge permanece pendente.
Siga o [roteiro Windows com backup privado](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/WINDOWS_RECOVERY.md)
e consulte o [registro da investigação](docs/bridge/BOOT_INCIDENT.md).

**Estado: correção validada em software; ensaios físicos da `.3` ainda pendentes.**
Uma compilação bem-sucedida não comprova descoberta BLE, entrega, consumo, boot, atualização
ou recuperação. Esta versão não é um produto validado para uso de campo.
Consulte os resultados efetivamente executados em [TESTING](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/TESTING.md).

## Modos independentes

| Modo                     | Comportamento implementado                                                                                       | Rádios modificados                                                  |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| A — BLE local            | Relé de pacotes Bitchat, com limites, assinatura, TTL e deduplicação                                             | Um por área BLE; sem uso de LoRa pela função A                      |
| B — texto compartilhado  | `!lora texto` → texto nativo em canal Meshtastic dedicado; canal → Bitchat público                               | Um; os demais rádios continuam stock                                |
| C — túnel de laboratório | Pacotes preservados em porta privada 256; descoberta e `!bctun texto`; opção separada para tráfego privado opaco | Dois terminadores; intermediários stock precisam de ensaio separado |

**Tudo vem desligado por padrão.** B não traduz DMs, sessões Noise, posição, telemetria,
arquivos, voz ou comandos administrativos. O texto do canal com PSK será público no Bitchat
local após consentimento explícito. A PSK não protege o texto depois dessa exportação.

## Alvos desta entrega

- `heltec-v3-bitchat`: Heltec WiFi LoRa 32 V3, ESP32-S3/SX1262, flash de 8 MB.
- `m5stack-cardputer-adv-bitchat`: Cardputer **ADV**, com **Cap LoRa-1262** e revisão
  correspondente ao alvo upstream, flash de 8 MB. Cardputer original e outros acessórios não são equivalentes.

T1000-E, RAK4631, T-Deck, T-Echo, outras Heltec/LilyGO e demais dispositivos Meshtastic
continuam candidatos a **nós stock** do modo B, com rádio, banda, região, preset,
frequência e canal compatíveis. Isso não significa que este binário possa ser instalado neles.
O porte da própria ponte para T1000-E/nRF52 e T-Deck não está incluído nesta versão.
[Matriz completa](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/COMPATIBILITY.md).

O núcleo de protocolo é compartilhado, mas o firmware é compilado **por modelo de placa**.
Processador igual não garante pinagem, rádio, flash, partições ou bootloader iguais. Não há
binário universal ESP32 ou nRF52; instalar um arquivo de outro modelo pode impedir o boot.

## Download

Versão disponível: [**v0.1.0-experimental.3**](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.3), para teste de recuperação após
backup e preflight. Os binários não mudaram com a organização dos downloads.

| Placa                         | Arquivo para o flasher oficial                                                                                                                                                                |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Heltec V3**                 | [**Baixar ZIP Heltec V3**](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/meshtastic-bitchat-heltec-v3-0.1.0-experimental.3-d15a403.zip)             |
| Cardputer ADV + Cap LoRa-1262 | [Baixar ZIP Cardputer ADV](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.3-d15a403.zip) |

1. Seguir o [roteiro Windows com backup privado](https://github.com/EduChacon/meshtastic-bitchat/blob/main/docs/bridge/WINDOWS_RECOVERY.md).
2. Conferir o download com [SHA256SUMS](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/SHA256SUMS).
3. No [Web Flasher oficial](https://flasher.meshtastic.org/), selecionar a placa e o ZIP,
   depois **Atualizar / Update**. Não selecionar **Apagar Flash e Instalar**.

Os dois scripts de backup/preflight e o guia continuam anexados à release. O `-update.bin`
por placa é uma alternativa ao ZIP; **Source code** não é firmware para o instalador.
Não é necessário fazer merge para instalar os arquivos da release.

Para auditoria ou compilação, o [pacote técnico](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/technical-package-0.1.0-experimental.3.tar.gz) reúne fontes correspondentes,
dependências, licenças, símbolos ELF/mapa, manifestos, resultados de testes e os anexos
originais da `.3`, com seus hashes. Não é um arquivo para o flasher. O histórico de código,
as tags e o [registro da investigação](docs/bridge/BOOT_INCIDENT.md) foram preservados.

## Começo seguro

1. Confirme modelo/revisão, banda/antena, aplicativos e sistema operacional.
2. Faça a bancada stock nos dois sentidos e guarde um backup privado e a recuperação stock.
3. Leia [instalação e rollback](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/INSTALL.md). Os pacotes contêm apenas atualização
   de aplicativo; não incluem apagador, bootloader ou tabela de partições.
4. Após instalação e verificação do primeiro boot, use `Menu → System → Bitchat bridge`.
   Configure antes um canal secundário de teste com PSK própria de 32 bytes no app Meshtastic.
5. Ative A primeiro. Para B, selecione explicitamente o canal e leia o aviso de publicação.
   Aguarde os 30 segundos de observação de outras pontes. Envie, por exemplo, `!lora Olá`.
6. `Meshtastic BLE: 5 min` anuncia temporariamente o serviço nativo para administração e relógio.
   Depois retorna ao anúncio Bitchat. A coexistência simultânea precisa de teste no iOS exato.

O painel mostra BLE (conexões), P (peers), D (descartes), TX/RX e Q (fila LoRa).
Contadores TX/RX grandes usam k/M/G com arredondamento para baixo; não são confirmações de
entrega no telefone. O espaçamento acompanha a fonte da placa. Navegação física e legibilidade
no aparelho ainda precisam de bancada.

Após perda de alimentação, o Heltec sem RTC confiável pode precisar de nova conexão ao
app Meshtastic para obter a hora. O estado local mostra `Open Meshtastic: clock`. Abrir somente
o Bitchat ainda não resolve essa condição. Não há fallback de internet.

## Construção e documentação

Base: Meshtastic **2.7.26.54e0d8d**, commit
`54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`. É a versão Beta apresentada como estável na
[página oficial de downloads](https://meshtastic.org/downloads/) consultada em 2026-11-13.
Esta entrega não usa a linha Alpha 2.8.2.
Bitchat de referência: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

Para compilar a correção, faça checkout da tag em uma cópia de trabalho limpa:

```sh
git fetch origin tag v0.1.0-experimental.3
git checkout --detach v0.1.0-experimental.3
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

Os comandos constroem software; não fazem upload. Use somente fontes e ferramentas oficiais
fixadas. As dependências e a ferramenta precisam de download na preparação.

- [Arquitetura, limites e hipóteses refutáveis](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/ARCHITECTURE.md)
- [Compatibilidade e cobertura funcional](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/COMPATIBILITY.md)
- [Instalação, verificador e recuperação](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/INSTALL.md)
- [Ameaças, privacidade e limitações](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/SECURITY.md)
- [Testes, bancada, consumo e critérios](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/TESTING.md)
- [Fontes, proveniência e licenças](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/SOURCES.md)
- [Registro do trabalho](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/WORKLOG.md)
- [README original do firmware](docs/bridge/README.upstream.md)

O histórico oficial e o histórico inicial do projeto foram preservados. Não é um projeto
MeshCore. Licença do firmware: [GPL-3.0](LICENSE); as dependências mantêm suas próprias licenças.
