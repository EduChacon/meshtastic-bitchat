# Backup privado antes de instalar (opcional)

Instalar exige somente o ZIP único e o flasher oficial, como descrito no
[README](../../README.md). Este roteiro é para quem quer, antes, uma cópia completa da flash do
rádio e a conferência de partições. Ele só lê o aparelho.

O exemplo usa o Heltec WiFi LoRa 32 V3. Para outro aparelho da [lista](COMPATIBILITY.md), trocar
`heltec-v3` pelo nome do modelo nos arquivos e no parâmetro `--model` (por exemplo `heltec-v4`,
`t-deck`, `tbeam-s3-core`). O programa confere chip ESP32-S3 e tamanho da flash (8 ou 16 MB),
mas não distingue modelos de placa. **Boot físico desta versão ainda não validado em nenhum
aparelho.** Não há garantia de risco zero.

## 1. Baixar os arquivos

Em [v1.0.0-beta.5](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta.5), baixar
`technical-package-1.0.0-beta.5.tar.gz` e extrair, para a mesma pasta:

- `meshtastic-bitchat-heltec-v3-1.0.0-beta.5-<commit>.zip`;
- `prepare_flash.py` e `verify_artifact.py`;
- `SHA256SUMS`.

Não usar `Source code`, o pacote de outro modelo, uma imagem `.factory.bin` ou a versão `.2`.
Conferir o SHA-256 do ZIP com `Get-FileHash .\meshtastic-bitchat-heltec-v3-*.zip -Algorithm SHA256`
e a entrada correspondente em `SHA256SUMS`. Manter somente o ZIP escolhido nessa pasta.

