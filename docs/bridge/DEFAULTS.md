# Integração padrão — experimental .5

> A árvore contém uma [revisão candidata](CANDIDATE.md) posterior à `.5`. Os trechos marcados
> com **Candidata** descrevem o código atual; os demais valem para as duas.

## O que a instalação ativa

Desde a revisão `.4`, o firmware ativa o relé Bitchat por BLE e a tradução de texto no primeiro boot,
inclusive na primeira atualização a partir das revisões anteriores. A mudança é aplicada
uma vez e registrada: desligar a ponte ou selecionar somente BLE depois disso continua
valendo após reiniciar. Um canal de texto já escolhido e autorizado é preservado.

Se não houver canal escolhido, o firmware cria `Bitchat` como **canal secundário público**,
somente em uma posição desativada e vazia. Usa a API nativa de canais e persiste pelo NodeDB.
Não altera região, preset, slot RF, potência, canal primário ou chaves existentes. MQTT e
compartilhamento de posição ficam desativados nesse canal novo. Não formata armazenamento.

Chave pública de interoperabilidade, em Base64:

```text
xes1ntLxyD2pakZvNlqF1/6OzATHxMeQqndOjKBWZFs=
```

É SHA-256 da identificação pública `meshtastic-bitchat/public-text-channel/v1`. **Não é
segredo e não oferece privacidade.** O conteúdo desse canal é destinado a publicação no
`#mesh` local do Bitchat. Nome igual sozinho não basta: um canal preexistente com outra
chave, papel ou MQTT não é escolhido nem sobrescrito. O firmware pode retomar a criação
interrompida apenas se nome exato, chave pública completa e política do canal coincidirem.

Sem espaço vazio, com nome conflitante, modo radioamador licenciado ou erro de configuração,
a criação automática não prossegue. BLE local pode continuar; conferir `Status` e escolher
um canal elegível pelo menu, com consentimento. Falha de escrita, identidade ou falta de
memória bloqueia a ponte. Bluetooth desativado ou economia stock não são reconfigurados
automaticamente: é preciso corrigir essas opções pelo aplicativo Meshtastic.

A `.5` reduz a memória inicial da ponte sem repetir a migração nem alterar as escolhas
persistidas. O aviso `Low memory: bridge OFF` é descrito em [MEMORY_FIX.md](MEMORY_FIX.md).

## Primeiro uso

1. Após backup, preflight e atualização, manter alimentação USB estável e conferir o boot.
2. Abrir **Meshtastic**, parear com a ponte e concluir a conexão para fornecer a hora. Isso
   não exige internet. Sem hora válida, o rádio anuncia o serviço Meshtastic automaticamente.
3. Conferir o canal secundário `Bitchat`. No rádio stock remoto, adicionar **somente esse
   canal**, com o mesmo nome e a chave pública acima, ou compartilhá-lo pela função nativa
   do aplicativo. Não importar uma configuração que substitua todos os canais sem revisar.
   Os dois rádios ainda precisam ter a mesma região/preset/frequência compatíveis.
4. Abrir **Bitchat → `#mesh`** perto da ponte. Com hora válida, o anúncio muda para Bitchat
   automaticamente, sem seleção de modo. Aguardar a janela inicial de 30 segundos.
5. Enviar `Teste 1` pelo canal `Bitchat` do Meshtastic remoto: deve aparecer em `#mesh` com
   atribuição `[MT !…]`. Enviar `!lora Teste 2` em `#mesh`: deve aparecer nesse canal remoto.

Na `.5`, texto sem `!lora ` não é exportado para LoRa. **Candidata:** todo texto público do
`#mesh` é exportado como `apelido: texto`, o prefixo é opcional e pode voltar a ser exigido em
`Settings → LoRa: only !lora`; a atribuição de entrada usa o nome do nó (`[MT nome] …`).
DMs, posição, credenciais, telemetria, arquivos e voz não são traduzidos. O túnel C continua de laboratório e exige ativação separada; não
é necessário para texto entre um iPhone Bitchat e um rádio stock com Meshtastic.

