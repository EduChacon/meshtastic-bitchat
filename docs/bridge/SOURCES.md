# Fontes, versões e proveniência

Consulta em 2026-11-13. As referências fixadas abaixo prevalecem sobre `main`/`develop`
mutáveis. Documentação descreve comportamento esperado; ensaio físico permanece NT.

| Componente          | Revisão usada                                                                                                            | Motivo/evidência                                                                               |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------- |
| Firmware Meshtastic | [2.7.26.54e0d8d](https://github.com/meshtastic/firmware/commit/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb)                 | Beta oficial; foto do app rotula STABLE. Alpha 2.8.2 não foi necessária para o MVP             |
| Protobufs           | [6b1ded4](https://github.com/meshtastic/protobufs/commit/6b1ded439633cd03d4af85b44231b91d1d106278)                       | Submodule do firmware escolhido; porta 1 para texto, 256 PRIVATE_APP para domínio experimental |
| Bitchat iOS         | [1.8.0 / 30c58ee](https://github.com/permissionlesstech/bitchat/commit/30c58eee349e172b37f69faa2113c9d5f2c76c23)         | Fonte e codec usados para vetores; versão instalada desconhecida                               |
| Bitchat Android     | [2.2.0 / 20501fd](https://github.com/permissionlesstech/bitchat-android/commit/20501fdfe0d56c7705c377d4a263b01d8f38eeeb) | Referência identificada; interoperabilidade ainda NT                                           |
| Build               | PlatformIO 6.2.0; intelhex 2.3.0; espressif32 6.13.0                                                                     | CI Python 3.12.8; ferramentas efetivas registradas nos anexos                                  |
| BLE/crypto          | NimBLE-Arduino 1.4.3; rweather Crypto 0.4.0                                                                              | Versões fixadas pelo upstream; sem implementação de Noise intermediária                        |
| Formatação          | Trunk 1.25.0; clang-format 16.0.3                                                                                        | Configuração `.trunk`; telemetria desativada na execução local                                 |

## Integração e preservação

O repositório do proprietário não foi presumido vazio nem fork do GitHub. Seu commit inicial
`9040e45db23acfd906753d7c3c28dd121d280c8b` e o histórico oficial foram preservados pela
integração `78464ecac093adba7e927ce15e8b7f4f37074d0a`. O README inicial permanece em
`README.initial.md`; o README upstream, em `README.upstream.md`. Workflows upstream foram
arquivados em `.github/upstream-workflows`, evitando agendamentos e publicações alheios.

Pontos de extensão revisados: `src/modules/TextMessageModule.cpp`, `MeshModule`, `Modules.cpp`,
`Router`, `NimbleBluetooth` e menus locais. A porta 256 é privada, não uma alocação exclusiva
global: magic, versão, canal e domínio distinguem este experimento. Não há alterações em
PhoneAPI, StreamAPI, NodeInfo ou schemas de preferências nativas para fingir suporte dos apps.

## Protocolo Bitchat e iOS

- [WHITEPAPER na revisão](https://github.com/permissionlesstech/bitchat/blob/30c58eee349e172b37f69faa2113c9d5f2c76c23/WHITEPAPER.md)
  e [BinaryProtocol.swift](https://github.com/permissionlesstech/bitchat/blob/30c58eee349e172b37f69faa2113c9d5f2c76c23/localPackages/BitFoundation/Sources/BitFoundation/BinaryProtocol.swift):
  assinatura, padding, cabeçalhos v1/v2, flags, rota e compressão. O código/fixture controla
  detalhes que o whitepaper pode simplificar.
- [Link layer central](https://github.com/permissionlesstech/bitchat/blob/30c58eee349e172b37f69faa2113c9d5f2c76c23/bitchat/Services/BLE/BLEService%2BLinkLayerCentralRole.swift):
  montagem de streams de notificação. UUID no pacote principal é uma estratégia proposta,
  não comprovação de descoberta no iOS bloqueado.
- [Apple Core Bluetooth](https://developer.apple.com/documentation/corebluetooth/) e
  [TN3115](https://developer.apple.com/documentation/technotes/tn3115-bluetooth-state-restoration-app-relaunch-rules):
  restauração/relançamento depende do estado do sistema e do app. Encerramento manual não
  permite prometer entrega; o protocolo de bancada inclui os estados separadamente.
- [Bitchat #508](https://github.com/permissionlesstech/bitchat/issues/508) e
  [discussão Apple](https://developer.apple.com/forums/thread/739680) são contexto, não prova
  de compatibilidade do firmware produzido.

## Placas, RF, energia e instalação

- [Heltec, revisões](https://docs.heltec.org/en/node/esp32/wifi_lora_32/hardware_update_log.html):
  V3 muda MCU/radio para S3/SX1262 e pinagem difere da V2. Subrevisões de alimentação existem.
- [T1000-E](https://meshtastic.org/docs/hardware/devices/seeed-studio/sensecap/card-tracker/):
  nRF52840/LR1110; restrição documentada de recepção de SX127x, não de SX1262. A documentação
  atual também diferencia bootloader de fábrica e Meshtastic 2.5+, com acesso UF2 específico.
- [Cardputer Mesh Kit](https://docs.m5stack.com/en/core/Cardputer_Mesh_Kit) e
  [procedimento M5](https://docs.m5stack.com/en/guide/cardputer_mesh_kit/meshtastic): ADV + Cap
  LoRa-1262; confirmar conjunto, banda e revisão. O alvo existe no commit escolhido em
  `variants/esp32s3/m5stack_cardputer_adv`. O build upstream citado pelo proprietário é
  [referência adicional](https://github.com/meshtastic/firmware/actions/runs/21725885459),
  não teste deste fork.
- [Energia Meshtastic](https://meshtastic.org/docs/configuration/radio/power/): power saving
  stock pode interferir com BLE; não aplicar automaticamente ao perfil econômico da ponte.
- [Flasher oficial](https://flasher.meshtastic.org/),
  [guia ESP32](https://meshtastic.org/docs/getting-started/flashing-firmware/esp32/web-flasher/) e
  [nRF52](https://meshtastic.org/docs/getting-started/flashing-firmware/nrf52/): Chrome/Edge no
  computador para Web Serial; formatos/offsets e preservação precisam da conferência descrita
  em INSTALL. Documentação/código examinados; nenhum upload/flash físico executado.
- Brasil: manter controles regionais do firmware e conferir banda, antena, frequência
  efetiva e requisitos aplicáveis da [Anatel](https://www.gov.br/anatel/pt-br/regulado/certificacao).
  Uso comunitário de ANZ/LongFast não é laudo regulatório nem confirmação de homologação.
  O projeto não muda região, potência ou duty cycle para vencer testes.

## Licenças

Firmware e alterações desta integração: GPL-3.0 conforme `LICENSE`. Preservar avisos e
fornecer fonte correspondente, scripts e versões ao distribuir binários. Bitchat usa
Unlicense; serviu como especificação e gerador de vetores, sem incorporar app Swift ao rádio.
Crypto usa MIT; NimBLE inclui avisos próprios. Valores de pontos Ed25519 de pequena ordem
referenciam [libsodium 1.0.20](https://github.com/jedisct1/libsodium/tree/1.0.20-RELEASE), ISC;
o aviso está em `third-party/libsodium.LICENSE`. O SDK e bibliotecas upstream têm licenças
distintas, inclusive componentes binários; não atribuir GPL a todos eles. Seus avisos e fontes
resolvidas acompanham os bundles de dependências, e SDK/toolchain são identificados no manifesto.

## Inspirações que não são dependências

[GigaProjects](https://github.com/GigaProjects/Meshtastic-Bitchat-Bridge),
[evansmj](https://github.com/evansmj/firmware-bitchat-plugin),
[frindle](https://github.com/frindle/meshtastic-bitchat-bridge),
[bitle](https://github.com/bitleproject/bitle),
[SteveCohen](https://github.com/SteveCohen/Bitchat-Meshtastic),
[MeshCore-BC](https://github.com/eddieoz/MeshCore-BC) e
[meshtastic-adv](https://github.com/anton-vinogradov/meshtastic-adv) não foram copiados ou usados
como binários. Tradução Linux/TCP, túnel opaco e protocolo LoRa próprio são desenhos distintos.
MeshCore não é Meshtastic. Nenhum resultado desses projetos comprova esta implementação.
