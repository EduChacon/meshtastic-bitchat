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

Quarto passo. O operador fechou e abriu o Bitchat: `Phones` foi a 0 e voltou a 1, com `People`
em 0. A hipótese do passo anterior está **afastada**: o Bitchat se conecta e se inscreve. A
falha está na troca de anúncios nos dois sentidos. Releitura do aplicativo de referência:
anúncio e mensagem não recebem preenchimento no Bluetooth; o aplicativo fragmenta o que
escreve quando o quadro passa do limite de escrita da conexão; e o anúncio precisa de
assinatura válida, identidade derivada da chave Noise e idade de até 15 minutos. Defeito
encontrado na ponte, introduzido na `1.0.0-beta`: o marcador de ponte de 22 bytes levou o
anúncio a 196 bytes, acima de uma notificação no MTU de 185 bytes, e o conteúdo a 110 bytes,
acima do limite de 100 em que o aplicativo tenta comprimir. A `.3` reduz o marcador a 8 bytes
(nó e 4 bytes do código de autenticação): anúncio de 182 bytes e conteúdo de 96. Relação com
o sintoma **não confirmada**. O sentido telefone → ponte continua sem causa conhecida; em
simulação, um anúncio com vizinhos, capacidades e célula de ponte, recebido em dois
fragmentos, é aceito. O relatório do `!bridge` (`rx`, `ok`, `old`, `sig`, `fmt`) é o dado que
falta.

Quinto passo. Relatório do aparelho com a `.3`: `Bluetooth 1, Bitchat 0, people 0`, `UTC` certo,
`rx 0 ok 0`, `tx` subindo de 20 em 20 s, nenhuma recusa. Com o aplicativo Meshtastic fechado e
só o Bitchat aberto, a tela mostrou `Phones 0` e `People 0`. A `experimental.5`, reinstalada
como controle, também não funcionou; segundo o operador, ela havia funcionado por pouco tempo
antes. Conclusões sustentadas: relógio, versão do aplicativo, falta de conexão e convivência
dos dois aplicativos estão **afastados**; a causa não está só nas mudanças feitas desde a
`experimental.5`; a ponte conta toda escrita recebida e não recebe nenhuma. **Correção:** o
marcador de 22 bytes já existia na `experimental.5`; a afirmação do quarto passo, de que foi
introduzido na `1.0.0-beta`, estava errada.

Leitura completa do caminho de envio do aplicativo de referência, com revisão independente.
Verificado no código: as três chamadas de escrita usam escrita sem resposta e dependem de
`canSendWriteWithoutResponse`; não há escrita com resposta nem outro meio de entregar dados a
um periférico; com limite de escrita abaixo de 99 bytes (MTU abaixo de 102) todo pacote vira
fragmento de 99 bytes, que não cabe; o primeiro anúncio é planejado 50 ms depois da descoberta
da característica. Não decidível pelo código: por que o iOS reteria as escritas. Relatos
públicos registram o indicador falso de forma persistente, sem causa estabelecida.

A `.4` contorna a dependência: a ponte, como cliente GATT na conexão aberta pelo telefone,
inscreve-se no serviço que o aplicativo publica; pede o MTU ao conectar; e só usa a
notificação com telefone cujas escritas chegaram. Hipótese de que depende, **não ensaiada**:
o iOS expõe o serviço de um aplicativo a um periférico conectado, como faz com os serviços do
sistema. Se não expuser, o relatório mostrará `path 10` e `wr 0`, e o defeito continua aberto.

Sétimo passo. Relato do operador com a `.4` no mesmo Heltec V3 e no mesmo telefone: tela
`Ready: Bitchat + LoRa`, `Phones 1/1`, `People 2` (um segundo aparelho Bitchat estava ligado)
e `LoRa out 2 in 1`; segundo ele, as mensagens passaram nos dois sentidos. O defeito do
telefone mudo **não se repetiu** nessa sessão. Não registrado: as respostas do `!bridge`. Por
isso **não se sabe** qual das mudanças resolveu, o caminho pelo serviço do telefone
(`path 9`) ou o pedido de MTU ao conectar com o telefone voltando a escrever (`path 10`,
`wr 1`), nem se a hipótese sobre o iOS expor o serviço do aplicativo foi a que valeu. Também
não há ensaio de duração, de reconexão ou com o aplicativo Meshtastic aberto ao mesmo tempo.
A publicação da `.4` ficou pendente porque os trabalhos do GitHub Actions passaram a falhar
em dois segundos, sem executar passo algum; o pacote do Heltec V3 foi retirado da execução
verificada do commit `6facd9f` e entregue ao operador diretamente.

Oitavo passo. Relatório `!bridge` da mesma sessão, às 19:38 UTC:

```text
con 1 dis 0 sub 1 wr 0 ntf 0 err 0 in 230 out 59
link 1 mtu 255 itv 40 enc 1 bond 1 sub 1 wr 1 path 9
Bluetooth 1, Bitchat 1, people 2
LoRa Bitchat out 2 in 1 lost 0
rx 230 ok 87 tx 59
old 0 sig 4 fmt 0
ver 0 pol 104 full 28
```

