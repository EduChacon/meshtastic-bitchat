# Compatibilidade: protocolo não equivale a ensaio físico

Referência de fonte: Bitchat iOS 1.8.0 (`30c58eee349e172b37f69faa2113c9d5f2c76c23`).
As versões instaladas do Bitchat, Meshtastic e sistema operacional devem ser registradas em
cada ensaio, junto com modelo/revisão, antena, banda, frequência efetiva e preset dos rádios.
Não há combinação física validada nesta release. Android 2.2.0 é uma referência adicional
de fonte; não participou dos vetores nem de testes de interoperabilidade.

O cliente de referência contém anúncios, mensagens públicas, Noise, contatos, grupos/prekeys,
sincronização/courier, mídia, recibos e voz, além de recursos Nostr/geográficos. A presença
na fonte não prova habilitação na distribuição instalada. Capacidades anunciadas, opções de
compilação, permissões e configurações devem ser registradas no início da bancada. A ponte
não anuncia suporte próprio a grupos, correio, recibos de mídia ou verificação Noise.

Legenda: **HW** confirmado em hardware; **SIM** testado em host/simulação; **COMP** compilado;
**PREV** previsto pelo desenho/fonte; **INC** incompatível com o modo/limite; **NA** não aplicável;
**NT** não testado. Nesta entrega **nenhuma célula é HW**. BLE direto também não foi medido aqui.

| Função                        | Cliente original                 | BLE direto | Repetidor A              | Tradução B             | Túnel C                        | Fixo              | Econômico               | Evidência                                       | Limitação                                                         |
| ----------------------------- | -------------------------------- | ---------- | ------------------------ | ---------------------- | ------------------------------ | ----------------- | ----------------------- | ----------------------------------------------- | ----------------------------------------------------------------- |
| Descoberta/presença           | Presente na fonte                | PREV/NT    | COMP/PREV                | Presença da ponte COMP | COMP/PREV                      | Disponível PREV   | Anúncio mais lento PREV | UUID, TLV e anúncios assinados; vetores SIM     | Só periférico; até 24 identidades; relógio obrigatório            |
| Texto público                 | Presente                         | PREV/NT    | COMP, codec SIM          | COMP, conversão SIM    | COMP, envelope SIM             | PREV              | PREV, mais atraso       | Tipos 1/2; TEXT_MESSAGE_APP 1                   | B exige `!lora `; C exige `!bctun `; 120 bytes de corpo           |
| DMs entre clientes            | Noise presente                   | PREV/NT    | PREV/NT                  | INC por política       | PREV/NT, opt-in lab            | NT                | NT                      | Encaminhamento de 0x10/0x11 sem descriptografia | Não há sessão real testada; timeout pode impedir C                |
| DM para a ponte               | Cliente permite escolher contato | NA         | INC                      | INC                    | INC                            | NA                | NA                      | Ponte não implementa endpoint Noise             | Identidade da ponte serve para anúncios e conversões              |
| Verificação de contatos       | Presente                         | PREV/NT    | PREV de ponta a ponta    | INC                    | PREV/NT                        | NT                | NT                      | Chaves e assinaturas preservadas                | TOFU da ponte não é verificação Noise de um contato               |
| Grupos e mensagens            | Presente, depende de capacidades | PREV/NT    | PREV limitado            | INC                    | PREV/NT, privado lab           | NT                | NT                      | Pacotes 0x24/0x25 e sessões                     | Não traduzir semântica para canal Meshtastic                      |
| Convites/chaves de grupo      | Presente                         | PREV/NT    | PREV opaco               | INC                    | PREV/NT se couber              | NT                | NT                      | Permanecem em pacotes dos clientes              | Sem chave privada retida; não é gerenciador de grupo              |
| Entrega/leitura               | Presente, depende do tipo        | PREV/NT    | PREV opaco               | INC como equivalência  | PREV/NT                        | NT                | NT                      | Recibos do cliente permanecem no protocolo      | ACK Meshtastic não significa chegada/leitura no iPhone            |
| Sincronização/histórico       | Presente                         | PREV/NT    | INC nesta versão         | INC                    | INC                            | INC               | INC                     | REQUEST_SYNC local e RSR com contexto           | Não inventar autorização de replay                                |
| Reenvio                       | Políticas do cliente             | PREV/NT    | Limitado                 | Sem ACK de app         | Best effort, sem ARQ           | PREV              | Mais restrito           | Dedup e filas SIM                               | Repetir exatamente o mesmo evento pode ser suprimido              |
| Fragmentação                  | Presente                         | PREV/NT    | COMP, remontagem SIM     | Não exporta anexos     | COMP, remontagem SIM           | PREV              | PREV limitado           | Perda, duplicata, ordem e expiração em testes   | A 2 KiB/32 fragmentos; C embarcado 512 bytes                      |
| Correio offline/courier       | Presente                         | PREV/NT    | Relé opaco limitado PREV | INC                    | INC por política de admissão C | Sem armazenamento | Sem armazenamento       | Tipo 0x04 local, sem serviço de depósito        | Não aceita compromisso de correio durável                         |
| Anexos/mídia pública          | Presente                         | PREV/NT    | PREV só se couber        | INC                    | INC                            | NT em BLE         | NT em BLE               | Tipos preservados dentro do limite              | Arquivos maiores são rejeitados; não prometer transparência total |
| Mídia privada                 | Presente/capacidades             | PREV/NT    | PREV opaco limitado      | INC                    | Não classificável no Noise lab | NT                | NT                      | Não se lê o plaintext                           | Pode consumir quota até ser rejeitada por tamanho/tempo           |
| Voz/PTT                       | Presente na fonte                | PREV/NT    | NT; vazão precisa medir  | INC                    | INC para voz contínua LongFast | Só candidato BLE  | Não aceito              | Comparação analítica de taxas                   | Nenhuma qualidade de voz medida                                   |
| Boards/publicações            | Presente na fonte                | PREV/NT    | PREV limitado            | INC                    | INC na admissão inicial C      | NT                | NT                      | Sem conversão de tipo 0x23                      | Sem equivalente automático em texto stock                         |
| Canais geográficos/Nostr      | Presente, usa serviços online    | NT         | Sem gateway online       | INC                    | INC                            | Fora do offline   | Fora do offline         | Função distinta da malha BLE                    | Sem fallback online; preferência Nearby não é consentimento LoRa  |
| Posição/telemetria Meshtastic | Presente no Meshtastic           | NA         | NA                       | INC por política       | NA                             | Stock separado    | Stock separado          | Somente portas 1/256 no módulo                  | Não confundir recursos stock com dados exportados                 |

