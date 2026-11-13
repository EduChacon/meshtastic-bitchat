# Verificação da publicação .4

Fonte: `e69edd9d2502add17a1d38b048cc8737e1ca9a72`.
Tag: [v0.1.0-experimental.4](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.4).
[CI 43499954132](https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43499954132):
dois alvos e publicação concluídos com sucesso em 2026-11-13. Este registro foi acrescentado
à documentação após a publicação; não altera a tag nem as imagens.

## Resultado de software

- Heltec V3 e Cardputer ADV + Cap LoRa-1262 compilados.
- Recompilação limpa gerou o mesmo SHA-256 por alvo no ambiente da CI.
- 523 verificações, 19 vetores produzidos pelo cliente oficial e 100.000 entradas inválidas.
- Adaptador BLE, inicialização, menu/controle negativo e configuração padrão executados em host.
- Onze testes de artefatos/backup passaram; Cppcheck passou; Heltec sem flag da ponte compilou.
- Árvores de firmware, variantes e testes publicadas coincidem com as fontes locais verificadas.
- Os dois artefatos da CI foram baixados e seus hashes conferidos. Cada ZIP passou no verificador;
  selecionar o outro modelo foi rejeitado. Binário avulso e binário no ZIP são idênticos.
- Cada imagem contém o SHA-256 do ELF correspondente. Os manifestos dizem explicitamente
  `hardware_tested: false`, padrão BLE/texto ON e túnel de laboratório OFF.
- Tag e release apontam ao commit acima. Os hashes informados pelo GitHub para os anexos
  publicados coincidem com os arquivos baixados da CI. São nove anexos, incluindo fontes
  correspondentes, dependências/licenças e evidências no pacote técnico.

O repositório está privado. Download anônimo dos anexos retorna 404; é necessário login com
acesso ao repositório. O conector usado permite baixar artefatos da CI, mas não anexos binários
privados de release: a conferência final dos anexos usa os digests retornados pelo GitHub,
comparados aos bytes baixados da CI. Isso não é teste do navegador Windows/USB do operador.

## Imagens

| Modelo        | RAM estática / orçamento | Aplicação (bytes) | SHA-256 da aplicação                                               |
| ------------- | ------------------------ | ----------------- | ------------------------------------------------------------------ |
| Heltec V3     | 132736 / 327680          | 2107008           | `4b0466fae5d935a13d98eced7026abb4c09bbd5ebae131aa61a22bf7e37c614e` |
| Cardputer ADV | 137120 / 327680          | 2223600           | `ed3597ccec3a29f0326404ed2c68ea3e03158869503110c18c0de54110197608` |

RAM estática não equivale a heap livre em execução. Os buffers continuam alocados somente
após inicialização BLE e validação de memória; margem, picos e estabilidade exigem medição.

| Arquivo                                                                     | SHA-256                                                            |
| --------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| `meshtastic-bitchat-heltec-v3-0.1.0-experimental.4-e69edd9.zip`             | `2e7cfc66e51323a747f0c979a4859056c74f94c60306fefcb870e7064236b2da` |
| `meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.4-e69edd9.zip` | `22cc85805c941c182c706239b4219705a9f51d27f7178fc4ddc6131e2a602cd3` |
| ELF Heltec V3                                                               | `322bc06029f51c6d9f3e810681dbaf1ff5a2b3869f132e69c7d738a7c7e63f07` |
| ELF Cardputer ADV                                                           | `4e1d32fce40c4fa8b93342046df6fe21e345a560a312e438d400268402cd0c49` |

## Bancada pendente

Boot desta revisão, entrega BLE/LoRa/iPhone/Android, pareamento simultâneo, consumo, falta
de energia, preservação, atualização e rollback físicos continuam **não testados**.
O boot normal relatado na `.3` não é aceite da `.4`. Não há garantia de risco zero.

Seguir [Windows](WINDOWS_RECOVERY.md) para backup/preflight e **Update**, depois
[início rápido](DEFAULTS.md). O menor aceite usa uma ponte Heltec e um rádio stock, internet
desligada e Bitchat em primeiro plano: texto no canal público dedicado → `#mesh`;
`!lora texto` em `#mesh` → esse canal. Aguardar 30 segundos e conferir hora/estado/BLE.
Comparar com a ponte desligada e testar os apps separadamente antes da simultaneidade.

## English

Both board builds, identical clean rebuilds, host/defensive tests, static analysis and the
stock Heltec compilation passed. The CI artifacts were downloaded and checked against
GitHub's artifact digests. ZIP and standalone application match; wrong-board selection is
rejected; embedded ELF hashes match their debug symbols. Published release asset digests
match these verified CI files. Private release downloads require a GitHub account with access;
an authenticated browser/USB installation was not exercised here.

The firmware tag is unchanged by this later documentation record. Physical boot, phone
delivery, power measurements, recovery and rollback remain untested for this revision.