Conclusões sustentadas pela medição: a) o que resolveu foi o caminho pelo serviço do telefone
(`path 9`); todos os 230 quadros recebidos vieram por ele e os 59 enviados saíram por ele; b)
o iOS expôs o serviço do aplicativo ao rádio nesse aparelho, o que confirma ali a hipótese do
sexto passo; c) o defeito original **continua presente**: o telefone não fez escrita alguma
no serviço da ponte (`wr 0`); ele só deixou de impedir a troca; d) o MTU era 255, logo o pedido de
MTU não foi o fator; e) havia uma única conexão, criptografada e pareada, portanto a do
aplicativo Meshtastic: os dois aplicativos conviveram nela; f) nenhuma mensagem contada como
perdida. Sem explicação medida: `full 28`. Como `lost 0`, a origem compatível com o código é
a recusa de fragmentos quando as três remontagens simultâneas estão ocupadas, que não entra
na conta de perdidas; isso é inferência, e um texto longo vindo do telefone, que chega em
fragmentos, pode ser afetado. `pol 104` é o esperado: pedidos de sincronização que a ponte
ignora de propósito. Continua sem ensaio: duração, reconexão, queda de energia e Bitchat em
segundo plano.

Nono passo. Causa da falha de publicação, lida na anotação das execuções: "The job was not
started because recent account payments have failed or your spending limit needs to be
increased". Era o limite de gastos do GitHub Actions da conta, não o código. O operador tornou
o repositório público e mesclou o ramo de trabalho na `main` pelo PR 1, o que trouxe para a
`main` a pasta provisória `downloads`, a remover quando a versão estiver publicada.

Décimo passo. A execução 43634808137 da `main` (commit `768b6e1`) terminou com sucesso e publicou
a `v1.0.0-beta.4` às 20:09 UTC de 2026-11-14, com o ZIP único, o `SHA256SUMS` e o pacote técnico.
A pasta provisória `downloads` foi removida.

Décimo primeiro passo, no ramo de trabalho, sem publicação. a) Alvo de ensaio para o SenseCAP
T1000-E (nRF52840): o código de enlace dos ESP32 compila sem alteração sobre a camada
`src/bridge/nrf52`; o firmware passa a aceitar duas conexões, a pilha Bluetooth recebe 40 KB e o
trabalho da ponte roda em tarefa própria de 12 KB, porque a tarefa principal tem 4 KB de pilha
nesse chip. Compilou com 517444 de 815104 bytes de flash; o binário usa a origem de RAM
`0x2000A000`. Não foi ligado em aparelho. Detalhes e roteiro em T1000E.md. b) Comandos para
rádios sem tela: `!bridge on`, `off`, `local`, `lora` e `help`, aceitos só do aplicativo
conectado ao próprio rádio. O teste novo revelou um defeito: um comando que desligava a ponte
deixava o mesmo ciclo seguir com a memória já liberada; o ciclo agora termina após o comando.
c) O serviço do Meshtastic entrou na resposta de varredura do anúncio da ponte nos ESP32, com o
nome reduzido para caber. Muda o anúncio do Heltec V3 já ensaiado.

Décimo segundo passo. O operador pediu a publicação imediata, com suporte a rádios sem tela.
Acrescentado: o nome anunciado na resposta de varredura passa a ser o do próprio rádio (inteiro
quando cabe em 11 caracteres; senão `Mesh_` e os quatro dígitos finais). Versão `1.0.0-beta.5`:
os dez alvos ESP32 pelo fluxo de verificação de sempre e, à parte, o arquivo
`EXPERIMENTAL-t1000-e-bitchat-1.0.0-beta.5.uf2`, fora das verificações. Estado declarado nas
notas: as mudanças desta versão foram simuladas (965 verificações de fluxo) e compiladas, sem
ensaio em aparelho; o ensaio em aparelho continua sendo o da `1.0.0-beta.4`, em um Heltec V3.
Antes do envio à `main`, a execução 43641506857 do ramo de trabalho compilou o Heltec V3 com a
análise estática e o T1000-E, com e sem a ponte, sem falhas.

Décimo terceiro passo. O operador aprovou ligar as redes Meshtastic e MeshCore pelo Bitchat,
depois de ver as duas pontes na mesma conversa. Versão `1.0.0-beta.6`: texto que uma ponte
MeshCore–Bitchat traz ao Bitchat (`[MC nome] texto`) passa a sair no canal do Meshtastic como
`MC nome: texto`, uma vez só. A regra contra laço é de marcação: nome iniciado por `MT ` ou
`MC ` indica texto que já atravessou, e nenhuma ponte o leva de volta; ouvido no LoRa, o texto
marcado chega aos telefones com o nome marcado, em todas as partes. A escolha `!bridge link
on/off` fica em uma entrada própria da memória, sem mudar o formato das configurações já
gravadas; no T1000-E ela não sobrevive ao reinício. Verificado só em simulação (643 verificações
do núcleo e 997 de fluxo) e por compilação.
Antes do envio à `main`, a execução 43652380386 do ramo de trabalho compilou o Heltec V3 com a
análise estática e o T1000-E, com e sem a ponte, sem falhas.

Décimo quarto passo. O operador pediu que o modo econômico pudesse ser escolhido também nesta
ponte, como comando. Versão `1.0.0-beta.7`: o `Battery saver`, que só existia no menu da tela,
passa a ter os comandos `!bridge economy on` e `!bridge economy off`, aceitos do aplicativo
Meshtastic conectado ao próprio rádio; o relatório `!bridge` mostra `Saver on` ou `Saver off`.
O efeito do ajuste e o padrão (desligado) não mudaram. Verificado só em simulação (645
verificações do núcleo e 1002 de fluxo) e por compilação.

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