## Aparelhos Meshtastic stock

B usa texto nativo, sem extensão no aplicativo remoto. A compatibilidade pretendida é com
qualquer nó Meshtastic que participe do mesmo enlace/configuração e apresente texto na porta 1.
Não há lista de fabricantes codificada para a tradução. Banda, modulação, região, frequência
efetiva, PSK, papel de canal, versão e problemas do rádio continuam determinantes.

| Família                                 | Como nó stock remoto/intermediário | Como ponte                               | Condição importante                                                         |
| --------------------------------------- | ---------------------------------- | ---------------------------------------- | --------------------------------------------------------------------------- |
| Heltec V3                               | PREV/NT                            | COMP na `.5` e na candidata; hardware NT | ESP32-S3/SX1262, 8 MB; confirmar revisão e banda físicas                    |
| Cardputer ADV + Cap LoRa-1262           | PREV/NT                            | COMP na `.5` e na candidata; hardware NT | Confirmar Cap/revisão, comutação de antena e banda; não confundir original  |
| Heltec V4                               | PREV/NT                            | Alvo só na candidata; hardware NT        | ESP32-S3, 16 MB; alvo com OLED, não o `heltec-v4-tft`                       |
| Heltec Wireless Stick Lite V3           | PREV/NT                            | Alvo só na candidata; hardware NT        | Sem tela: só os padrões automáticos, sem menu                               |
| Heltec Wireless Tracker                 | PREV/NT                            | Alvo só na candidata; hardware NT        | Alvo V1.1; tela pequena, textos do painel não conferidos                    |
| Heltec Wireless Paper                   | PREV/NT                            | Alvo só na candidata; hardware NT        | Tela e-ink; alvo padrão, não o InkHUD                                       |
| LILYGO T-Beam Supreme                   | PREV/NT                            | Alvo só na candidata; hardware NT        | `tbeam-s3-core`; não vale para o T-Beam clássico                            |
| Seeed XIAO ESP32-S3                     | PREV/NT                            | Alvo só na candidata; hardware NT        | Com o módulo Wio-SX1262; tela opcional                                      |
| LILYGO T-Deck                           | PREV/NT                            | Alvo só na candidata; hardware NT        | 16 MB; alvo `t-deck`, não `t-deck-tft` nem T-Deck Pro                       |
| Station G2                              | PREV/NT                            | Alvo só na candidata; hardware NT        | 16 MB; potência alta, conferir limites regionais                            |
| SenseCAP T1000-E                        | PREV/NT; primeira bancada stock    | Sem binário; exige porte                 | nRF52840/LR1110; recepção direta de SX127x tem restrição documentada        |
| RAK4631/WisBlock, T-Echo e outros nRF52 | PREV/NT                            | Sem binário; exige porte                 | Pilha Bluetooth e armazenamento diferentes do ESP32; UF2                    |
| T-Beam clássico, T-LoRa32, T3-S3        | PREV/NT                            | Sem binário                              | ESP32 clássico ou 4 MB: fora do leiaute revisado e sem medição de memória   |
| Heltec V2 e outras                      | PREV/NT                            | Sem binário                              | Chip, flash, pinagem e potência diferem; nunca usar binário de outro modelo |

Os oito alvos marcados como "só na candidata" foram acrescentados depois da `.5`. Cada um
estende o ambiente stock do próprio aparelho e é aceito pelas ferramentas de pacote somente
para esse modelo. A compilação de cada alvo consta da CI do commit; nenhum deles foi ligado
com este firmware, e
tela, botões, memória livre e rádio dependem de ensaio por modelo.

O Heltec V3 usa SX1262: a restrição publicada para recepção de SX127x pelo LR1110 não demonstra
um defeito nessa dupla V3/T1000-E. O baseline stock nos dois sentidos é obrigatório para
separar falha de rádio de falha da ponte. Intermediários stock do túnel precisam de teste próprio.

## Estados dos telefones

Testar separadamente iPhone/Android e a versão exata: primeiro plano, background, tela bloqueada,
encerramento manual, Bluetooth desligado/reativado, saída/retorno ao alcance, restart de rádio,
dois e três telefones, conexão alternada e simultânea dos apps. Todas essas células estão NT.
Não há promessa de entrega com app encerrado manualmente. Recuperação visível: conferir estado
local, conectar Meshtastic se faltar hora e abrir Bitchat perto da ponte. Nada depende de Nostr.
