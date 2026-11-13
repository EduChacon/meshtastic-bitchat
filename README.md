# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

Ponte embarcada experimental, derivada do firmware oficial Meshtastic. O objetivo é usar
os aplicativos originais Bitchat e Meshtastic, sem computador, MQTT, Nostr ou internet na
operação. Computador continua necessário para compilar, instalar e diagnosticar.

**A versão `.2` está suspensa por falha de boot no Heltec V3. A `.3` é uma pré-release
para testar as correções de inicialização, ainda sem validação física.** Use somente o novo
pacote após backup e conferência. O Cardputer permanece sem teste físico.
Veja o [registro da investigação](docs/bridge/BOOT_INCIDENT.md) e o
[roteiro de recuperação no Windows](docs/bridge/WINDOWS_RECOVERY.md).

**Estado: correção em teste; a falha física reportada foi na versão `.2`.**
Uma compilação bem-sucedida não comprova descoberta BLE, entrega, consumo, boot, atualização
ou recuperação. Esta versão não é um produto validado para uso de campo.
Consulte os resultados efetivamente executados em [TESTING](docs/bridge/TESTING.md).

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
[Matriz completa](docs/bridge/COMPATIBILITY.md).

O núcleo de protocolo é compartilhado, mas o firmware é compilado **por modelo de placa**.
Processador igual não garante pinagem, rádio, flash, partições ou bootloader iguais. Não há
binário universal ESP32 ou nRF52; instalar um arquivo de outro modelo pode impedir o boot.

## Download

Os arquivos estão em [Releases](https://github.com/EduChacon/meshtastic-bitchat/releases).
As versões anteriores à `.3` permanecem como evidência, **não para novas instalações**.
A `.3` destina-se a teste de recuperação após o preflight; uso de campo ainda não homologado.
Os nomes abaixo identificam os artefatos:

| Placa                         | Pacote para upload próprio no flasher                            |
| ----------------------------- | ---------------------------------------------------------------- |
| Heltec V3                     | `meshtastic-bitchat-heltec-v3-<versão>-<commit>.zip`             |
| Cardputer ADV + Cap LoRa-1262 | `meshtastic-bitchat-m5stack-cardputer-adv-<versão>-<commit>.zip` |

Também há um `-update.bin` por placa, `SHA256SUMS`, o verificador, as fontes e os resultados
dos testes. **Source code (zip/tar.gz)** é código-fonte, não firmware para o instalador.
Leia [INSTALL](docs/bridge/INSTALL.md) antes de usar o
[Web Flasher oficial Meshtastic](https://flasher.meshtastic.org/). Esta publicação não exige
inclusão no catálogo oficial. O fluxo de gravação permanece **documentado, não testado em hardware**.

## Começo seguro

1. Confirme modelo/revisão, banda/antena, aplicativos e sistema operacional.
2. Faça a bancada stock nos dois sentidos e guarde um backup privado e a recuperação stock.
3. Leia [instalação e rollback](docs/bridge/INSTALL.md). Os pacotes contêm apenas atualização
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

```sh
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

Os comandos constroem software; não fazem upload. Use somente fontes e ferramentas oficiais
fixadas. As dependências e a ferramenta precisam de download na preparação.

- [Arquitetura, limites e hipóteses refutáveis](docs/bridge/ARCHITECTURE.md)
- [Compatibilidade e cobertura funcional](docs/bridge/COMPATIBILITY.md)
- [Instalação, verificador e recuperação](docs/bridge/INSTALL.md)
- [Ameaças, privacidade e limitações](docs/bridge/SECURITY.md)
- [Testes, bancada, consumo e critérios](docs/bridge/TESTING.md)
- [Fontes, proveniência e licenças](docs/bridge/SOURCES.md)
- [Registro do trabalho](docs/bridge/WORKLOG.md)
- [README original do firmware](docs/bridge/README.upstream.md)

O histórico oficial e o histórico inicial do projeto foram preservados. Não é um projeto
MeshCore. Licença do firmware: [GPL-3.0](LICENSE); as dependências mantêm suas próprias licenças.
