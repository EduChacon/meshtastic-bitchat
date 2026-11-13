# Segurança e limites de confiança

Esta é uma implementação experimental, não uma auditoria independente nem uma garantia de
ausência de vulnerabilidades. A ponte começa desligada. Não há acesso a rádio, atualização
própria, telemetria externa da ponte ou execução de comandos recebidos por mensagem.

## Fronteiras

- A encaminha pacotes Bitchat com autenticação/frescor verificáveis e formatos limitados.
  As sessões Noise permanecem nos clientes. A ponte não termina sessões nem guarda chaves
  privadas de clientes. Sua identidade própria serve para presença e texto traduzido.
- B exporta **texto público** autorizado para um canal secundário e vice-versa. A PSK protege
  o transporte Meshtastic; não protege a cópia pública no Bitchat. A confirmação local é
  obrigatória. Canal primário, PSK padrão, chave fora do tamanho aceito e MQTT são recusados.
- C acrescenta HMAC e domínio ao transporte de bytes. O HMAC autentica membros do domínio,
  não uma pessoa. Não substitui Noise/Ed25519. O modo privado é experimental e separado:
  conteúdo cifrado pode ser mídia; o gateway não consegue distingui-lo de texto.
- PeerID e assinatura da presença constituem uma associação TOFU em RAM. Uma presença
  autoassinada não prova posse da chave privada Noise anunciada. Verificação de contatos
  continua sendo responsabilidade dos apps e dos usuários. Apelidos não autenticam origem.
- Um participante que conhece a PSK pode produzir envelopes e marcadores válidos. Não
  tratamos usuários autorizados à chave como autoridades administrativas. NodeNum também
  não é identidade pessoal autenticada entre redes.

## Ameaças, defesa e como refutar

| Ameaça                              | Defesa implementada                                                                  | Teste que pode refutar / lacuna                                                                              |
| ----------------------------------- | ------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------ |
| Frame malformado ou enorme          | Limites antes de cópia, buffers estáticos, validação de comprimentos e UTF-8         | ASan/UBSan, mutações estruturadas e entradas aleatórias; callbacks BLE ainda precisam de bancada             |
| Fragmentos consomem toda a RAM      | 3 remontagens BLE e 3 de túnel, teto por frame e prazo não renovável                 | Ocupar todas, repetir fragmento, aguardar prazo, verificar liberação e heap em rádio                         |
| Spoof ou assinatura maleável        | Ed25519 da biblioteca upstream, S canônico, pontos inválidos/pequena ordem recusados | Vetores independentes do cliente; não substitui auditoria criptográfica externa                              |
| Replay e reordenação                | Frescor ±120 s, ID SHA-256 canônico sem TTL, cache 256/125 s                         | Repetir antes/depois do prazo, voltar relógio e reiniciar                                                    |
| Reinício de gateway                 | Diário de 64 IDs/10 min em NVS, gravação antes de exportação                         | Corte no ponto de gravação; erro NVS bloqueia; gravação antes de envio pode perder mensagem                  |
| Eco entre A/B/C                     | IDs canônicos, marcador B autenticado, C recebido não é reexportado para LoRa        | Duas pontes, redes sobrepostas e B+C; não existe garantia de entrega exatamente uma vez                      |
| Duas pontes B sobrepostas           | Prova autenticada na presença, espera 30 s e bloqueio ao detectar conflito           | Uma ponte invisível à outra pode escapar: topologia suportada exige um exportador por malha BLE/canal        |
| Inundação e Sybil                   | Fila global LoRa 4, por conexão BLE 2, limites por peer, orçamento global            | 24 identidades podem ocupar a tabela; DoS permanece possível, sem expulsão automática de identidades válidas |
| Um iPhone lento bloqueia outros     | Fila de saída separada por conexão, rodízio e prazo                                  | Medir justiça com 3 clientes, um sem consumir; timeout descarta conexão para ressincronizar                  |
| Mudança de canal/PSK                | Vínculo de configuração por digest, validação repetida                               | Mudar índice, nome, PSK, MQTT ou papel com filas cheias: deve bloquear                                       |
| Queda de tensão, firmware incorreto | Sem erase/bootloader/partições no pacote, preflight e confirmação física             | Nenhuma gravação é livre de risco; recuperação e preservação ainda não testadas                              |

O diário guarda IDs e horários, sem corpo de mensagem. Cada atualização grava somente um
registro de 24 bytes em uma chave NVS própria, não toda a tabela; o desgaste real depende
do gerenciamento NVS e da flash e ainda precisa ser medido. O túnel verifica espaço antes
de persistir. Presenças C usam dedup em RAM para não
consumir o diário de mensagens. IDs truncados do diário/envelope têm 128 bits; colisão é
extremamente improvável, não impossível. Colisões e saturação causam descarte, nunca
substituição silenciosa de identidade ou abertura de canal.

## Privacidade e armazenamento

O código da ponte não registra corpos, localização ou chaves. A variante usa `DEBUG_MUTE`.
**O firmware Meshtastic mantém seu armazenamento nativo**, inclusive histórico de texto
quando habilitado e configurações com PSK. Isso não é eliminado pela ponte. Backups, dumps
de flash e NVS contêm material sensível: manter privados, nunca anexar a issues/releases.
RAM temporária contém pacotes cifrados e texto público durante o processamento.

Não há criptografia de flash ou secure boot novos, e não se alteram eFuses. Quem obtiver
acesso físico pode ler a identidade da ponte e as PSKs conforme a proteção stock do aparelho.
Perda da ponte exige avaliar revogação/troca das chaves do domínio e dos canais afetados.
Não prometer sigilo futuro por apenas apagar uma entrada de menu.

## Criptografia e dependências

Ed25519, SHA-256, HMAC e HKDF usam Crypto 0.4.0, a biblioteca já utilizada pelo firmware.
O projeto não cria algoritmo criptográfico. A verificação adicional recusa encodings não
canônicos antes de chamar a biblioteca; os valores de pequena ordem têm proveniência
libsodium, com licença preservada. O envelope e os domínios de HKDF estão versionados.

As versões vêm do commit upstream fixado. A release registra os conteúdos das dependências
e seus SHA-256; isso detecta deriva, não comprova ausência de CVEs. Bibliotecas, SDK e
toolchain precisam continuar recebendo revisão de segurança antes de promover uma versão
de bancada a uso cotidiano. Nenhum binário de ponte de terceiros foi usado.

## Comportamento fechado e recuperação

Relógio ausente, configuração inválida, versão de envelope desconhecida, HMAC inválido,
fila cheia ou armazenamento falho resultam em descarte/bloqueio com contador. Não há
fallback para Nostr, MQTT ou texto claro. Abrir Bitchat perto da ponte e usar o menu de
estado/alternância é a recuperação normal prevista. Reboot esvazia peers e filas voláteis;
não resolve armazenamento corrompido e não autoriza apagar NVS.

Não executar ensaios ativos contra redes de terceiros. Usar mensagens, identidades e PSKs
sintéticas em rede consentida, antena correta e controles RF originais.
