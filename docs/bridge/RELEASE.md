# Meshtastic ↔ Bitchat 0.1.0 experimental

Código e atualizações de aplicativo para **Heltec V3** e **M5Stack Cardputer ADV + Cap
LoRa-1262**, baseados no Meshtastic 2.7.26.54e0d8d. Ponte desligada por padrão.

Inclui relé BLE limitado, tradução opt-in de texto público `!lora ` para canal secundário
nativo e túnel experimental separado `!bctun `. Os apps oficiais permanecem sem modificação.
T1000-E, T-Deck e outros rádios são candidatos a nós stock remotos; esta release **não inclui
firmware de ponte para eles**.

**Não houve ensaio em hardware. Não é uma release pronta para uso cotidiano.** A CI valida
compilação, testes host com sanitizers, entradas malformadas, pacotes, análise estática e
recompilação idêntica no mesmo ambiente. Os logs estão em `test-evidence.tar.gz`.

Os ZIPs contêm somente aplicação de atualização, manifesto, checksums, licença e aviso.
Não contêm bootloader, partições, filesystem ou apagador. Os arquivos `-update.bin` são
específicos de cada placa. **Não usar como imagem factory nem em outro modelo.**

Ler `INSTALL.md` e executar `verify_artifact.py` antes de abrir o flasher oficial. O modo
`--package-only` confere o arquivo e o alvo declarado; não identifica o rádio nem autoriza
gravação. Uma futura instalação exige identificação física, revisão/banda, backup privado,
layout/slot compatíveis, plano de rollback e autorização separada. Nenhuma gravação oferece
garantia de risco zero de impedir boot ou de perda de dados.

Limites principais: iOS/Android reais, coexistência BLE, RF, energia e rollback ainda não
testados; hora confiável necessária após boot; sincronização histórica/RSR recusada; apenas
um exportador B por malha local/canal; DMs pelo túnel dependem de tempo/capacidade e estão em
modo de laboratório; voz contínua e arquivos grandes não são suportados por LoRa.

O código correspondente está no commit indicado nos manifestos e em `project-source.tar.gz`.
As bibliotecas resolvidas e seus avisos estão nos arquivos `dependencies-*.tar.gz`, com
inventários de conteúdo `dependencies-*.json`. Toolchain/SDK são identificados nesses
inventários e nos logs. `SHA256SUMS` permite verificar todos os anexos. Os arquivos mantêm a
licença GPL-3.0 do firmware e as licenças próprias das dependências. O repositório mantém sua
visibilidade existente; esta publicação não altera permissões de acesso.
