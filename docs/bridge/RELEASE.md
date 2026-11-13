# Meshtastic ↔ Bitchat 0.1.0-experimental.4

**Correção do menu e duas escolhas de operação. Pré-release para teste, sem aceite físico.**

A confirmação de canal na `.3` podia desaparecer ao selecionar o canal. O teste host reproduz
esse defeito com o código anterior; a `.4` apresenta o aviso depois de encerrar a seleção.
Cancelar não habilita a tradução. Esta correção não demonstra que toda ausência de entrega
no iPhone tenha a mesma causa.

## Baixar para instalar

Nos anexos abaixo, escolher o ZIP do modelo exato:

- **Heltec V3:** `meshtastic-bitchat-heltec-v3-0.1.0-experimental.4-<commit>.zip`.
- **Cardputer ADV + Cap LoRa-1262:** `meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.4-<commit>.zip`.

Fazer backup privado e preflight pelo [roteiro Windows desta versão](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/WINDOWS_RECOVERY.md).
No [flasher oficial](https://flasher.meshtastic.org/), selecionar o modelo, carregar o ZIP e
usar **Atualizar / Update**. Não usar **Apagar Flash e Instalar**. Não é necessário merge.
O ZIP contém só a aplicação; o `-update.bin` do mesmo modelo é a alternativa. Não usar em
Heltec V2/V4, T1000-E, T-Deck ou Cardputer original. Conferir os hashes em `SHA256SUMS`.

## Usar

Após o primeiro boot, conectar Meshtastic para fornecer a hora. Abrir **System → Bitchat bridge**:

1. **Local Bitchat:** somente relé Bluetooth, sem tradução/túnel LoRa da ponte.
2. **Bitchat + LoRa:** selecionar canal secundário e confirmar que seu texto será público
   no Bitchat local. Aguardar 30 segundos antes do primeiro teste.

No Meshtastic remoto stock, enviar texto normal **no canal selecionado**. No Bitchat público,
enviar `!lora texto` para o sentido contrário. Manter Bitchat aberto perto da ponte e conferir
as conexões BLE. Contadores não comprovam entrega no iPhone.

**More options** reúne perfil fixo/econômico, alternância de descoberta entre os apps, estado,
ajuda e túnel de laboratório. O perfil atual é explícito. Os modos básicos desativam o túnel
privado; ele exige ativação separada. A ponte continua OFF por padrão e preserva preferências
já salvas ao atualizar. O modo local desliga B/C sem apagar os canais Meshtastic.

[Detalhes e teste do menu](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/MENU_FIX.md).
**Heltec sem tela:** o alvo da placa pode ser o mesmo, mas a ativação da ponte ainda exige
menu local. Não há configurador USB da ponte nesta revisão.

## Verificação e arquivos técnicos

A publicação depende dos dois builds, reconstrução idêntica, testes de protocolo/entradas
inválidas, BLE, inicialização, menu, artefatos e análise estática, mais compilação stock Heltec.
A suíte do menu usa código real com fakes de tela/transporte/armazenamento e ASan/UBSan;
não equivale a teste OLED/iOS. A `.4` ainda exige boot, preservação, entrega, estabilidade e
rollback físicos. Não há garantia de risco zero.

`technical-package-0.1.0-experimental.4.tar.gz` reúne fontes correspondentes, dependências,
licenças, símbolos, manifestos e resultados dos testes. Não é arquivo para o flasher.
`prepare_flash.py`, `verify_artifact.py` e `WINDOWS_RECOVERY.md` apoiam backup/preflight.
A base permanece Meshtastic 2.7.26.54e0d8d, com as correções de boot da `.3`.

## English

**Experimental menu fix; no hardware acceptance for .4.** The old channel-selection callback
could erase its own confirmation. A host negative control reproduces that defect. The revised
menu queues transitions and requires explicit consent before sharing text.

Choose the board-specific `.4` ZIP above, perform private backup/preflight, and use **Update**
in the official flasher. Do not erase flash. No merge is required. Set the clock through
Meshtastic, then choose **Local Bitchat** or **Bitchat + LoRa**. The latter requires a secondary
channel and public-export consent. Wait 30 seconds; send ordinary text on that Meshtastic
channel, or `!lora text` in public Bitchat for the opposite direction. Both basic choices
disable lab tunneling. Power, app discovery, status and lab settings are under **More options**.

Software checks do not prove phone delivery. Headless activation is still unsupported.
The technical archive contains corresponding source, dependency licenses, debug symbols
and test evidence. It is not flasher input. The base and startup safeguards remain unchanged.
