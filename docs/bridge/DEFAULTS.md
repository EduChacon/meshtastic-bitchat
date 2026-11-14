# Início rápido e padrões — versão 1.0 beta

## O que a instalação ativa

No primeiro boot, inclusive na primeira atualização a partir de uma revisão anterior, o
firmware ativa o relé Bitchat por Bluetooth e a troca de texto com um canal LoRa. A mudança é
aplicada uma vez e registrada: desligar a ponte ou escolher somente Bluetooth depois disso
continua valendo após reiniciar. Um canal de texto já escolhido e autorizado é preservado.

Se não houver canal escolhido, o firmware cria `Bitchat` como **canal secundário público**,
somente em uma posição desativada e vazia. Usa a API nativa de canais e persiste pelo NodeDB.
Não altera região, preset, slot RF, potência, canal primário ou chaves existentes. MQTT e
compartilhamento de posição ficam desativados nesse canal novo. Não formata armazenamento.

Chave pública de interoperabilidade, em Base64:

```text
xes1ntLxyD2pakZvNlqF1/6OzATHxMeQqndOjKBWZFs=
```

É SHA-256 da identificação pública `meshtastic-bitchat/public-text-channel/v1`. **Não é
segredo e não oferece privacidade.** O texto desse canal é publicado no `#mesh` local do
Bitchat, e **todo texto público do `#mesh` ao alcance da ponte é publicado nesse canal**,
mesmo de quem não sabe que a ponte existe. Nome igual sozinho não basta: um canal
preexistente com outra chave, papel ou MQTT não é escolhido nem sobrescrito.

Sem espaço vazio, com nome conflitante, modo radioamador licenciado ou erro de configuração,
a criação automática não prossegue: o Bluetooth local continua e a tela orienta a escolher um
canal pelo menu, com consentimento. Falha de escrita, de identidade ou falta de memória
bloqueia a ponte. Bluetooth desativado ou economia de energia stock não são reconfigurados
automaticamente: corrigir essas opções pelo aplicativo Meshtastic.

## Primeiro uso

1. Após backup, conferência e atualização, manter alimentação USB estável e conferir o boot.
2. Abrir o **Bitchat** perto do rádio, no `#mesh`. Se o rádio estiver sem hora, o primeiro
   telefone Bitchat que conecta fornece a hora. O aplicativo Meshtastic, o GPS e o RTC também
   fornecem e têm precedência. Enquanto falta hora, o rádio alterna o anúncio Bluetooth a
   cada 20 s entre os dois aplicativos.
