# Instalação e recuperação

> **`.6`:** há pacotes para dez aparelhos ESP32-S3, de 8 e de 16 MB; a lista está em
> [COMPATIBILITY.md](COMPATIBILITY.md). O procedimento abaixo vale para todos, trocando o nome
> do modelo. As tabelas desta página citam o Heltec V3 e o Cardputer ADV como exemplos.

**Pré-releases anteriores à `.3` retiradas dos downloads:** o boot reportado da `.2` no Heltec V3
travou e reiniciou pelo watchdog. A `.3` contém correções de inicialização para teste,
sem boot físico validado. Consulte [BOOT_INCIDENT](BOOT_INCIDENT.md) e o
[roteiro Windows com backup privado](WINDOWS_RECOVERY.md). Rollback físico ainda não validado.

Não existe garantia de risco zero em uma gravação. Falta de
energia, seleção errada, bootloader, partições e defeito físico podem impedir o boot.
Nenhum comando deste projeto grava um rádio automaticamente.

## Onde baixar

Na página de [Releases](https://github.com/EduChacon/meshtastic-bitchat/releases), abrir a
versão experimental mais recente e expandir **Assets**. Para Heltec V3, baixar
`meshtastic-bitchat-heltec-v3-<versão>-<commit>.zip`; para Cardputer ADV + Cap LoRa-1262,
`meshtastic-bitchat-m5stack-cardputer-adv-<versão>-<commit>.zip`. Baixar também
`SHA256SUMS` e `verify_artifact.py`. Os anexos `Source code` são fontes, não imagens para flash.

Os anexos de auditoria ficam reunidos em `technical-package-<versão>.tar.gz`:
fontes, dependências, licenças, símbolos, manifestos, resultados e cópia dos anexos originais.
Esse pacote técnico não deve ser enviado ao flasher. A organização não alterou os binários.

O arquivo `firmware-<modelo>-bridge-<versão>-<commit>-update.bin` é a aplicação do mesmo
ZIP, fornecida separadamente como alternativa para o upload próprio. Nunca escolher apenas
pelo processador: os dois alvos desta release usam ESP32-S3, com hardware distinto.

## Pacotes desta release

| Alvo                          | Artefato publicado                                                   | Método/navegador                                                                         | Bootloader                                                         | Estado                                                   | Rollback previsto                                                             |
| ----------------------------- | -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Heltec V3 8 MB                | ZIP por placa e `firmware-heltec-v3-bridge-…-update.bin`             | Upload próprio no Web Flasher oficial; Chrome/Edge desktop com Web Serial e USB de dados | ROM/bootloader ESP32-S3 já instalado, preservado                   | `.2`: escrita reportada, boot falhou; candidato ainda NT | Atualização oficial Heltec V3 compatível, via USB; ROM download se necessário |
| Cardputer ADV + Cap LoRa-1262 | ZIP por placa e `firmware-m5stack-cardputer-adv-bridge-…-update.bin` | Mesmo fluxo ESP32, selecionando o alvo exato                                             | Procedimento M5 específico da revisão; não confundir modos de boot | COMP; flash NT                                           | Firmware oficial do alvo ADV e procedimento M5                                |
| T1000-E                       | Nenhum firmware de ponte                                             | Futuro: UF2/bootloader nRF52840 específico documentado por Seeed/Meshtastic              | Não usar ESP32, .bin mesclado ou offset 0x10000                    | Porte adiado                                             | Manter stock; recuperação oficial do T1000-E                                  |
| T-Deck/outros                 | Nenhum firmware de ponte                                             | Futuro por alvo/revisão                                                                  | Não presumir equivalente ao Cardputer                              | Porte adiado                                             | Manter firmware oficial específico                                            |

A fonte upstream gera aplicação `.bin`, imagem `.factory.bin`, tabela e filesystem separados.
O empacotador desta entrega **só inclui a aplicação**, renomeada com sufixo `-update.bin` para
o fluxo legado de upload próprio do flasher. `.factory.bin`, partições, imagem mesclada e
aplicação não são equivalentes. Não foram publicados apagador nem imagem de fábrica customizada.

Para uma placa virgem, a alternativa é instalar primeiro o firmware **oficial exato** pelo
procedimento documentado do fabricante, após autorização específica, e então avaliar a
atualização da ponte. Isso é uma operação diferente, não uma atualização que preserva dados.
Não apagar flash para “tentar resolver” seleção ou partições incompatíveis.

## Confirmações antes de qualquer gravação

1. Identificar aparelho físico, revisão, banda e antena. USB-C e formato da caixa não bastam.
2. Confirmar firmware atual, alvo reportado, tamanho real da flash, partições e slot ativo.
3. Guardar backup **privado** de configuração, canais/PSKs, identidades e, quando apropriado,
   flash completa. Exportação de configuração não deve ser presumida equivalente a backup completo.
4. Baixar e verificar previamente o firmware oficial para retorno e conhecer o modo de recuperação.
5. Confirmar SHA-256 do artefato, alimentação estável, cabo de dados e consequências de falha.
6. Confirmar conscientemente a gravação do aparelho identificado com o artefato conferido,
   distinguindo atualização de instalação de fábrica e aceitando as consequências possíveis.

Backup/flash dump contém segredos. Não colocar no Git, em release, issue, chat público ou CI.
Os scripts de empacotamento não procuram portas USB. O helper opcional `prepare_flash.py`
faz somente leitura, com porta explícita e `--read-backup`; nunca grava ou apaga flash.

A [CLI oficial](https://meshtastic.org/docs/software/python/cli/) documenta
`meshtastic --export-config > backup.yaml` para exportar configuração e
`meshtastic --configure backup.yaml` para restaurá-la. A exportação deve ser guardada em
local privado e conferida antes do flash; não é uma cópia integral de todas as áreas da flash.
O comando de restauração escreve configurações e só pertence ao procedimento de recuperação,
depois da conferência do aparelho e do conteúdo. Não executá-lo como teste de leitura.

## Verificador sem escrita

```sh
python3 tools/bridge/verify_artifact.py pacote.zip --model heltec-v3 --package-only
```

Esse teste valida ZIP, nomes, hash, chip, alvo declarado, tamanho e política de atualização.
**Não identifica o aparelho físico.** Heltec e Cardputer podem ter o mesmo chip; detecção de
ESP32-S3 pelo flasher não comprova modelo, pinagem ou acessório correto.

O preflight completo recebe arquivos obtidos em uma sessão de leitura autorizada:

```sh
python3 tools/bridge/verify_artifact.py pacote.zip --model heltec-v3 \
  --partition-table /caminho/privado/partitions.bin \
  --otadata /caminho/privado/otadata.bin
```

Ler exatamente o comprimento `partition_table_bytes` do manifesto a partir de `0x8000`,
e `0x2000` bytes de seleção OTA em `0xe000`, com ferramenta oficial e procedimento de
leitura revisado para o aparelho. Esses endereços são do layout ESP32-S3 específico, não
instruções para nRF52. Nenhuma leitura de aparelho foi feita durante o desenvolvimento.

O verificador deliberadamente só aceita a seleção OTA inicial conhecida do framework. Um
slot ativo diferente ou atualização OTA anterior pode ser legítimo, mas exige revisão manual;
não deve ser “corrigido” gravando boot metadata às cegas. A tabela esperada de 8 MB contém
NVS `0x9000/0x5000`, OTA data `0xe000/0x2000`, app0 `0x10000/0x330000`, app1
`0x340000/0x330000`, filesystem `0x670000/0x180000` e coredump `0x7f0000/0x10000`.

Hash confere integridade do download em relação ao manifesto; não é uma certificação de
segurança, assinatura do fabricante ou prova de compatibilidade física. Obtenha o pacote
diretamente desta release ou compile o commit registrado.

## Web Flasher oficial

O [Web Flasher](https://flasher.meshtastic.org/) documenta upload de ZIP de release ou de
`firmware-update.bin` para ESP32. Não é preciso entrar no catálogo oficial nem criar flasher.
A [documentação ESP32](https://meshtastic.org/docs/getting-started/flashing-firmware/esp32/web-flasher/)
deve ser conferida novamente no momento da instalação, porque a interface pode mudar.

No computador, usar navegador com Web Serial suportado e cabo USB de dados; selecionar o
modelo confirmado, enviar o pacote da placa e escolher **Update**, com preservação de dados,
somente se o preflight e o layout forem compatíveis. Conferir resumo de arquivos/offsets.
Se o site propuser erase, instalação de fábrica, filesystem, bootloader ou outro modelo,
interromper para revisão. Como alternativa oficial, usar o atualizador USB da release
Meshtastic exata após conferir seus argumentos; não executar o instalador de fábrica.

O fluxo ZIP legado foi considerado no empacotamento. Se a UI vigente não reconhecer o ZIP,
usar somente o `-update.bin` correspondente no upload próprio, após as mesmas verificações.
Não foi demonstrado upload pelo app iOS, Safari/iPhone ou OTA móvel para este firmware.
O iPhone é requisito de uso, não de gravação.

## Primeiro boot previsto

- BLE e texto público dedicado ativados uma vez no primeiro boot desta revisão, inclusive
  na atualização. O canal automático `Bitchat` é adicionado somente em espaço vazio;
  [ler o padrão e a chave pública antes de instalar](DEFAULTS.md).
- Conectar o app Meshtastic original e fornecer a hora. Sem hora, a descoberta Meshtastic
  é automática; depois o rádio anuncia Bitchat. Conferir região/preset/canais existentes.
- Manter Wi-Fi fora do caminho, desativar power saving stock que desligue BLE para o teste.
- Adicionar somente o canal `Bitchat` ao rádio stock remoto, com a mesma chave pública,
  sem substituir sua configuração completa. Não precisa de seleção no menu para os padrões.
- Para BLE apenas, escolher `1. Local Bitchat`. Para outro canal, usar `2. Bitchat + LoRa`,
  canal secundário com chave de 32 bytes e MQTT desligado; confirmar a publicação de texto.
  Canal primário não é elegível. Aguardar 30 s e testar
  `!lora ` literal. Não há parser de administração em texto recebido.
- Para voltar à descoberta Meshtastic: `More options → Switch phone app → Find Meshtastic: 5m` ou `Turn bridge OFF`.

No mapeamento padrão do Heltec V3, pressões curtas em USER avançam as telas; uma pressão
longa de cerca de 500 ms corresponde a selecionar. Na tela System, selecionar abre o menu,
que inclui `Bitchat bridge`. Manter pressionado por muito mais tempo pode desligar o aparelho.
Esse mapeamento foi conferido no código; navegação e resposta física do botão permanecem NT.

O estado persiste, mas o relógio pode não sobreviver ao corte de energia. Falta de hora, mudança
de canal/PSK, conflito de pontes ou erro de armazenamento bloqueia a operação correspondente.
As filas são descartadas no reinício. Não apagar NVS para recuperar uma identidade sem analisar
o backup, a causa e o efeito sobre clientes.

## Recuperação

Se apenas a ponte falhar, desligá-la pelo menu e reconectar o Meshtastic. Se não houver boot,
usar o modo ROM/download documentado para a placa e reavaliar a atualização oficial compatível.
No Heltec, a documentação descreve PRG/BOOT e reset; no ADV, seguir a sequência M5 da revisão.
Não tocar eFuses, proteção de flash, bootloader ou configuração RF para conseguir um resultado.

Ensaio obrigatório antes de recomendar uso: stock → ponte OFF → ponte A/B → stock,
preservando configurações quando suportado, comparando backup e verificando chave/identidade,
comunicação e energia. Repetir após desligamento controlado. **Rollback ainda não testado.**

### Referência oficial de retorno

A release [Meshtastic 2.7.26.54e0d8d](https://github.com/meshtastic/firmware/releases/tag/v2.7.26.54e0d8d)
inclui `firmware-esp32s3-2.7.26.54e0d8d.zip`. O arquivo foi baixado e seu SHA-256 conferido
com o digest publicado: `339650ced8b8c859751f04be69c6af845fea96baca1327518f199afd2595a9d4`.

| Aplicação oficial contida no ZIP                    | SHA-256 conferido                                                  |
| --------------------------------------------------- | ------------------------------------------------------------------ |
| `firmware-heltec-v3-2.7.26.54e0d8d.bin`             | `34c38ff938380a64ad905ab2b1086172190d83bce0b62d53b95ac490d96c1fc6` |
| `firmware-m5stack-cardputer-adv-2.7.26.54e0d8d.bin` | `bbe614bf9e9613891f53094c551daa3b1686f28ca67498713e14c9ad168205a8` |

Esses arquivos de aplicação são diferentes dos `.factory.bin` e dos `littlefs-…` presentes
no mesmo ZIP. Usar o fluxo oficial de **atualização** do modelo exato, com seleção de slot/layout
compatível. A conferência dos arquivos não demonstra que o rollback funcionou em um rádio.
