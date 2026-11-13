# Instalação e recuperação

**Estado: fluxo documentado e pacote verificado em software; nenhum flash, boot ou rollback
em hardware foi realizado.** Não existe garantia de risco zero em uma gravação. Falta de
energia, seleção errada, bootloader, partições e defeito físico podem impedir o boot.
Nenhum comando deste projeto grava um rádio automaticamente.

## Pacotes desta release

| Alvo                          | Artefato publicado                                                   | Método/navegador                                                                         | Bootloader                                                         | Estado                                | Rollback previsto                                                             |
| ----------------------------- | -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------ | ------------------------------------- | ----------------------------------------------------------------------------- |
| Heltec V3 8 MB                | ZIP por placa e `firmware-heltec-v3-bridge-…-update.bin`             | Upload próprio no Web Flasher oficial; Chrome/Edge desktop com Web Serial e USB de dados | ROM/bootloader ESP32-S3 já instalado, preservado                   | COMP; empacotamento testado; flash NT | Atualização oficial Heltec V3 compatível, via USB; ROM download se necessário |
| Cardputer ADV + Cap LoRa-1262 | ZIP por placa e `firmware-m5stack-cardputer-adv-bridge-…-update.bin` | Mesmo fluxo ESP32, selecionando o alvo exato                                             | Procedimento M5 específico da revisão; não confundir modos de boot | COMP; flash NT                        | Firmware oficial do alvo ADV e procedimento M5                                |
| T1000-E                       | Nenhum firmware de ponte                                             | Futuro: UF2/bootloader nRF52840 específico documentado por Seeed/Meshtastic              | Não usar ESP32, .bin mesclado ou offset 0x10000                    | Porte adiado                          | Manter stock; recuperação oficial do T1000-E                                  |
| T-Deck/outros                 | Nenhum firmware de ponte                                             | Futuro por alvo/revisão                                                                  | Não presumir equivalente ao Cardputer                              | Porte adiado                          | Manter firmware oficial específico                                            |

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
6. Autorizar separadamente a gravação desse aparelho com esse artefato. Publicar código não
   autoriza conectar, reconfigurar, apagar ou gravar um dispositivo.

Backup/flash dump contém segredos. Não colocar no Git, em release, issue, chat público ou CI.
Os scripts de empacotamento não procuram portas USB nem fazem backup de dispositivos.

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

- Ponte OFF: anúncio Meshtastic e funções nativas disponíveis, sujeitos à regressão de bancada.
- Conectar o app Meshtastic original, acertar região/preset/canais e fornecer a hora.
- Manter Wi-Fi fora do caminho, desativar power saving stock que desligue BLE para o teste.
- No menu local, iniciar BLE bridge. Para B, criar/selecionar canal secundário com nome não vazio,
  PSK própria de 32 bytes e MQTT uplink/downlink desligados. Canal primário não é elegível.
- Aceitar o aviso de texto público somente na rede de teste consentida. Aguardar 30 s e testar
  `!lora ` literal. Não há parser de administração em texto recebido.
- Para voltar à descoberta Meshtastic: `Meshtastic BLE: 5 min` ou `Stop bridge`.

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
