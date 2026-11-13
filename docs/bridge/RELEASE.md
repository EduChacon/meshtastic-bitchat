# Meshtastic ↔ Bitchat 0.1.0-experimental.3

**Pré-release para teste. Boot e funcionamento em hardware ainda não validados.**
As versões anteriores foram retiradas dos downloads. Os binários desta `.3` permanecem
inalterados: fonte `d15a403fe9bb50b28a4c025330008ce11636da9b`.

## Baixar para instalar

| Aparelho                      | Download                                                                                                                                                                                    |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Heltec V3**                 | [**ZIP para Heltec V3**](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/meshtastic-bitchat-heltec-v3-0.1.0-experimental.3-d15a403.zip)             |
| Cardputer ADV + Cap LoRa-1262 | [ZIP para Cardputer ADV](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.3-d15a403.zip) |

Faça o backup privado e o preflight pelo [roteiro Windows](https://github.com/EduChacon/meshtastic-bitchat/blob/main/docs/bridge/WINDOWS_RECOVERY.md).
Depois, no [Web Flasher oficial](https://flasher.meshtastic.org/), use **Atualizar / Update**.
**Não use Apagar Flash e Instalar.** A ponte vem OFF por padrão. Não é preciso fazer merge.

O ZIP contém somente a aplicação específica da placa. Há um `-update.bin` equivalente
para cada alvo, caso o upload ZIP não seja aceito. Não existe binário genérico ESP32.
Nenhum desses arquivos serve para T1000-E, T-Deck, Cardputer original ou Heltec V2/V4.

## Arquivos de apoio

- [prepare_flash.py](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/prepare_flash.py) e [verify_artifact.py](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/verify_artifact.py):
  backup/preflight; não gravam nem apagam flash. O primeiro usa esptool oficial e requer
  autorização explícita de leitura e reinício temporário em modo ROM.
- [WINDOWS_RECOVERY.md](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/WINDOWS_RECOVERY.md): procedimento para Windows/COM3.
- [SHA256SUMS](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/SHA256SUMS): integridade dos downloads atuais.
- [Pacote técnico](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/technical-package-0.1.0-experimental.3.tar.gz): fontes, dependências, licenças, símbolos, manifestos,
  testes e todos os anexos originais da `.3`. **Não enviar esse pacote ao flasher.**

## Correções e verificação

A versão adia os buffers grandes até a ativação da ponte; limita a espera de sincronização
BLE; trata falhas de inicialização sem repetição sobre estado parcial nem apagamento de
NVS nessa etapa; e mantém o estado das conexões protegido entre tarefas.
O watchdog continua ativo. Base oficial Meshtastic 2.7.26.54e0d8d.

A [CI](https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43482990039) passou para os dois alvos:
compilação, recompilação idêntica, protocolo, sanitizers, entradas inválidas, adaptador BLE,
falhas de inicialização, preflight e análise estática; o Heltec stock também compilou.
Esses resultados não substituem teste físico nem garantem risco zero.
[Investigação, hashes e lacunas](https://github.com/EduChacon/meshtastic-bitchat/blob/main/docs/bridge/BOOT_INCIDENT.md).

## English

**Experimental testing release; real-device boot and operation remain unverified.**
Older prereleases and their installable downloads have been removed. The `.3` firmware
bytes are unchanged. Use the board-specific ZIP above, private backup and preflight, then
**Update** in the official Meshtastic Web Flasher. Do not erase flash or install another
board's image. No merge is needed to use these assets.

The technical package consolidates all original `.3` assets, including corresponding
source, dependencies, licenses, debug symbols, manifests and test evidence. It is not
flasher input. Source history and historical tags are retained. Software CI passed;
hardware acceptance remains pending.
