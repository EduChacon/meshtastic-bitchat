# Falha de boot reportada — Heltec V3

## Estado

Em 2026-11-13 foi reportada falha no primeiro boot de `v0.1.0-experimental.2`, commit
`0e981daffaa8d05287fdc311530d60949f782f4d`, no alvo `heltec-v3-bitchat`.
O logotipo permaneceu parado e o watchdog reiniciou o dispositivo. Trata-se de evidência
fornecida pelo operador, não de um ensaio instrumentado executado pelo projeto.

As instalações das pré-releases existentes estão suspensas. A versão anterior compartilha
o código de inicialização sob investigação; não foi comprovado que apresente a mesma falha.
Cardputer ADV não foi testado fisicamente. Não há imagem corrigida validada em hardware.

## Evidência sanitizada

- ESP32-S3 QFN56, revisão v0.2, flash embutida de 8 MB.
- A ferramenta terminou a escrita de 2.060.800 bytes em `0x10000` e solicitou reset.
- O banner identificou `2.7.26.0e981da for heltec-v3-bitchat`.
- A execução chegou à redução de frequência para 80 MHz, no fim de `setup()`.
- Em 142.351 ms, o watchdog acusou `loopTask (CPU 1)` sem atendimento e abortou.
- O backtrace fornecido pertence ao core 0 durante o watchdog; ele não identifica por si
  só a chamada bloqueada na tarefa principal do core 1.
- O prefixo SHA-256 do ELF informado no panic foi `5c1343a83b5e1b65`.
- Mensagens sobre arquivos LittleFS ausentes e o posterior `BreakError` do navegador não
  demonstram, isoladamente, corrupção de configuração, causa do watchdog ou dano físico.

MAC, identificação pessoal, chaves, payload e dumps de flash não são registrados aqui.
O relato de escrita não comprova preservação de toda a configuração nem verificação das
partições. Não foi obtido backup ou inventário de partições/slot desse aparelho.

## Hipóteses e como refutá-las

1. **Pressão de memória ao iniciar BLE:** no ELF local anterior, os buffers BLE globais
   ocupam 30.912 bytes e a construção do módulo solicita 37.800 bytes, mesmo desligado.
   A pilha principal também foi aumentada. Isso é um defeito de isolamento de recursos,
   mas ainda não prova que causou este watchdog. Registrar heap livre, maior bloco e
   estágios do boot; refutar se a execução ultrapassar a inicialização BLE e bloquear em
   outra etapa, ou se a falha persistir sem essas reservas.
2. **Bloqueio em biblioteca/callback:** NimBLE-Arduino 1.4.3 espera sincronização sem prazo
   e seu port FreeRTOS não confere a criação da tarefa host. Instrumentar fronteiras de
   inicialização e falhas de alocação; não desativar ou alimentar artificialmente o watchdog.
3. **Configuração ou armazenamento preexistente:** ainda sem evidência suficiente.
   Conferir somente por leitura/backup privado antes de qualquer alteração. Não usar
   apagamento integral para testar essa hipótese.

## Recuperação

Não instalar outro pacote experimental para tentar destravar. Primeiro preservar a flash
existente por leitura privada e identificar partições/slot, em sessão autorizada. O fato
de o carregador ROM e a aplicação terem executado oferece uma via de recuperação por USB,
mas não é garantia de ausência de dano ou de recuperação sem perda.

O artefato oficial de referência para retorno é a aplicação **Heltec V3**
`firmware-heltec-v3-2.7.26.54e0d8d.bin`, dentro do
[ZIP oficial ESP32-S3](https://github.com/meshtastic/firmware/releases/download/v2.7.26.54e0d8d/firmware-esp32s3-2.7.26.54e0d8d.zip).
O arquivo foi conferido localmente: 2.109.248 bytes, SHA-256
`34c38ff938380a64ad905ab2b1086172190d83bce0b62d53b95ac490d96c1fc6`.
Ele não é a imagem `.factory.bin`. Nenhuma gravação de recuperação foi executada.

Confirmar aparelho, revisão/banda, arquivo, backup, consequências e rollback antes de
gravar novamente. Os procedimentos gerais permanecem em [INSTALL](INSTALL.md).

## Correção candidata, ainda sem validação física

A branch `fix/heltec-boot-memory` move os buffers grandes do módulo e das conexões BLE
para alocação posterior ao início do Bluetooth, somente quando a ponte é ativada. Desativar
a ponte libera os buffers. Falha de alocação ou reserva insuficiente bloqueia a ativação,
com indicação `Low memory: bridge OFF`; não altera canais, identidade ou região.

O módulo no caminho de boot passa de 37.800 para 2.032 bytes no build local de Heltec;
a reserva estática dos enlaces de 30.912 bytes passa a um ponteiro. O linker local reporta
132.528 bytes de RAM estática no primeiro build candidato. Essas são medidas de software,
não medições de heap livre ou prova de que a memória causou a falha observada.

O limite inicial exige 24 KiB de heap interno ainda livre após a alocação, incluindo um
bloco de pelo menos 8 KiB. É uma barreira conservadora provisória, não uma garantia de
capacidade sob carga. Deve ser ajustada somente com medições reais. A pilha principal
e o watchdog não são reduzidos, desligados nem alimentados dentro de esperas bloqueadas.

Diagnósticos mínimos de boot informam estágio, heap livre, maior bloco e mínimo observado;
falhas de alocação informam tamanho e capacidades. Não incluem chaves, mensagens, nomes
ou posições. Os artefatos de CI passam a preservar ELF e mapa correspondentes ao binário.

O teste host executa o adaptador BLE real com substitutos de transporte/heap e sanitizers:
desligado sem buffers, memória insuficiente, falha do alocador, ativação, três enlaces,
remontagem de writes, filas cheias, justiça, expiração, desativação e reativação sem resíduos.
Isso não executa o controlador Bluetooth nem o FreeRTOS do ESP32. A CI não publica outra
release automaticamente; um candidato aprovado em software ainda exige ensaio autorizado.

## Avaliação da versão oficial 2.8.2

A [2.8.2.d5f1619 é Alpha](https://github.com/meshtastic/firmware/releases/tag/v2.8.2.d5f1619).
A correção de [DIO1 após light sleep](https://github.com/meshtastic/firmware/pull/12131)
trata watchdog de interrupção após suspensão; o relato presente é de task watchdog durante
boot. O [limite de BUSY](https://github.com/meshtastic/firmware/pull/12105) reduz espera de
1.000 para 50 ms, e não demonstra a origem deste bloqueio. A
[correção de DFU BLE](https://github.com/meshtastic/firmware/pull/12220) é específica de nRF52,
não da escrita USB observada no ESP32-S3. Não há evidência suficiente para considerar a
migração completa uma correção deste incidente; a base 2.7.26 permanece fixada.
