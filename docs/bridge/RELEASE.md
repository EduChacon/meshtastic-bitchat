# Meshtastic ↔ Bitchat 0.1.0-experimental.3

Pré-release para teste de recuperação do boot no **Heltec V3**. As versões anteriores
permanecem suspensas. **Esta correção ainda não teve boot confirmado em aparelho real**;
compilação e simulação não certificam entrega BLE/RF, autonomia ou rollback.

## O que mudou

- Buffers grandes da ponte são reservados somente após o Bluetooth iniciar e a ponte
  ser ativada. Desativar libera memória; falta de espaço mantém a ponte OFF.
- NimBLE 1.4.3 recebe uma correção mínima, conferida por hash: verifica criação da tarefa
  host, limita a espera de sincronização a cinco segundos e propaga falhas de inicialização.
- Falha BLE retorna ao firmware com status `BLE failed: restart`, sem repetir inicialização
  parcial. O watchdog continua ativo. A inicialização BLE não apaga NVS para contornar erro.
- Diagnóstico de boot expõe apenas estágios/contadores de memória. Símbolos ELF/mapa exatos
  acompanham os artefatos para interpretar eventual falha.
- Helper opcional lê backup privado completo e confere chip, capacidade, partições e slot.
  Não grava flash; a instalação continua no Web Flasher oficial.

A reprodução controlada confirma um bloqueio no código antigo quando a tarefa host não
inicia ou não sincroniza. A correção passa nesses cenários simulados. **Não foi provado que
esse era o ponto exato bloqueado no rádio do relato**: a pilha recebida pertencia ao core 0,
enquanto a tarefa acusada estava no core 1. Detalhes e hipóteses refutáveis em
[BOOT_INCIDENT](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/BOOT_INCIDENT.md).

## Download e instalação

Para Heltec V3, baixar `meshtastic-bitchat-heltec-v3-0.1.0-experimental.3-<commit>.zip`.
O pacote Cardputer ADV é separado e continua sem ensaio físico. Não há binário genérico
por processador nem firmware da ponte para T1000-E/T-Deck nesta entrega.

Seguir [WINDOWS_RECOVERY](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/WINDOWS_RECOVERY.md), também anexado à release: backup privado,
preflight, firmware oficial de retorno, então **Atualizar / Update** no
[Web Flasher oficial](https://flasher.meshtastic.org/). Nunca selecionar **Apagar Flash e Instalar**
para este pacote. Se a interface não aceitar o ZIP, há o `-update.bin` correspondente.

O ZIP contém somente a aplicação da placa, sem bootloader, partições, filesystem ou apagador.
A escrita substitui os setores da aplicação; risco zero não existe. Nenhum backup deve
ser enviado ao repositório. A decisão de gravar o aparelho identificado é separada da leitura.

## Validação e limites

A publicação depende de compilação por placa, recompilação idêntica no mesmo ambiente,
testes de protocolo e entradas inválidas com sanitizers, testes do adaptador BLE com heap/
transporte simulados, falhas injetadas nas funções reais de inicialização, verificadores
de pacotes/backups e análise estática. O controle negativo usa a inicialização original.
Os resultados efetivos ficam em `test-evidence.tar.gz` e na execução Actions do commit.

Base oficial **2.7.26.54e0d8d**, ponte OFF por padrão. A Alpha 2.8.2 não foi adotada: suas
correções publicadas não demonstram resolver este incidente. Sem mudanças nas políticas
RF, no consentimento do modo B, na criptografia ou na separação entre tradução e túnel.

A publicação de teste pela branch de correção não promove a versão a estável nem faz merge
em `main`. Antes de uso de campo continuam pendentes: boot, preservação das configurações,
retorno ao firmware oficial, ensaios A/B/C com controles negativos, duração e energia.

## English

Experimental recovery-test release for Heltec V3. Large bridge buffers are now allocated
only after BLE startup and explicit bridge activation. A hash-checked patch to pinned
NimBLE 1.4.3 checks host-task creation, bounds synchronization waiting to five seconds and
stops safely on initialization errors without erasing NVS. The watchdog remains enabled.
The previous releases remain suspended. **Real-device boot of this version is unverified.**

Use the board-specific ZIP or matching `-update.bin`, private backup and preflight, then
**Update** in the official Meshtastic Web Flasher. Do not select erase/factory installation.
Software checks and injected failures are not hardware acceptance. No merge to main or
stable promotion is performed by this test release.