Na tela: `System → Bitchat bridge → More options → Status` mostra estado, clientes BLE e
canal de texto. `BLE clients: 0` não confirma conexão Bitchat. `Open Meshtastic: clock`
significa que falta hora confiável; a ponte não aprende a hora de mensagens BLE arbitrárias.
**Candidata:** `Status` fica no primeiro nível do menu; os textos equivalentes são
`Phones connected: 0` e `Clock not set`, com a orientação `Open Bitchat nearby`. Sem hora
confiável, a candidata aceita a hora do primeiro telefone Bitchat que conecta, com os limites
descritos em [CANDIDATE.md](CANDIDATE.md); as frases desta página que negam esse recurso
valem para a `.5`.
Se o Meshtastic não aparecer na lista após a hora estar ajustada, usar **Switch phone app →
Find Meshtastic: 5m**. Existência dos dois serviços GATT não demonstra uso simultâneo em iOS.

## Heltec V3 sem tela

O mesmo alvo `heltec-v3-bitchat` executa a configuração automática sem depender do OLED.
O pareamento nativo ainda precisa funcionar: usar um vínculo já existente ou o procedimento
Meshtastic para PIN fixo privado. Esta revisão não remove autenticação nem troca o PIN.
A [documentação oficial de Bluetooth](https://meshtastic.org/docs/configuration/radio/bluetooth/)
explica os modos de PIN e o caso de uma tela removida após a configuração.
Sem vínculo e sem acesso ao PIN, configurar o pareamento pela interface USB oficial antes
da implantação. Em ESP32, Wi-Fi habilitado impede BLE nesta base: se necessário, ajustar
essa configuração pela interface Meshtastic oficial. O padrão da ponte não desliga uma
configuração Wi-Fi existente automaticamente. O computador não é necessário na operação
normal após a configuração.

O ESP32-S3 pode perder a hora ao cortar totalmente a energia. Sem RTC/GPS confiável, abrir
Meshtastic e conectar novamente para restaurá-la; depois abrir Bitchat. Portanto não há
promessa de recuperação de mensagens sem intervenção após toda falta de energia. Não se
desativou a proteção de frescor para esconder essa limitação.

Sem tela, há ativação automática dos padrões, mas não um configurador completo de preferências
da ponte no app stock. Desligar BLE pelo app interrompe também a ponte. Modos personalizados,
diagnóstico local e alternância manual continuam exigindo a interface disponível no aparelho.

## Testes e limites

`python3 tools/bridge/test_defaults.py` compila os métodos de produção de inicialização,
provisionamento, persistência e janela de hora com canais/NVS/relógio simulados, sob ASan/UBSan.
Cobre instalação nova, atualização legada, OFF persistente, BLE-only persistente, retomada
após criação interrompida, canal primário/privado intacto, falta de espaço, conflito de chave,
MQTT, memória/identidade/BLE indisponíveis, armazenamento inválido e erro de gravação.
É teste host, não ensaio de flash, falta de energia, Bluetooth ou rádio real.

## English

This revision enables local BLE and dedicated public text once on first boot, including an
upgrade. Previously authorized text settings are retained. Later OFF/BLE-only choices survive
reboot. Without a selected channel it creates secondary **Bitchat** in an empty disabled slot,
using the public Base64 key shown above. That key is intentionally public: channel text is
exported into local Bitchat `#mesh`. Existing channels, primary, RF settings and keys are not
overwritten. Conflicts/full slots leave text unconfigured; storage failures stop the bridge.

Connect the Meshtastic app first to supply trusted time, without internet. Without time the
radio advertises Meshtastic automatically; once time is set it advertises Bitchat. Add only
the same named channel/key to the remote stock radio and keep compatible RF settings.
Wait 30 seconds, keep Bitchat open nearby, send ordinary text on the Meshtastic **Bitchat**
channel and `!lora text` in Bitchat **#mesh** for the reverse direction. Other text stays local.
DMs/credentials/location/media are not translated. Laboratory tunneling still needs separate
activation. Choose a different text channel only after consenting to its public export.

Automatic setup does not require an OLED, but native Meshtastic pairing still needs a known
PIN or existing bond. Pairing security is not weakened. Use the official USB configuration
method before deployment if the PIN cannot be read. A complete power loss may require
reconnecting Meshtastic to set time again. No peer-clock fallback is used. Custom bridge
preferences still need a local UI; there is no custom USB/app configurator in this revision.
These changes are host-tested and compiled, not physically accepted on screenless hardware.
