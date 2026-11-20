# Conversa privada com a ponte e mensagens privadas pelo LoRa (experimental, a partir da `1.0.0-beta.17`)

**Nada disso foi ligado em aparelho.** O protocolo foi conferido byte a byte com uma implementação
independente do Noise e com o código do Bitchat para iPhone e Android, e o fluxo completo passou nos
testes em computador, com telefones e rádio simulados. A conversa com o aplicativo de verdade ainda não
foi ensaiada.

## Conversa privada com a ponte

No Bitchat, a ponte aparece na lista de pessoas com o nome que ela anuncia. Tocando nela, abre-se uma
conversa privada, como com qualquer pessoa, e os pedidos feitos ali têm resposta só ali, sem aparecer
no chat público.

a) **Basta a palavra:** `bateria`, `quem`, `guia`, `teste`, `recado <texto>`, `sos <texto>`, ou `ponte`
para o estado (em inglês, `battery`, `who`, `guide`, `test`, `note`, `sos`, `bridge`). Os comandos
completos (`!ponte bateria`) também valem. Qualquer outra coisa recebe a lista de palavras.

b) **As regras são as do chat público:** a lista de rádios continua só no aplicativo do próprio rádio,
o recado só muda por quem está ligado diretamente ao rádio, e os ajustes continuam só pelo aplicativo
Meshtastic. O que um pedido manda ao LoRa (um socorro, um teste de alcance) vai ao LoRa como no chat
público; o resultado de um teste de alcance pedido na conversa privada volta a ela.

c) **Criptografia:** a mesma das conversas privadas entre telefones (protocolo Noise, com a chave que a
ponte anuncia). Cada mensagem recebida é confirmada como entregue. A ponte mantém até três conversas ao
mesmo tempo; uma conversa usada nos últimos dois minutos não dá lugar a outra, e uma conversa parada por
30 minutos é encerrada. Depois de reiniciar o rádio, a primeira mensagem refaz a conversa sozinha.

d) **Proteções:** os pacotes de abertura da conversa não são assinados, então qualquer um poderia se
passar por um telefone. Por isso nada que não se autentique encerra uma conversa ou uma abertura em
curso, as aberturas são limitadas por telefone e no total, e as respostas são limitadas a dez de uma vez
e uma por segundo depois.

e) **O túnel de laboratório não leva estas conversas:** pacotes endereçados à própria ponte nunca são
repassados nem postos no túnel.

## Mensagens privadas pelo LoRa

Na conversa privada com a ponte:

a) **`@nome texto`** (ou `@nome: texto`) manda `texto` como mensagem direta ao nó Meshtastic com esse
nome: o nome longo ou o curto, sem diferença entre maiúsculas e minúsculas, o mais longo que coincidir.
No LoRa a mensagem sai como `[Bitchat apelido] texto`, com até 200 caracteres no total, cifrada com as
chaves dos dois rádios (PKI). A ponte responde `Enviado a <nome> pelo LoRa.`

b) **Recusas,** sem nada no ar: nome desconhecido, dois nós com o mesmo nome, nó sem chave pública (o
firmware não manda mensagem direta sem ela), texto vazio ou longo demais. Limite: duas mensagens de cada
telefone e depois uma a cada 20 s; para todos os telefones juntos, cinco e depois uma a cada 3 minutos.

c) **A resposta volta ao telefone.** Uma mensagem direta que chega ao rádio da ponte vai à conversa
privada como `[MT nome] texto` quando: começa com `@apelido` de um telefone Bitchat que a ponte conhece;
ou vem de um nó a quem um telefone escreveu pela ponte nas últimas 24 horas (vai ao último que escreveu).
As demais mensagens diretas são do dono do rádio e só aparecem no aplicativo Meshtastic, como sempre.
**O aplicativo Meshtastic do dono continua vendo todas as mensagens diretas do rádio**, inclusive as
levadas a telefones.

d) **Telefone longe:** a mensagem fica guardada por até 24 horas (oito mensagens ao todo; a mais antiga
dá lugar à nova). Quando o telefone se apresenta perto da ponte, ela abre a conversa privada e entrega.

e) **A ponte lê o texto:** ela abre a mensagem do Bitchat e a cifra de novo para o LoRa, e o contrário.
Não é cifragem de ponta a ponta entre o telefone e o nó. O nó vê que a mensagem veio do rádio da ponte.

f) **O dono decide:** no aplicativo Meshtastic do rádio da ponte, `!bridge dm off` (ou
`!ponte privado off`) desliga as mensagens privadas pelo LoRa, nos dois sentidos; `!bridge dm on` religa.
Vem ligado. A escolha fica gravada.

## Para quem desenvolve

- `src/bridge/BridgeNoise.*`: Noise XX e o enquadramento do Bitchat; os mesmos arquivos da ponte MeshCore.
- `src/bridge/BridgePrivate.*`: sessões, aberturas, respostas, `@nome` e caixa de entrada.
- Testes: `tools/bridge/test_host.py` (vetores de `tools/bridge/noise_vectors.py`) e
  `tools/bridge/test_flow.py`.
