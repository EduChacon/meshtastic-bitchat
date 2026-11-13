# Meshtastic ↔ Bitchat 0.1.0-experimental.5

**Correção de memória; pré-release sem aceite físico.**

A `.4` pode mostrar `Low memory: bridge OFF` antes de criar o canal. Esta revisão reduz
alocações iniciais e evita exigir dois grandes blocos contínuos. No modelo host, a reserva
inicial cai de aproximadamente 67 KB para 23 KB. A margem de segurança permanece intacta.
Isso é uma correção testada em software, ainda sem confirmação no aparelho afetado.
[Diagnóstico, testes e limites](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/MEMORY_FIX.md).

## Baixar para instalar

Nos anexos abaixo, escolher o ZIP do modelo exato:

- **Heltec V3:** `meshtastic-bitchat-heltec-v3-0.1.0-experimental.5-<commit>.zip`.
- **Cardputer ADV + Cap LoRa-1262:** `meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.5-<commit>.zip`.

Fazer backup privado e preflight pelo [roteiro Windows desta versão](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/WINDOWS_RECOVERY.md).
No [flasher oficial](https://flasher.meshtastic.org/), selecionar o modelo, carregar o ZIP e
usar **Atualizar / Update**. Não usar **Apagar Flash e Instalar**. Não é necessário merge.
O ZIP contém só a aplicação; o `-update.bin` do mesmo modelo é a alternativa. Não usar em
Heltec V2/V4, T1000-E, T-Deck ou Cardputer original. Conferir os hashes em `SHA256SUMS`.

## Usar

Após o primeiro boot, conectar Meshtastic para fornecer a hora. Sem hora confiável, o rádio
anuncia Meshtastic automaticamente; com hora válida, volta a anunciar Bitchat. Desde a `.4`, BLE e texto
dedicado são ativados uma vez; a `.5` preserva essa escolha e não repete a migração. Um canal de
texto já autorizado é preservado; caso contrário, cria-se **Bitchat**, secundário e público,
somente em posição vazia. Nenhum canal existente é escolhido por nome ou sobrescrito.

**A chave desse canal é pública; seu texto será publicado no `#mesh` local.**
[Ler o início rápido, chave e limites antes de instalar](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/DEFAULTS.md).
Adicionar somente esse canal no rádio remoto stock, mantendo suas demais configurações.
O canal primário continua excluído. Escolhas posteriores de OFF/BLE-only persistem.

Para mudar o padrão, abrir **System → Bitchat bridge**:

1. **Local Bitchat:** somente relé Bluetooth, sem tradução/túnel LoRa da ponte.
2. **Bitchat + LoRa:** selecionar canal secundário e confirmar que seu texto será público
   no Bitchat local. Aguardar 30 segundos antes do primeiro teste.

No Meshtastic remoto stock, enviar texto normal **no canal selecionado**. No Bitchat público,
enviar `!lora texto` para o sentido contrário. Manter Bitchat aberto perto da ponte e conferir
as conexões BLE. Contadores não comprovam entrega no iPhone.

**More options** reúne perfil fixo/econômico, alternância de descoberta entre os apps, estado,
ajuda e túnel de laboratório. O perfil atual é explícito. Os modos básicos desativam o túnel
privado; ele exige ativação separada. O modo local desliga B/C sem apagar os canais Meshtastic.

[Detalhes e teste do menu](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/MENU_FIX.md).
**Heltec sem tela:** a ativação padrão não exige menu, mas o pareamento e a hora fornecida
por Meshtastic ainda são necessários. Não há configurador USB próprio. A placa sem OLED
continua sem aceite físico. Após corte de energia, pode ser necessário fornecer a hora novamente.

## Verificação e arquivos técnicos

A publicação depende dos dois builds, reconstrução idêntica, testes de protocolo/entradas
inválidas, BLE, memória limitada, inicialização, menu, configuração padrão, artefatos e análise estática, mais compilação stock Heltec.
A suíte do menu usa código real com fakes de tela/transporte/armazenamento e ASan/UBSan;
não equivale a teste OLED/iOS. A `.5` ainda exige boot, preservação, entrega, estabilidade e
rollback físicos. Não há garantia de risco zero.

`technical-package-0.1.0-experimental.5.tar.gz` reúne fontes correspondentes, dependências,
licenças, símbolos, manifestos e resultados dos testes. Não é arquivo para o flasher.
`prepare_flash.py`, `verify_artifact.py` e `WINDOWS_RECOVERY.md` apoiam backup/preflight.
A base permanece Meshtastic 2.7.26.54e0d8d, com as correções de boot da `.3`.

## English

**Memory allocation fix; no hardware acceptance for .5.** Lazy BLE buffers, shared broadcast
payloads and smaller runtime blocks reduce startup memory in the host model from about
67 KB to 23 KB. The safety reserve and existing settings are retained. The exact heap
condition of the affected physical device is unknown; a new hardware test is required.

Choose the board-specific `.5` ZIP above, perform private backup/preflight, and use **Update**
in the official flasher. Do not erase flash. No merge is required. Set the clock through
Meshtastic. Local BLE plus dedicated public text are enabled once since .4; .5 preserves that migration state; a new **Bitchat** secondary channel is created only in an empty slot when no text
channel was already authorized. Its key is public and its text is exported to local **#mesh**.
Add only that channel to the remote stock radio. Later OFF/BLE-only choices persist.
See the [English quick start](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/DEFAULTS.md#english).
Wait 30 seconds; send ordinary text on that Meshtastic
channel, or `!lora text` in public Bitchat for the opposite direction. Both basic choices
disable lab tunneling. Power, app discovery, status and lab settings are under **More options**.

Software checks do not prove phone delivery. Automatic setup does not require an OLED,
but native pairing and clock setup still do; screenless hardware is not physically validated.
The technical archive contains corresponding source, dependency licenses, debug symbols
and test evidence. It is not flasher input. The base and startup safeguards remain unchanged.