3. No rádio remoto com firmware original, adicionar **somente o canal `Bitchat`**. O caminho
   mais curto é abrir [este link](https://meshtastic.org/e/?add=true#CisSIMXrNZ7S8cg9qWpGbzZahdf-jswEx8THkKp3ToygVmRbGgdCaXRjaGF0)
   no telefone com o aplicativo Meshtastic e confirmar a **adição** do canal; também serve
   digitar o nome e a chave pública acima, ou compartilhar o canal pela função do aplicativo.
   O link foi montado a partir do formato de canais do Meshtastic e não foi ensaiado: se o
   aplicativo propuser **substituir** os canais em vez de adicionar, cancelar e digitar a chave. Os dois rádios
   precisam de região, preset e frequência compatíveis.
4. Aguardar os 30 segundos iniciais (`Starting LoRa: 30 s`) e conversar:
   - texto no canal `Bitchat` do Meshtastic aparece no `#mesh` como `[MT nome do nó] texto`;
   - texto no `#mesh` aparece no canal como `apelido: texto`.

Texto com mais de 200 bytes é dividido em até três partes. DMs, grupos privados, posição,
credenciais, telemetria, arquivos e voz não são traduzidos. O túnel de laboratório exige
ativação separada e não é necessário para a conversa entre Bitchat e Meshtastic.

## Canal comum e canal próprio

A chave padrão é a mesma em todo rádio com este firmware. Com poucas pontes, isso só facilita.
Com muitas na mesma região, o canal `Bitchat` vira uma sala pública comum: todas as pontes ao
alcance do LoRa trocam texto entre si, sem privacidade. Laços e duplicação entre pontes são
contidos, e há limite por pessoa e de tempo de transmissão, mas a conversa de um grupo se
mistura à dos outros. Para um grupo fechado:

1. no aplicativo Meshtastic, criar um canal secundário com nome e chave próprios, com MQTT
   desligado, no rádio da ponte e nos rádios remotos;
2. no rádio da ponte, `System → Bitchat bridge → Bitchat + LoRa`, escolher esse canal e
   confirmar. A ponte passa a usar somente ele.

Em local público, `Settings → LoRa: only !lora` limita o que sai pelo LoRa ao texto que a
pessoa marcou de propósito.

## Menu e tela

`System → Bitchat bridge` oferece `Bitchat only`, `Bitchat + LoRa`, `Turn bridge off`,
`Status` e `Settings`. Em `Settings`:

- `LoRa: all messages` / `LoRa: only !lora`: no segundo modo, só o texto iniciado por
  `!lora ` sai pelo LoRa. O prefixo é aceito e removido nos dois modos.
- `Battery saver: off / on`: menos transmissões LoRa e anúncio Bluetooth mais espaçado.
- `Find Meshtastic app`: anuncia o serviço Meshtastic por 5 minutos, para parear o aplicativo.
- `Advanced: tunnel`: laboratório, com confirmação própria.

A tela de estado mostra uma frase (`Ready: Bitchat + LoRa`, `Clock not set`, `Standby: other
bridge`, `LoRa channel changed`) e, na última linha, o que fazer. `Phones` é o número de
telefones inscritos; `Not delivered` conta somente mensagens perdidas.

## Rádio sem tela

Os padrões automáticos não dependem de tela. O pareamento do aplicativo Meshtastic continua
exigindo o PIN ou um vínculo existente; esta versão não remove autenticação nem troca o PIN.
A [documentação oficial de Bluetooth](https://meshtastic.org/docs/configuration/radio/bluetooth/)
explica os modos de PIN. Em ESP32, Wi-Fi habilitado impede o Bluetooth nesta base. Sem tela
não há como mudar o modo da ponte: desligar o Bluetooth pelo aplicativo interrompe também a
ponte. Nenhum aparelho sem tela foi ensaiado.

## Testes e limites

`python3 tools/bridge/test_defaults.py` e `python3 tools/bridge/test_flow.py` executam os
métodos de produção de inicialização, criação do canal, persistência, hora e conversa com
canais, armazenamento, relógio, telefones e rádio simulados. São testes de host, não ensaio de
flash, falta de energia, Bluetooth ou rádio real. O que mudou desde a `experimental.5`, com os
motivos e os custos assumidos, está em [CHANGES.md](CHANGES.md).

## English

On first boot, including an upgrade, the firmware enables the Bluetooth relay and text exchange
with a LoRa channel, once. Later OFF or Bluetooth-only choices survive a reboot, and a
previously authorized text channel is kept. Otherwise it creates the secondary **Bitchat**
channel in an empty disabled slot with the public Base64 key shown above. That key is public
on purpose: channel text is published in local Bitchat `#mesh`, and **every public `#mesh`
message within reach of the bridge is published on that channel**. Existing channels, the
primary, RF settings and keys are never overwritten.

Open Bitchat near the radio. A radio without a clock takes the time from the first Bitchat
phone that connects; the Meshtastic app, GPS and RTC also supply it and take precedence. Add
only the same named channel and key to the remote stock radio, wait for the initial 30 seconds
and chat: channel text appears in `#mesh` as `[MT node name] text`, `#mesh` text appears on
the channel as `nick: text`. Text above 200 bytes is split in up to three parts. Under
`System → Bitchat bridge → Settings`, `LoRa: only !lora` restricts export to text typed with
that prefix. DMs, private groups, location and media are not translated. Host-tested and
compiled only; see [CHANGES.md](CHANGES.md#english) for what changed and the accepted costs.
