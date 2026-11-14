# Verificação da publicação .5

Fonte: `be61e1dc7de284f1c220922a72c9c203d9723b8d`.
Tag: [v0.1.0-experimental.5](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.5).
[CI 43514545708](https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43514545708).
Registro acrescentado depois da publicação; não altera imagens ou tag.

## Resultado

- Heltec V3 e Cardputer ADV compilados; reconstruções limpas idênticas por alvo.
- 523 verificações, 19 vetores do cliente oficial e 100.000 entradas inválidas passaram.
- Adaptador BLE real com transporte simulado: conjunto global de oito frames, três conexões,
  broadcast compartilhado, desconexão durante envio, esgotamento/recuperação e expiração.
- Métodos reais de alocação: quatro falhas iniciais injetadas, memória total e maior bloco
  insuficientes, Bluetooth indisponível e 200 ciclos sem vazamento no modelo host com ASan/UBSan.
- Configuração padrão, persistência, confirmação de menu, inicialização BLE e onze testes de
  artefatos/backup passaram. Cppcheck e compilação Heltec sem flag da ponte passaram.
- ZIPs da CI baixados e conferidos contra os digests; verificador aprovou o alvo correto e
  rejeitou o outro. Binário avulso igual ao do ZIP; SHA do ELF embutido igual ao arquivo de símbolos.
- ZIPs e aplicações publicados conferidos pelos digests do GitHub contra esses bytes da CI.
- No alvo ESP32-S3 compilado, objetos iniciais: 66.688 → 23.084 bytes sem overhead do alocador;
  maior objeto: 35.776 → 10.244. Reserva de 24 KiB e bloco livre mínimo de 8 KiB preservados.

`trunk fmt` local encerrou com erro interno. Foram usados os mesmos formatadores fixados:
clang-format 16.0.3 com `.trunk/configs/.clang-format`, Black 26.5.1 e Prettier 3.8.4.
Os testes executados depois da formatação passaram; não se registra o wrapper Trunk como aprovado.

## Imagens verificadas

| Modelo                | Bytes da aplicação | SHA-256 da aplicação                                               | SHA-256 do ZIP                                                     |
| --------------------- | -----------------: | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| heltec-v3             |            2108560 | `83828371d821a34812d30f225ecfe515f156ae3567659cb95135473ad8eab877` | `29159b5142a5927578a58c990937d5431c842053ea346bdf1f01fd5b3a0685f6` |
| m5stack-cardputer-adv |            2225072 | `592cc95ee1d633cc77e4f90fd09686d63e5265b65386d8f722550c46e734559e` | `014ed8b40f62e79ec02790b575f1990e09b5b56443bc3eda5d9a9b1e841e3974` |

## Limites e menor próximo teste

A `.4` apresentou `Low memory: bridge OFF` em um Heltec, mesmo após reiniciar. Não há log de
heap desse aparelho: a condição exata e a suficiência da correção ainda exigem confirmação física.
A `.5` não foi gravada nem ensaiada em hardware pelo agente. Não se equiparam testes host,
tamanhos compilados e hashes a funcionamento BLE/LoRa, preservação ou rollback reais.

Após backup/preflight, usar **Atualizar / Update**, sem apagar flash, com o ZIP exato Heltec V3.
Conferir estado, conectar Meshtastic para hora, abrir Bitchat e testar texto nos dois sentidos
com outro rádio stock. Não recriar canais como tentativa de liberar memória. Ver
[correção de memória](MEMORY_FIX.md), [início rápido](../DEFAULTS.md) e [Windows](../WINDOWS_RECOVERY.md).
O repositório permanece privado: downloads exigem login autorizado. O conector baixa artefatos
privados da CI, mas não anexos binários privados de release; para estes, a conferência usa os
digests da API. Não houve teste do navegador Windows/USB do operador.

## English

Both board builds, identical clean rebuilds, protocol, bounded-memory, BLE, menu, default
configuration, startup, artifact tests and static analysis passed; stock Heltec also compiled.
Downloaded CI ZIPs, manifests, application images and matching ELF hashes were checked.
Published asset digests match those verified CI bytes. Source-size reduction is measured
in compiled layouts, not on the device. Physical boot, phone delivery, preservation, power,
recovery and rollback still require acceptance. Use the exact model's update ZIP without erase.
