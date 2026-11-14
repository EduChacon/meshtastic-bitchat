# Uso da versão experimental .3

[Português](USAGE.md) | [English](USAGE.en.md)

Este guia descreve o firmware `v0.1.0-experimental.3`, commit `d15a403`.
Boot do Heltec relatado pelo operador não comprova entrega de mensagens. A matriz de
compatibilidade e os testes de bancada continuam aplicáveis.

## Começar pelo Bluetooth local

1. Conectar o app Meshtastic ao Heltec para fornecer a hora. Bluetooth deve estar ligado
   e a economia stock que desliga BLE deve estar desativada.
2. Na tela System, abrir `Bitchat bridge` e selecionar `Start BLE bridge`.
3. Abrir o Bitchat no iPhone, com Bluetooth permitido, perto do Heltec. Não é preciso
   emparelhar a ponte manualmente nos Ajustes do iOS.
4. Conferir o painel da ponte. `Open Meshtastic: clock` indica falta de hora; usar
   `Meshtastic BLE: 5 min`, conectar o app Meshtastic e aguardar o retorno do anúncio Bitchat.

Na configuração inicial, só o relé BLE é habilitado. Se canais B/C já foram configurados,
`Start BLE bridge` também retoma esses modos. Para um ensaio exclusivamente BLE, desabilitar
ambos em seus seletores. Não é necessário criar um canal LoRa para o relé local.

## O que faz cada opção existente

| Opção na tela                              | Efeito                                                                                                                                              |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Back`                                     | Volta ao menu System.                                                                                                                               |
| `Start BLE bridge` / `Stop bridge`         | Ativa ou desativa a ponte. Os canais B/C configurados são preservados.                                                                              |
| `B: public text channel`                   | Seleciona o canal Meshtastic que compartilha texto com o Bitchat público.                                                                           |
| `C: tunnel channel (lab)`                  | Configura o túnel experimental entre duas pontes; não entrega texto a clientes Meshtastic stock.                                                    |
| `Profile: economy`                         | **Muda para econômico**; o rótulo indica a ação, não o perfil atual.                                                                                |
| `Profile: fixed`                           | **Muda para fixo**. Para bancada alimentada por USB, este é o perfil de referência.                                                                 |
| `Meshtastic BLE: 5 min`                    | Prioriza a descoberta pelo app Meshtastic durante cinco minutos; depois retorna ao anúncio Bitchat. Não é um novo pareamento.                       |
| `C: private lab opt-in` / `C: private OFF` | Autoriza ou interrompe tráfego privado opaco no túnel experimental. Não traduz nem descriptografa DMs. Manter desligado no primeiro teste de texto. |
| `Status`                                   | Mostra o estado ou bloqueio. Estar disponível não comprova entrega no telefone.                                                                     |

O perfil econômico usa anúncios BLE menos frequentes e orçamento LoRa menor; entrega,
latência e consumo reais ainda precisam de medição. Não significa que toda função irá
operar com a mesma disponibilidade.

## Texto do Meshtastic para o Bitchat

1. Usar uma rede de teste consentida. **O texto desse canal será público no Bitchat local,
   mesmo que o canal Meshtastic tenha PSK.** Não usar um canal privado de terceiros.
2. Configurar nos rádios o mesmo canal secundário, nome e PSK própria de 32 bytes, com
   uplink/downlink MQTT desligados. Banda, região, preset e frequência também devem coincidir.
3. No Heltec modificado, abrir `B: public text channel` e escolher o canal. O primário não é
   elegível. Um canal já escolhido para C não pode ser reutilizado para B.
4. A escolha precisa ser seguida do aviso de publicação e de confirmação explícita. Se
   a tela sumir sem permitir confirmar, a ativação não está demonstrada: não presumir sucesso.
5. Com a ponte ligada, aguardar os 30 segundos de observação indicados no estado. Manter
   o Bitchat aberto perto do Heltec durante o primeiro ensaio.
6. No app Meshtastic do rádio remoto stock, selecionar **esse canal**, e enviar uma mensagem
   curta normal, por exemplo `Teste 1`. Não é DM e não precisa de `!lora` nesse sentido.

No sentido contrário, escrever no chat público local do Bitchat `!lora Teste 2`.
O prefixo é necessário para autorizar o envio por LoRa. Conferir o recebimento efetivo no
app Meshtastic remoto. Não equiparar ACK LoRa ou incremento de TX/RX à entrega no iPhone.

## Ler o estado

| Indicação                                         | Próxima verificação                                                                   |
| ------------------------------------------------- | ------------------------------------------------------------------------------------- |
| `OFF - stock BLE`                                 | A ponte está desligada; ativá-la no menu.                                             |
| `Open Meshtastic: clock`                          | Conectar o app Meshtastic para acertar a hora.                                        |
| `Meshtastic window 5m`                            | O anúncio prioriza o Meshtastic; aguardar a janela terminar.                          |
| `B listening: wait 30s`                           | Aguardar a observação inicial antes de enviar texto.                                  |
| `B conflict: stop overlap`                        | Outra ponte de texto foi detectada; revisar a topologia, sem ignorar o bloqueio.      |
| `Channel changed: blocked`                        | Nome/chave/configuração mudou; revisar e confirmar novamente o canal desejado.        |
| `BC fixed (available)` / `BC economy (available)` | A ponte está disponível segundo o firmware. Conferir conexões e recebimento nos apps. |

`BLE` conta clientes inscritos nas notificações Bitchat; `P`, peers conhecidos; `D`, descartes;
`TX/RX`, pacotes da integração LoRa; `Q`, fila LoRa. `BLE0` durante o ensaio indica que ainda
não há cliente Bitchat inscrito. Nome de usuário não autentica identidade entre redes.

## Heltec V3 sem tela

A ausência de tela, por si só, não muda a pinagem do rádio nem torna a placa outro alvo.
Confirmar que é **Heltec WiFi LoRa 32 V3**, ESP32-S3/SX1262, 8 MB e banda adequada. Se for,
o arquivo do alvo Heltec V3 é o mesmo; seguir [instalação e backup](../WINDOWS_RECOVERY.md).
Não usar esse arquivo em Heltec V2, V4, Wireless Tracker ou Wireless Stick.

**Limitação da .3:** a ponte vem desligada e sua configuração depende do menu local. Não
há ainda configurador USB da ponte ou botão correspondente no app Meshtastic oficial.
Instalar o ZIP em uma placa sem tela não resolve a ativação. Essa implantação permanece
pendente de um método de configuração local verificável; não exige computador na operação,
mas não deve ser anunciada como pronta. Não editar NVS nem apagar a flash para contornar isso.
