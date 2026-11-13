# Falha de boot reportada — Heltec V3

## Estado

Em 2026-11-13 foi reportada falha no primeiro boot de `v0.1.0-experimental.2`, commit
`0e981daffaa8d05287fdc311530d60949f782f4d`, no alvo `heltec-v3-bitchat`.
O logotipo permaneceu parado e o watchdog reiniciou o dispositivo. Trata-se de evidência
fornecida pelo operador, não de um ensaio instrumentado executado pelo projeto.

As instalações das pré-releases anteriores à `.3` estão suspensas. A versão inicial compartilha
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

### Decodificação com símbolos exatos

A [reconstrução de símbolos](https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43469976491)
recompilou o commit original sem alterações. O SHA-256 da aplicação coincidiu exatamente:
`f0cac2e8eddf8497df7e877aa223bb85b0cede2e60ec96ce7394b28dc6a9fb63`.
O ELF completo tem SHA-256
`5c1343a83b5e1b6514ed9591d0010afd36ae33390025997c3660fb4329716d1b`, cujo prefixo coincide
com o panic fornecido. A pilha decodificada é:

```text
panic_abort → esp_system_abort → abort → task_wdt_isr → _xt_lowint1
→ cpu_ll_waiti → esp_vApplicationIdleHook → prvIdleTask
```

Isso confirma que a pilha pública é a interrupção do watchdog sobre a tarefa ociosa do
core 0. Ela não localiza o bloqueio de `loopTask` no core 1. Portanto, a causa continua
em aberto. A configuração do SDK habilita coredump em flash; se um registro válido existir,
uma leitura privada pode permitir examinar o core 1 antes de outra instalação. Coredump
contém memória potencialmente sensível e não deve ser publicado nem enviado à CI.

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

Antes de testar o pacote de correção `.3`, preservar a flash
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

### Resultado da CI do candidato

Commit de firmware `bd5bfbe6c087dee993f4b5f5084c7e1f3f3d821d`, candidato
`0.1.0-boot-candidate.1`: [CI concluída](https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43468801100).
Os dois jobs de verificação passaram; o job de publicação foi deliberadamente ignorado.
Compilação por placa, testes de protocolo e adaptador com ASan/UBSan, sete testes de pacote,
Cppcheck e recompilação limpa idêntica passaram. A compilação Heltec sem a flag também passou.

| Alvo                       |  RAM estática | Flash da aplicação indicada pelo linker | Boot físico deste candidato |
| -------------------------- | ------------: | --------------------------------------: | --------------------------- |
| Heltec V3                  | 132.528 bytes |                         2.031.325 bytes | Não testado                 |
| Cardputer ADV              | 136.928 bytes |                         2.147.065 bytes | Não testado                 |
| Heltec sem a flag de ponte | 132.104 bytes |                         2.109.001 bytes | Não testado                 |

Os ZIPs da CI foram baixados e conferidos independentemente. SHA-256 dos pacotes:

- Heltec: `8dde06309f11bdca432385f96b795dabd4c3f752d62824bf7cff62a81ed9968c`.
- Cardputer ADV: `989bf614c7d74ff70fe43453c34d74d4bca5e0924fae5983842ec108555b77d3`.

SHA-256 das aplicações:

- Heltec: `93c04180ed67ddf1ac8f9f628ac4d5b666e7f8bcf4d8656f009d2ecbf6bc8af5`.
- Cardputer ADV: `eabb745fa6a6c8fdd3f326410e530945a8f67704ac76d5a3b41fab419fcfc62e`.

Os símbolos de cada candidato também foram conferidos contra o SHA-256 do ELF embutido
na aplicação. O verificador rejeitou o ZIP Heltec quando informado o alvo Cardputer.
Nenhum desses testes identifica o hardware conectado ou comprova boot/rollback. Os
artefatos permanecem candidatos da CI, não uma nova release declarada pronta para instalar.

## Avaliação da versão oficial 2.8.2

