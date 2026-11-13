# Comparação de implementações — 2026-11-13

Esta revisão compara fontes e relatos de três projetos indicados, sem executar seus binários
nem importar sua implementação. Código existente e relato de outro aparelho não constituem
aceite da ponte Heltec/iPhone. O cliente de referência continua Bitchat iOS 1.8.0, commit
`30c58eee349e172b37f69faa2113c9d5f2c76c23`; a versão instalada no telefone de teste precisa ser registrada.

## evansmj/firmware-bitchat-plugin

A [PR 17](https://github.com/evansmj/firmware-bitchat-plugin/pull/17), aberta e não incorporada
no momento da consulta, trata de descoberta, identidade, timestamp, filas e tamanho das
notificações. Foi examinada a revisão `23ab0ff4e66a45c1763d3dc038cd17f4ba23c597`, incluindo
[NimbleBluetooth.cpp](https://github.com/evansmj/firmware-bitchat-plugin/blob/23ab0ff4e66a45c1763d3dc038cd17f4ba23c597/src/nimble/NimbleBluetooth.cpp)
e [BitChatBridgeModule.cpp](https://github.com/evansmj/firmware-bitchat-plugin/blob/23ab0ff4e66a45c1763d3dc038cd17f4ba23c597/src/modules/BitChatBridgeModule.cpp).

Essa revisão alterna o UUID principal e deriva PeerID da chave Noise. Nossa ponte já anuncia
um UUID de 128 bits por vez e deriva o mesmo tipo de PeerID; mantém identidade própria
persistente, sem reutilizar a chave Meshtastic. A janela nativa é explícita, com seleção
automática adicional quando falta hora. Não foi adotado o fallback de hora aprendida de peers.

A [issue 4](https://github.com/evansmj/firmware-bitchat-plugin/issues/4) relata descoberta Bitchat
apenas após desconectar Meshtastic em RAK4631/Android/2.7.10. É evidência contra presumir
simultaneidade. Não demonstra o mesmo defeito em Heltec/iOS. Nosso adaptador tenta retomar
anúncios ao conectar/desconectar; o resultado real com os dois apps ainda exige bancada.

## GigaProjects/meshtastic-bitchat-bridge

É uma [ponte Python em Linux](https://github.com/GigaProjects/meshtastic-bitchat-bridge/tree/917304f56e714fba0678a2c606de6041261b1aeb),
com BLE central e interface Meshtastic externa, não firmware autônomo. O
[script nessa revisão](https://github.com/GigaProjects/meshtastic-bitchat-bridge/blob/917304f56e714fba0678a2c606de6041261b1aeb/mesh-bit.py)
calcula `my_id` a partir da chave pública Ed25519. A política do
[cliente iOS de referência](https://github.com/permissionlesstech/bitchat/blob/30c58eee349e172b37f69faa2113c9d5f2c76c23/bitchat/Services/BLE/BLEAnnounceHandlingPolicy.swift)
deriva o ID da chave Noise anunciada e rejeita divergência. Portanto copiar esse cálculo
não resolveria compatibilidade com o cliente consultado. A conclusão é de análise de fonte,
não ensaio desse projeto no telefone. Mudança de versão pode refutá-la e exige nova comparação.

## frindle/meshtastic-bitchat-bridge

O [README consultado](https://github.com/frindle/meshtastic-bitchat-bridge/tree/b49c022ecc6a82b0e01678c98257db1ab99404c5)
descreve ponte Linux de tráfego opaco privado/grupos por porta 256 e exclui retransmissão
de broadcast público. Não substitui o modo B de texto nativo visível no app Meshtastic stock.
As notas de protocolo são auxiliares; o codec/frescor/identidade devem ser conferidos no
cliente oficial fixado. Relatos de entrega desse túnel não validam nossa tradução.

## Evidência própria e como refutar

- **Confirmado em host:** o menu `.3` perde sua confirmação no ciclo callback → reset;
  o controle negativo executa o fonte original. A revisão enfileira o próximo banner.
- **Conferido no fonte:** UUID release, identidade derivada de Noise, assinatura canônica,
  filas e enquadramento conforme MTU. O iOS usa `NotificationStreamAssembler` para remontar
  notificações: dividir bytes de um frame entre notificações é previsto nesse caminho.
- **Nova correção de operação:** sem hora, a ponte recusava mensagens e podia anunciar
  Bitchat sem explicar o caminho de ajuste. Agora anuncia Meshtastic até receber hora
  confiável. Isso não demonstra que falta de hora foi a causa no aparelho relatado.
- **Ainda desconhecido no aparelho:** ligação/subscrição BLE, aceitação da presença no
  cliente instalado, memória disponível em operação, entrega nos dois sentidos e efeito
  de manter Meshtastic conectado. Não se atribui todo o defeito ao menu.

O menor ensaio é um Heltec com a revisão nova e um nó stock, Bitchat em primeiro plano e
internet desligada. Conferir hora, canal, `BLE clients`, presença da ponte e RX/TX; enviar
texto público nos dois sentidos. Repetir com o app Meshtastic desconectado da ponte. Se
`BLE clients > 0` mas a presença não aparece, investigar rejeição de identidade/assinatura/
frescor com dados sintéticos antes de mudar LoRa. Se a presença aparece, comparar RX/TX e
canal. O controle com ponte desligada distingue relé de comunicação BLE direta.
