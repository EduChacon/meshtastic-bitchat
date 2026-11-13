# Registro de implementação e validação

## Base e proveniência

- Meshtastic 2.7.26.54e0d8d Beta: `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
- Protobufs: `6b1ded439633cd03d4af85b44231b91d1d106278`.
- Bitchat iOS 1.8.0: `30c58eee349e172b37f69faa2113c9d5f2c76c23`.
- Commit inicial do projeto: `9040e45db23acfd906753d7c3c28dd121d280c8b`.
- Integração com preservação dos históricos: `78464ecac093adba7e927ce15e8b7f4f37074d0a`.
- [CI da importação](https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43434050474).

O README inicial e o README upstream foram preservados. Os workflows upstream estão
arquivados; o workflow ativo verifica e empacota os dois alvos desta entrega. Nenhum
procedimento de build ou teste deste projeto aciona flash ou descobre dispositivos USB.

## Implementação

- Núcleo C++ portátil com codec, limites de memória, expiração, filas, deduplicação e regras
  independentes para relé BLE, tradução pública e túnel experimental.
- Adaptadores Meshtastic e ESP32/NimBLE; controles no menu e painel de estado local.
- Publicação de texto em `TEXT_MESSAGE_APP`, sem alteração dos aplicativos de referência.
- Recursos desligados por padrão; canal secundário opt-in; sem tradução de DMs em B.
- Journal persistente limitado, com entradas individuais para reduzir escrita em flash.
- Atualizações de aplicativo por placa, manifesto, SHA-256, verificador sem escrita,
  fontes correspondentes e inventário de dependências.

Alvos de compilação: Heltec V3 e Cardputer ADV + Cap LoRa-1262. Portes da ponte para nRF52,
T-Deck e outras placas não estão incluídos. Interoperabilidade com nós stock permanece
prevista pelo uso do protocolo Meshtastic nativo, sujeita à bancada por rádio/configuração.

## Evidência de software

A [CI de integração](https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43449290177)
passou nos dois alvos, incluindo:

- Compilação das aplicações e comparação com uma recompilação limpa no mesmo ambiente.
- 523 verificações em host, 19 vetores do cliente Bitchat oficial e 100.000 entradas
  malformadas sob AddressSanitizer e UndefinedBehaviorSanitizer.
- Sete testes defensivos de artefatos, incluindo alvo incorreto, corrupção e conteúdo indevido.
- Cppcheck nos sete arquivos de implementação da ponte.
- Compilação Heltec sem a flag da ponte, como verificação de regressão de compilação.
- Empacotamento e verificação somente em software, sem leitura ou escrita de rádio.

Uma revisão das métricas de fonte encontrou risco de corte de contadores no visor. O painel
passou a abreviar TX/RX e a usar espaçamento conforme a fonte da placa. Os limites calculados
cabem nas larguras de 128/240 pixels; a última linha termina em 61/91 pixels. Os dois alvos
compilaram após a correção. Isso é cálculo de layout, não inspeção de tela ou botão em hardware.

Cada release repete as verificações no commit publicado. Os manifestos identificam esse
commit; `test-evidence.tar.gz` contém comandos e resultados efetivos. Números de RAM/flash
do linker são medidas estáticas, não medições de heap livre ou consumo sob carga. A
recompilação idêntica atesta o mesmo ambiente, não reprodutibilidade entre toolchains distintas.

## Limites conhecidos

Assinaturas Bitchat dependem do padding canônico, mesmo quando o frame público é transmitido
sem padding. Rotas são assinadas. REQUEST_SYNC é local ao enlace; a ponte não inventa contexto
RSR. Tráfego Noise opaco não permite distinguir texto de mídia privada. LongFast não atende
à taxa da voz contínua atual. B exige um único exportador por domínio local sobreposto.

Boot sem relógio confiável requer sincronização local antes de originar mensagens. Portanto,
recuperação autônoma completa após perda de energia ainda não está atendida. Sincronização
histórica transparente não é suportada, e sessões privadas em C permanecem experimentais,
com risco de timeout imposto pela capacidade/latência LoRa.

## Evidência física e próxima etapa

O primeiro boot de `.2` reportado em Heltec V3 **falhou**: logotipo parado e task watchdog.
O registro sanitizado e as hipóteses estão em [BOOT_INCIDENT](BOOT_INCIDENT.md). Navegação,
descoberta iOS/Android, coexistência BLE, RF, preservação de configuração, rollback,
energia e estabilidade prolongada permanecem não validados. Aprovação de CI não altera isso.

O menor ensaio útil é identificar os dispositivos e medir comunicação stock nos dois sentidos.
Depois, com backup privado, layout/slot conferidos e artefato específico, testar stock → ponte
OFF → A → B → stock. C exige dois terminadores e ensaio separado. Os controles negativos,
critérios de aprovação e registros estão em [TESTING](TESTING.md); recuperação em
[INSTALL](INSTALL.md). Não executar o harness upstream de hardware como teste de software:
seu fluxo de instalação pode gravar rádios conectados.

## Investigação de boot — 2026-11-13

As instalações foram suspensas nos dois READMEs e nas pré-releases, preservando os arquivos
e o histórico. Identificada reserva antecipada de cerca de 68 KiB para módulo e enlaces,
mesmo com a ponte desligada. Isso motivou alocação posterior ao início do BLE, liberação ao
desativar, recusa por falta de memória e diagnóstico mínimo. A causa do watchdog real
continua não demonstrada; não se afirma que o hardware esteja recuperado.

Os testes de protocolo passaram novamente: 523 assertivas, 19 vetores oficiais e 100.000
entradas malformadas com ASan/UBSan, além de sete verificações de pacote. Um novo teste
do adaptador BLE real com transporte/heap substituídos passou com sanitizers, incluindo
alocador sem memória, reativação limpa e três enlaces. O primeiro build candidato Heltec
compilou com 132.528 bytes estáticos, contra 163.424 na release afetada; esse número não
mede consumo dinâmico. A CI deve registrar o resultado final do commit e preservar símbolos.

A Alpha 2.8.2 foi examinada sem migrar a base: os fixes considerados ainda não correspondem
à falha observada. A próxima etapa física depende de backup privado e confirmação separada
da operação. Nenhum comando desta investigação acessou ou gravou o rádio.