A [2.8.2.d5f1619 é Alpha](https://github.com/meshtastic/firmware/releases/tag/v2.8.2.d5f1619).
A correção de [DIO1 após light sleep](https://github.com/meshtastic/firmware/pull/12131)
trata watchdog de interrupção após suspensão; o relato presente é de task watchdog durante
boot. O [limite de BUSY](https://github.com/meshtastic/firmware/pull/12105) reduz espera de
1.000 para 50 ms, e não demonstra a origem deste bloqueio. A
[correção de DFU BLE](https://github.com/meshtastic/firmware/pull/12220) é específica de nRF52,
não da escrita USB observada no ESP32-S3. Não há evidência suficiente para considerar a
migração completa uma correção deste incidente; a base 2.7.26 permanece fixada.

## Guarda de inicialização na versão experimental .3

A espera de sincronização de NimBLE 1.4.3 foi limitada a cinco segundos, com bloqueio
cooperativo de um tick entre consultas. A criação de `nimble_host` agora é conferida;
falha não é anunciada como sucesso. Falhas NVS/controlador/HCI/nome GAP retornam sem
criar serviços nem anúncio. A inicialização BLE deixa de apagar NVS automaticamente
em caso de versão incompatível ou falta de páginas.

Após uma falha, `NimbleBluetooth` retém `startupFailed` até reinício. Isso impede repetição
sobre controlador parcialmente iniciado e evita deinit potencialmente bloqueante nesse
estado. A UI da ponte mostra `BLE failed: restart`; USB/LoRa e o laço principal podem
continuar. Não há desligamento ou alimentação artificial do watchdog. Não se promete
limitar uma chamada interna diferente que eventualmente bloqueie no SDK.

O patch só é aplicado nos dois ambientes da ponte, antes de compilar NimBLE. Os três
arquivos originais têm SHA-256 verificado; versão inesperada ou patch parcial interrompe
o build. A sincronização entre tarefas usa `std::atomic<bool>`. O código e os avisos de
licença originais permanecem no pacote de dependências correspondente.

`tools/bridge/test_startup.py` compila as funções reais extraídas do fonte que será
compilado para o ESP32, substituindo apenas SDK/FreeRTOS/GATT por fakes. Injeta falha em
seis etapas, falta/atraso de sync, passagem pelo limite de ticks, recuperação por novo
boot e sucesso com os mesmos requisitos de autenticação. O controle negativo recompõe
a função original a partir das alterações verificadas e confirma que continua presa
sem sync ou tarefa host. **É uma reprodução simulada de um defeito do código, não uma
reprodução em bancada da causa exata do relato.**

O helper `prepare_flash.py` usa esptool oficial somente para `flash_id` e `read_flash`.
Seu teste verifica comandos, logs privados, capacidade incorreta, erro de leitura,
backup incompleto, partições e seleção OTA divergentes. O próprio helper não foi
executado contra um rádio e não oferece escrita. O [roteiro Windows](WINDOWS_RECOVERY.md)
separa backup da decisão posterior de atualizar no flasher oficial.

A hipótese de boot será refutada se `.3` ainda parar após `setup-complete`, se o diagnóstico
mostrar que BLE terminou antes do bloqueio ou se a pilha do core 1 apontar outra chamada.
O primeiro aceite físico continua sendo boot OFF, dez minutos estáveis, conexão Meshtastic,
configuração preservada e retorno oficial sem apagamento integral.

A revisão da alocação tardia eliminou também a cópia da lista mutável de conexões de
NimBLE pela tarefa principal: um registro de três handles/assinaturas, protegido pelo
lock da ponte, é mantido pelos callbacks. Ele ocupa poucos bytes mesmo com a ponte OFF.
Assim, ativar/desativar não consulta um `std::vector` que outra tarefa pode modificar,
e assinaturas de conexões ainda abertas sobrevivem à liberação dos buffers. O teste
do adaptador cobre parada, retomada e desconexão, sem retenção dos payloads anteriores.
O empacotador ainda verifica nome e marcador da revisão compilada para recusar binário
local desatualizado, mesmo que o checkout atual esteja limpo.
