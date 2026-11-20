# Pagamentos Cashu pela ponte (experimental, a partir da `1.0.0-beta.16`)

Um token Cashu (dinheiro eletrônico em bitcoin) colado no chat público do Bitchat é levado pela
ponte, pelo LoRa do Meshtastic, até as outras pontes do mesmo canal, que o entregam aos celulares
perto delas. É o mesmo funcionamento da ponte MeshCore, descrito em detalhe no
[guia dela](https://github.com/EduChacon/Meshcore-Bitchat/blob/main/docs/bitchat/CASHU.md): como
pagar, o que se garante, as mensagens da ponte e o roteiro de ensaio.

**Nada disso foi ligado em aparelho.** Passou nos testes em computador (descompressão, leitura de
tokens, o túnel e o fluxo completo com telefones e rádio simulados) e na compilação do GitHub.

## O que é igual à ponte MeshCore

a) Vão pelo LoRa as próprias mensagens assinadas pelo celular de quem paga (o anúncio e a mensagem
do token), em partes que só aparecem do outro lado quando todas chegam. Uma parte perdida é
pedida de novo, até duas vezes.

b) No canal público da ponte (`#bitchat`, de chave conhecida), só passam tokens em que todas as
provas estão travadas por chave (P2PK). Num canal de chave própria, de 32 bytes, passa também o
token sem trava, com o risco descrito no guia.

c) Nenhum token vai como texto pelo LoRa, e do LoRa para o Bitchat não passa texto que nomeie um
token nem que tenha 40 ou mais letras e números seguidos, sem espaço.

d) As mensagens da ponte, em português, são as mesmas.

## O que é próprio do Meshtastic

a) **Pacotes.** As partes vão como pacotes `PRIVATE_APP` no canal de texto da ponte, de até 200
bytes (o MeshCore usa 160).

b) **Confirmação.** A ponte de destino responde com uma mensagem direta à ponte de quem paga. O
Meshtastic a cifra com as chaves dos dois rádios (PKI) quando conhece a do outro, e isso prova
quem respondeu: só então a ponte de quem paga mostra `✓` com o nome do rádio. Sem a chave, a
resposta vai a todos no canal e chega como `✓?` (`não conferida`); confirme com quem recebe. As
chaves vêm das informações de nó ("NodeInfo") que os rádios trocam sozinhos.

c) **Pontes MeshCore e Meshtastic não trocam pagamentos entre si**: são redes de rádio
diferentes. Duas pontes Meshtastic, ou duas MeshCore, sim.

d) O túnel de laboratório (`Advanced: tunnel`) é outra coisa e não leva pagamentos.

## Para quem desenvolve

- `src/bridge/BridgeInflate.*` e `src/bridge/BridgeCashu.*`: os mesmos da ponte MeshCore.
- `src/bridge/BridgePayTunnel.*`: o túnel da ponte MeshCore, com os nomes `Pay*` (para não se
  confundir com o túnel de laboratório) e partes de 200 bytes.
- `src/bridge/BridgePayments.cpp`: regras, mensagens, envio e entrega.
- Testes: `python3 tools/bridge/test_host.py --crypto-dir <Crypto 0.4.0>` (inclui os testes dos
  pagamentos) e `python3 tools/bridge/test_flow.py --crypto-dir <Crypto 0.4.0>`.
