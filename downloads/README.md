# Arquivos de ensaio

Compilações do ramo de trabalho, commit `6d3c242`, geradas pela integração contínua. **Não são
uma versão publicada** e não passaram por ensaio em aparelho. A versão publicada continua sendo
a [v1.0.0-beta.4](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta.4).

| Arquivo                                                 | Aparelho         | SHA-256                                                            |
| ------------------------------------------------------- | ---------------- | ------------------------------------------------------------------ |
| `firmware-tracker-t1000-e-bitchat-2.7.26.6d3c242.uf2`   | SenseCAP T1000-E | `d3ba9aca7eb744bbe16f3e7d663b8e6fbd3fb357c17dba16e65f7262b8058e88` |
| `meshtastic-bitchat-heltec-v3-1.0.0-beta.4-6d3c242.zip` | Heltec V3        | `1be16035915f414cc65401f4260c25ed6618283ee2a332da96656005172ba9b6` |

- **T1000-E:** primeira compilação da ponte para esse aparelho. Instalação, limites e roteiro em
  [docs/bridge/T1000E.md](../docs/bridge/T1000E.md).
- **Heltec V3:** o nome do arquivo traz `1.0.0-beta.4` porque a numeração só muda na publicação; o
  que o distingue é o final `6d3c242`. Contém a `1.0.0-beta.4` mais os comandos `!bridge on`, `off`, `local`, `lora` e `help`
  e o serviço do Meshtastic na resposta de varredura do anúncio. Grava-se pelo
  [flasher oficial](https://flasher.meshtastic.org/), alvo `heltec-v3`, com **Update**. Não serve
  para outros modelos.

Remover esta pasta quando esses arquivos forem substituídos por uma versão publicada.