Guardar também o [ZIP oficial 2.7.26 para ESP32-S3](https://github.com/meshtastic/firmware/releases/download/v2.7.26.54e0d8d/firmware-esp32s3-2.7.26.54e0d8d.zip)
para eventual retorno. Para o Heltec V3, a aplicação é `firmware-heltec-v3-2.7.26.54e0d8d.bin`, SHA-256
`34c38ff938380a64ad905ab2b1086172190d83bce0b62d53b95ac490d96c1fc6`.
O fluxo de retorno é documentado, ainda não ensaiado fisicamente neste projeto.

## 2. Backup privado e conferência, sem gravar flash

Fechar o flasher e outros programas que usem a porta serial. Manter o rádio com antena
adequada, alimentação estável e cabo USB de dados. Confirmar modelo, revisão e banda;
o programa não distingue modelos de placa que usem o mesmo ESP32-S3.

O roteiro usa Python 3.9–3.13 e `esptool` oficial 4.11.0 em ambiente isolado.
Se `py -3 --version` não funcionar, instalar Python pelo [site oficial](https://www.python.org/downloads/windows/),
incluindo o launcher. Não baixar executáveis de sites de drivers ou instaladores de terceiros.

No **PowerShell**, dentro da pasta dos arquivos baixados:

```powershell
py -3 -m venv "$env:LOCALAPPDATA\MeshtasticBitchat\tools"
& "$env:LOCALAPPDATA\MeshtasticBitchat\tools\Scripts\python.exe" -m pip install esptool==4.11.0
$pacotes = @(Get-ChildItem .\meshtastic-bitchat-heltec-v3-1.0.0-beta.5-*.zip)
if ($pacotes.Count -ne 1) { throw "Mantenha somente um ZIP 1.0.0-beta.5 do modelo nesta pasta" }
& "$env:LOCALAPPDATA\MeshtasticBitchat\tools\Scripts\python.exe" .\prepare_flash.py $pacotes[0].FullName --model heltec-v3 --port COM3 --read-backup
```

Trocar `COM3` somente se o Gerenciador de Dispositivos mostrar outra porta CP2102.
O último comando autoriza **leitura e reinício temporário em modo ROM**, não gravação.
Ele verifica chip/capacidade, lê a flash completa, guarda SHA-256 e compara partições e
seleção OTA com o pacote. Se houver instabilidade USB, repetir a leitura com `--baud 115200`.
Cada tentativa cria uma pasta nova; backups anteriores não são sobrescritos.

Esperar `PASS`. O backup fica em `%LOCALAPPDATA%\MeshtasticBitchat\backups\backup-...`,
fora da pasta Downloads e do repositório. Os logs privados podem conter identificação
do dispositivo; a flash pode conter chaves, mensagens e memória de falha. **Não enviar
essa pasta ao GitHub, chat ou CI.** Um backup feito após a falha conserva o estado restante,
não reconstrói automaticamente o firmware que existia antes da primeira gravação.

Se aparecer `STOP`, não instalar nem apagar: tamanho, leitura, partições ou slot não foram
validados. O programa preserva o que conseguiu ler. A conferência é conservadora e pode
recusar um slot OTA legítimo diferente; nunca o modifica para forçar aprovação.

Ao concluir, a porta é liberada e o rádio permanece no modo de download; isso é esperado.
Para desistir sem gravar, um toque em RST volta a executar a aplicação existente.
O helper foi testado com dados sintéticos e subprocesso simulado; a porta Windows real
continua sem ensaio neste projeto. Os comandos de leitura seguem a
[documentação oficial esptool](https://docs.espressif.com/projects/esptool/en/release-v4/esp32s3/esptool/basic-commands.html#read-flash-contents-read-flash).

## 3. Atualizar pelo site oficial

Com backup `PASS`, arquivo conferido, modelo/banda/antena confirmados e firmware oficial
de retorno guardado, a decisão de iniciar a escrita é uma confirmação separada do operador:

1. Abrir [flasher.meshtastic.org](https://flasher.meshtastic.org/) no Chrome/Edge do Windows.
2. Selecionar **Heltec V3** e enviar o **novo ZIP `.4`**.
3. Escolher **Atualizar / Update**. Não usar **Apagar Flash e Instalar**.
4. Selecionar **CP2102 … (COM3)** e conectar. Manter 115200 se foi a taxa estável anterior.
5. Conferir que a escrita é apenas da aplicação em `0x10000`. Se forem propostos
   bootloader, tabela, filesystem ou apagamento integral, cancelar antes de escrever.
6. Esperar o término sem desconectar. Se necessário após a conclusão, pressionar RST uma vez.

Se o site não aceitar o ZIP, usar o `firmware-heltec-v3-bridge-…-update.bin` da **mesma release**,
com as mesmas conferências. Nenhum arquivo deste pacote atualiza bootloader ou partições.
Isso reduz o escopo da escrita; energia, USB, seleção incorreta ou defeito físico continuam
sendo riscos. A opção Update não faz backup automaticamente.

## 4. Verificar o resultado

No primeiro boot desta revisão, BLE e texto público dedicado são ativados uma vez,
inclusive na atualização: [ler padrão e chave pública](DEFAULTS.md). O canal `Bitchat` é
criado somente em espaço vazio quando não havia canal de texto autorizado. Conferir boot,
conexão Meshtastic, canais/identidade anteriores preservados e funcionamento por pelo menos
dez minutos. Região/preset/potência existentes não são alterados.

Depois, no Heltec, abrir **System → Bitchat bridge**. Pressões curtas em USER avançam as
telas/opções; pressão de aproximadamente 500 ms seleciona. Não manter pressionado por
vários segundos. Conectar Meshtastic primeiro para fornecer a hora; sem hora, a descoberta
Meshtastic é automática. Depois abrir Bitchat `#mesh` e seguir [DEFAULTS](DEFAULTS.md).
Para isolar o teste BLE, selecionar `1. Local Bitchat`; depois usar `2. Bitchat + LoRa`
para voltar ao canal compartilhado. O fluxo e o aviso de publicação estão em [MENU_FIX](history/MENU_FIX.md).

Se o Bluetooth falhar na inicialização, o status mostra `BLE failed: restart` e o log
informa `ble-init-failed-reboot-required`; não se tenta reiniciar um controlador parcialmente
iniciado. Se faltar memória ao ativar a ponte, ela permanece OFF. Nenhuma dessas situações
deve ser marcada como êxito do teste BLE.

O log mínimo esperado contém `ble-init-end`, `gatt-ready`, `advertising-ready` e
`loop-ready`. Se voltar a travar, preservar o log, interromper novos testes da
ponte e seguir a recuperação oficial descrita em [INSTALL](INSTALL.md), sem erase integral.
