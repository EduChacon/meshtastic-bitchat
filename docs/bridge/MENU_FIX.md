# Revisão do menu — experimental .4

## Defeito reproduzido

Na `.3`, a seleção de canal chamava `showConfirmationBanner` diretamente dentro do callback
da seleção anterior. `Screen::showOverlayBanner` substitui o banner imediatamente e atualiza
a UI, enquanto `NotificationRenderer::drawAlertBannerOverlay` ainda processa a seleção e,
ao retornar, chama `resetBanner`. O aviso novo pode consumir o evento anterior e é apagado
em seguida. Avisos de estado/erro sofriam da mesma reentrada. A existência de um canal na
lista não demonstrava que a autorização para compartilhar havia sido concluída.

O controle host executa o menu original da `.3` com o ciclo callback → reset do renderizador
e reproduz a perda da confirmação. Ele não reproduz um iPhone nem o hardware OLED. A cópia
de controle coincide com o fonte publicado `d15a403`, arquivo `src/bridge/BridgeMenu.cpp`,
SHA-256 `bb4cd212406e06f4beedaf137f969f468f28767d15929adca4dcaafb8e1f755b`.

## Correção

Toda navegação da ponte, inclusive confirmação, ajuda, sucesso e erro, usa a fila de menus
existente. O próximo banner só é apresentado depois de encerrada a seleção anterior.
O renderizador compartilhado stock não foi alterado. Cancelar um aviso não ativa um modo.

As duas escolhas de operação são:

- **1. Local Bitchat:** ativa o relé Bluetooth e desativa tradução/túnel LoRa da ponte.
- **2. Bitchat + LoRa:** pede um canal secundário elegível e consentimento para texto público;
  só depois ativa o relé e a tradução. Desativa o túnel e a opção privada de laboratório.

Ambas preservam as configurações stock, região, chaves, identidade da ponte e perfil de energia.
O túnel fica em **More options → Lab tunnel**, com autorização separada. A ponte agora ativa
BLE e texto público dedicado no primeiro uso: ver [padrões e migração](DEFAULTS.md).
As escolhas básicas substituem os modos da ponte previamente configurados;
quem precisa combinar B/C deve habilitar o laboratório depois da escolha básica.

**More options → Power profile** mostra o perfil atual e oferece `Fixed (USB)` e
`Economy (battery)`. **Switch phone app** permite anunciar Meshtastic por cinco minutos
ou voltar ao anúncio Bitchat imediatamente, sem reiniciar o dispositivo.
**More options → Status** mostra o número de clientes BLE inscritos e o canal de texto,
ou `LoRa text: OFF`. Não é necessário procurar a página de contadores para essa conferência.

## Primeiro teste após atualização

1. Abrir o app Meshtastic para fornecer a hora; manter o teste fixo por USB. Sem hora, a
   descoberta Meshtastic é automática; ao receber hora válida, o anúncio volta a Bitchat.
2. Para o padrão, usar o canal público `Bitchat` criado automaticamente, com a mesma chave
   no rádio stock remoto. Seguir [DEFAULTS](DEFAULTS.md); não é necessário ativar pelo menu.
3. Para usar outro canal, abrir `System → Bitchat bridge → 2. Bitchat + LoRa` e escolher
   o canal secundário configurado nos dois rádios. Ler o aviso:
   **o texto desse canal ficará público no Bitchat local**. Confirmar somente para a rede
   de teste autorizada. O canal primário continua excluído.
4. Aguardar 30 segundos, deixar o Bitchat aberto perto da ponte e conferir `BLE` no painel.
5. No app Meshtastic do rádio remoto stock, enviar `Teste 1` **nesse canal**, sem prefixo.
6. No chat público local Bitchat, enviar `!lora Teste 2` e conferir no Meshtastic remoto.

Se não houver entrega, registrar estado, BLE/P/TX/RX/D, sentido do teste e versões dos apps,
sem chaves ou mensagens pessoais. `BLE0` não demonstra conexão Bitchat. Contadores LoRa não
demonstram entrega ao telefone. Dois iPhones com ponte desligada são um controle necessário
para distinguir comunicação direta de retransmissão.

## Verificação e limites

`python3 tools/bridge/test_menu.py` executa callbacks reais e o método real de seleção do modo
com fakes de tela, armazenamento e transporte, sob ASan/UBSan. Cobre seleção sem consentimento,
cancelamento, confirmação, isolamento dos dois modos, perfil, alternância de anúncio, opção
privada, canal inválido, falta de memória/identidade/BLE e falha de persistência. O controle
negativo executa o menu anterior, que perde o aviso. Não há acesso a rádio nesse teste.

O teste host passou localmente. Compilação por placa e as regressões da CI devem passar antes
da publicação; os logs da revisão publicada são a evidência correspondente. **Boot da `.4`,
legibilidade física e entrega A/B/C ainda precisam de bancada.** A correção do menu não prova
que seja a única causa de ausência de entrega relatada.

O padrão automático não depende da tela. Pareamento/hora e limitações da implantação
sem OLED estão em [DEFAULTS](DEFAULTS.md#heltec-v3-sem-tela). Não há configurador USB próprio.

## English

The `.3` channel picker opened a confirmation inside the previous selection callback; the
shared renderer then cleared it. The host control reproduces this lost prompt. This revision
queues every bridge menu transition and feedback screen, preserving explicit consent.

Choose **1. Local Bitchat** for BLE-only bridge operation, or **2. Bitchat + LoRa**, select a
secondary channel and confirm public export. Both basic choices disable laboratory tunneling;
local mode also disables text translation. Local BLE plus dedicated public text now default
to ON on first boot; see [defaults and migration](DEFAULTS.md#english). Power, app
discovery and lab settings are under **More options**. Fixed/economy now show current state.

Set the clock through Meshtastic first. After enabling shared text, wait 30 seconds and keep
Bitchat open nearby. Send ordinary text on the selected channel from the remote stock radio;
send `!lora text` in public Bitchat for the opposite direction. PSK channel text becomes public
on local Bitchat. Host tests and builds do not establish phone delivery or physical stability.
Automatic defaults work without menu input; native pairing and valid time remain necessary.
Screenless hardware and actual phone delivery still require physical acceptance.
