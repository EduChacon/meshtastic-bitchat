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
