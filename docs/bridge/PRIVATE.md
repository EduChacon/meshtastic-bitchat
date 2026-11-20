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
mesmo tempo; uma conversa aberta e usada nos últimos dois minutos não dá lugar a outra, e uma conversa
parada por 30 minutos é encerrada. Depois de reiniciar o rádio, a primeira mensagem refaz a conversa
sozinha.

d) **Proteções:** os pacotes de abertura da conversa não são assinados, então qualquer um poderia se
passar por um telefone. Por isso:
   - nada que não se autentique encerra uma conversa ou uma abertura em curso;
   - a ligação Bluetooth pela qual a ponte responde a um telefone só muda com um pacote que se autentica
     (a última etapa da abertura ou uma mensagem que se decifra); um pacote qualquer com o ID de outro
     telefone não desvia as respostas dele;
   - um telefone novo só ocupa um lugar com um pacote de abertura bem formado, e no máximo seis seguidos e
     depois um a cada 5 s para todos; lugares de conversas ainda não abertas cedem primeiro, e nunca tomam
     o de uma conversa aberta e em uso;
   - as aberturas são limitadas por telefone e no total, e as respostas a dez de uma vez e uma por segundo
     depois.

e) **O túnel de laboratório não leva estas conversas:** pacotes endereçados à própria ponte nunca são
repassados nem postos no túnel.

## Mensagens privadas pelo LoRa

Na conversa privada com a ponte:

a) **Só aos favoritos do rádio.** `@nome texto` (ou `@nome: texto`) manda `texto` como mensagem direta
ao nó Meshtastic com esse nome, desde que o dono o tenha marcado como favorito no aplicativo Meshtastic
do rádio da ponte: o nome longo ou o curto, sem diferença entre maiúsculas e minúsculas, o mais longo que
coincidir. No LoRa a mensagem sai como `[Bitchat apelido] texto`, com até 200 bytes no total, sempre
cifrada com as chaves dos dois rádios (PKI) e com pedido de confirmação (no modo econômico, sem ele).
Quebras de linha e caracteres de controle viram espaços. A ponte responde `Enviado a <nome> pelo LoRa.`
quando entrega a mensagem ao firmware para envio; isso não confirma a chegada ao nó.

b) **Recusas,** sem nada no ar, em português e em inglês:
   - qualquer nome que não seja de um favorito, conhecido ou não, recebe a mesma resposta
     (`Não é possível mandar a "<nome>": só aos favoritos do rádio.`), para que a conversa não revele
     quais nós o rádio conhece;
   - dois favoritos com o mesmo nome;
   - favorito ainda sem chave pública (o firmware não manda mensagem direta sem ela);
   - texto vazio ou longo demais;
   - rádio em modo radioamador (nada vai cifrado nesse modo);
   - LoRa ocupado ou sem folga de tempo de ar;
   - limite: duas mensagens de cada telefone e depois uma a cada 20 s; para todos os telefones juntos,
     cinco e depois uma a cada 3 minutos.

c) **A resposta volta ao telefone** só quando vem de um favorito, cifrada para este rádio (PKI), e não é
uma reação ("tapback"). Vai à conversa privada como `[MT nome] texto` (`nome#xxxx` quando dois nós têm
o mesmo nome) em dois casos:
   - começa com `@apelido` de um telefone que se apresentou a este rádio pelo Bluetooth (não um telefone
     conhecido pelo túnel nem outra ponte); se dois telefones usam o mesmo apelido, só vale a forma com o
     fim do ID, `@apelido#xxxx`, como aparece no chat; com dois iguais e sem ela, a mensagem não vai a
     nenhum;
   - sem `@`, vem de um favorito a quem um telefone escreveu pela ponte nos últimos 30 minutos (vai ao
     último que escreveu).

   A mesma mensagem repetida pelo nó em até 2 minutos passa uma vez só. Todas as outras mensagens diretas
   são do dono do rádio e só aparecem no aplicativo Meshtastic, como sempre.

d) **O dono vê tudo:** o aplicativo Meshtastic do rádio da ponte continua mostrando todas as mensagens
diretas que o rádio recebe, inclusive as levadas a telefones; as que os telefones mandam saem do rádio
dele, com o nó dele como remetente.

e) **Telefone longe:** a mensagem fica guardada até o telefone confirmar que a recebeu, por no máximo 24
horas: três por telefone, três de cada nó, oito ao todo; a mais antiga dá lugar à nova. Ela só é enviada
a um telefone presente (conversa com sinal de vida nos últimos 30 s, ou apresentação ao rádio nos últimos
30 s); sem a confirmação, vai de novo, com o mesmo identificador, depois de 30 s, e o aplicativo não a
mostra duas vezes. Quando o telefone se apresenta perto da ponte, ela abre a conversa privada e entrega.

f) **A ponte lê o texto:** ela abre a mensagem do Bitchat e a cifra de novo para o LoRa, e o contrário.
Não é cifragem de ponta a ponta entre o telefone e o nó. O nó vê que a mensagem veio do rádio da ponte.

g) **O dono decide:** no aplicativo Meshtastic do rádio da ponte, `!bridge dm off` (ou
`!ponte privado off`) desliga as mensagens privadas pelo LoRa, nos dois sentidos, e apaga na hora as
mensagens guardadas e a lembrança das conversas; `!bridge dm on` religa. Vem ligado. A escolha fica
gravada (no nRF52, num arquivo próprio, que não altera os arquivos das versões anteriores); se o valor
gravado não puder ser lido, as mensagens ficam desligadas.

## Para quem desenvolve

- `src/bridge/BridgeNoise.*`: Noise XX e o enquadramento do Bitchat; os mesmos arquivos da ponte MeshCore.
- `src/bridge/BridgePrivate.*`: sessões, aberturas, respostas, `@nome` e caixa de entrada.
- `src/bridge/nrf52/BridgeNrf52Prefs.cpp`: a gravação do nRF52, com `dm` em `/prefs/bitchat_dm.dat`.
- Testes: `tools/bridge/test_host.py` (vetores de `tools/bridge/noise_vectors.py`),
  `tools/bridge/test_flow.py` (as conversas e as mensagens pelo LoRa, com telefones e rádio simulados),
  `tools/bridge/test_ble.py` (a gravação do nRF52) e `tools/bridge/test_defaults.py` (a leitura de `dm`
  ao ligar).
