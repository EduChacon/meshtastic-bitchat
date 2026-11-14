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
- Inicialmente recursos desligados por padrão; na `.4`, BLE/texto público dedicado ativos
  no primeiro uso, conforme [migração](DEFAULTS.md). Sem tradução de DMs em B.
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

## Versão 1.0 beta — 2026-11-14

Revisão do código da `.5` comparada com o Bitchat iOS 1.8.0, motivada por relato do operador
de funcionamento inconstante, sobretudo em grupo. As causas de perda encontradas, as mudanças
e os limites estão em [CANDIDATE.md](CHANGES.md). O firmware `.5`, até então fora da `main`,
foi incorporado ao ramo de trabalho com a documentação da `main`.

Evidência: todos os testes de host anteriores, ajustados ao novo comportamento, e o teste novo
de fluxo completo (`tools/bridge/test_flow.py`, 709 verificações), que compila o módulo de
produção. O ambiente de trabalho não alcança o registro do PlatformIO; a compilação dos dois
alvos depende da CI do repositório. Nenhum comando acessou ou gravou rádio. Entrega entre
aparelhos, tela, consumo e estabilidade prolongada permanecem não validados.

Na mesma data, por decisão do operador: hora fornecida pelo telefone Bitchat na falta de fonte
confiável; repetição nativa do Meshtastic para o texto da ponte; alvos de compilação para mais
oito aparelhos ESP32-S3, com as ferramentas de pacote aceitando 16 MB; e publicação como
`1.0.0-beta`, a partir da `main`, sem os gates físicos de [TESTING.md](TESTING.md), que continuam pendentes.
O firmware das revisões `.3` a `.5`, que estava nos ramos `fix/heltec-boot-memory` e
`fix/bridge-menu`, passou a integrar a `main`; os ramos e as tags anteriores foram preservados.

Depois da publicação, a documentação foi reorganizada: [índice](README.md), guias reescritos
para a `1.0` e documentos das revisões anteriores em `history/`. A CI passou a verificar e
publicar somente a partir da `main`, e o fluxo de aviso das pré-releases suspensas, já
cumprido, foi removido.

A `1.0.0-beta.2` muda a entrega, a pedido do operador: um único ZIP para os dez aparelhos,
aceito pelo flasher oficial, com os pacotes por modelo e as ferramentas de backup no pacote
técnico.

Primeiro relato em aparelho, na `1.0.0-beta`: no rádio em que o ZIP foi instalado, o canal
`Bitchat` não apareceu. O estado da tela não foi informado, e a causa exata naquele aparelho
não foi determinada. A leitura do código mostrou que a criação do canal só ocorria uma vez por
aparelho e que uma posição escrita pelo aplicativo com ajustes de módulo padrão não era aceita
como vazia. A `.2` recria o canal padrão sempre que ele falta, salvo escolha de `Bitchat only`
ou de outro canal, aceita essas posições e mostra `No LoRa channel` quando não há posição livre.

Segundo relato, mesma versão, com foto da tela: `Ready: Bitchat + LoRa`, `Phones 1  People 0`,
`LoRa out 0  in 1`, sem `Not delivered`; no Bitchat, a ponte não aparece na lista de pessoas.
O canal conferido no aplicativo estava correto. Conclusão sustentada pela evidência: o enlace
Bluetooth existe, o LoRa entrega à ponte, e ponte e telefone não se reconhecem. Causa
**desconhecida**. Hipóteses que a evidência não distingue: diferença de relógio acima de 2
minutos; versão do aplicativo diferente da referência 1.8.0; pacotes que não chegam em um dos
sentidos. A `.2` acrescenta `Settings → Diagnostics` para separar essas hipóteses. No mesmo
exame do aplicativo foi encontrado e corrigido outro defeito, sem relação demonstrada com esse
sintoma: texto de 100 bytes ou mais assinado pela ponte era recusado pelo telefone.

Terceiro passo do mesmo relato. O aplicativo instalado é o 1.8.0 de referência, o que afasta a
hipótese de versão. A leitura da biblioteca NimBLE-Arduino 1.4.3 mostrou que a inscrição de um
telefone pareado é gravada e restaurada sozinha a cada reconexão, e que a ponte contava essa
inscrição como `Phones`. O `Phones 1` da foto, portanto, não prova que o Bitchat estava
conectado. Hipótese compatível com a evidência, **não confirmada**: o Bitchat não se conectou
enquanto o aplicativo Meshtastic estava conectado ao mesmo rádio pelo mesmo telefone. O rádio
é periférico e não consegue iniciar a conexão; consegue anunciar, limpar o próprio estado e
informar o que vê. A `.3` faz as três coisas: `Phones` conta só o telefone que enviou dado do
Bitchat; `!bridge`, digitado no aplicativo Meshtastic ou no Bitchat, devolve o relatório; e
`!bridge reset` ou `Reconnect phones` envia a indicação de serviços alterados e derruba as
conexões. Não verificado em aparelho: como cada aplicativo exibe a resposta e o efeito da
indicação de serviços alterados no iOS.

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

A CI do commit `bd5bfbe6c087dee993f4b5f5084c7e1f3f3d821d` concluiu os dois alvos com
sucesso, inclusive recompilação idêntica e build Heltec sem flag. Os pacotes e a associação
binário/ELF foram conferidos após download; os hashes estão em BOOT_INCIDENT. Uma reconstrução
independente do commit original reproduziu exatamente o binário e o ELF do panic. A pilha
decodificada confirma watchdog sobre a tarefa ociosa do core 0, sem localizar o bloqueio
do core 1. Faltam backup privado, leitura do registro de falha e validação física; a publicação
de nova release permanece suspensa.
