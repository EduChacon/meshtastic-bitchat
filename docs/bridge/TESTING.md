# Evidência e protocolo de bancada

Estados: **COMP** compilado; **SIM** teste executado no host; **PREV** inferido do código;
**NT** não testado; **HW** confirmado em hardware; **INC** incompatível com a política atual.
Nenhum sucesso funcional está classificado como HW nesta release. O primeiro boot reportado
em Heltec V3 **falhou**, com watchdog da tarefa principal; ver [BOOT_INCIDENT](BOOT_INCIDENT.md).
Os resultados de software abaixo não cobriam a inicialização real do Bluetooth.

## Testes executáveis sem rádio

```sh
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
export SOURCE_DATE_EPOCH=1794528000
pio run -e heltec-v3-bitchat -e m5stack-cardputer-adv-bitchat -t buildprog
python3 tools/bridge/test_host.py --crypto-dir .pio/libdeps/heltec-v3-bitchat/Crypto
python3 tools/bridge/test_ble.py
python3 -m unittest discover -s test/test_bitchat_bridge -p 'test_*.py' -v
pio check -e heltec-v3-bitchat --skip-packages --src-filters '+<src/bridge/>' --fail-on-defect high
pio run -e heltec-v3 -t buildprog
```

Esses comandos não gravam aparelhos. Não usar o harness upstream com seu `bake` padrão:
ele pode instalar firmware. PlatformIO 6.2.0 e intelhex 2.3.0 são necessários; preferir venv
e cache separados. No Mac ARM o gerador de filesystem upstream pode ser Intel; `buildprog`
compila a aplicação sem exigir instalação de Rosetta. Imagem factory gerada localmente pelo
upstream não entra no pacote publicado.

| Verificação                                          | Evidência                                                                                         | Limite                                                       |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| Codec, assinatura, TTL, rotas, padding, compressão   | 19 vetores gerados pelo BitFoundation oficial + CryptoKit; SIM                                    | Não são capturas de BLE real                                 |
| UTF-8, tamanhos, DM não traduzida, tags, HMAC        | Testes host com ASan/UBSan; SIM                                                                   | Não valida callbacks/radio scheduler                         |
| Fila, dedup, wrap de millis, perdas/ordenação/prazos | Testes e simulação de dois gateways; SIM                                                          | Sem RF nem modelo probabilístico de canal                    |
| Entradas inválidas                                   | 100.000 entradas determinísticas, além de casos dirigidos; SIM                                    | Não equivale a cobertura exaustiva ou fuzzer contínuo        |
| Pacote de instalação                                 | Testes Python de corrupção, placa incorreta, offset factory, arquivo extra, checksums incompletos | Usa também imagens sintéticas; não prova boot                |
| Análise estática                                     | Cppcheck no diretório C++ da ponte; logs da CI                                                    | Configuração e bibliotecas afetam alcance da análise         |
| Heltec V3 e ADV                                      | COMP; RAM/flash e logs na CI/release                                                              | Boot Heltec `.2` falhou no relato do operador; ADV ainda NT  |
| Ponte desativada                                     | Compilação de `heltec-v3` sem flag                                                                | Regressão funcional stock ainda NT                           |
| Repetibilidade                                       | Duas compilações da aplicação no mesmo ambiente de CI com epoch fixo                              | Não promete binário idêntico entre SOs/toolchains diferentes |

O adaptador BLE também é executado no host com substitutos de NimBLE/heap, ASan e UBSan,
incluindo falha de alocação, filas e ciclo desligado/ligado/desligado. Esse teste não simula
o controlador ou o escalonador FreeRTOS; a inicialização real ainda exige bancada.

Logs da CI identificam commit, ferramenta, comando e resultado real. Desde o incidente de
boot, publicação exige acionamento explícito após os gates físicos; push só verifica e
gera candidatos. Não interpretar um job ignorado como aprovado.

## Reprodução dos vetores

O gerador está em `tools/bridge/generate_vectors.swift`; usa chaves sintéticas fixas.
No macOS, criar um executável Swift temporário com dependência local no pacote
`localPackages/BitFoundation` de Bitchat commit
`30c58eee349e172b37f69faa2113c9d5f2c76c23`, importar o produto `BitFoundation`, copiar o
gerador como `main.swift` e executar `swift run`. Comparar o JSON semanticamente com
`test/test_bitchat_bridge/official_vectors.json`. A geração original usou Swift 6.4.2.
Não copiar chaves de um iPhone nem usar tráfego pessoal como fixture.

## Gates físicos obrigatórios — sem aprovação física

Antes de cada fase, registrar modelo/revisão/antena/banda, versões e flags dos dois apps,
iOS/Android, commit, SHA-256 do artefato, região, preset, frequência efetiva e canais.
Manter o T1000-E stock. Confirmar baseline SX1262 ↔ LR1110 separadamente do Bitchat.
O aviso de SX127x ↔ LR1110 não é evidência de falha no par Heltec V3/SX1262.

1. **Baseline stock:** dois rádios originais, 100 textos sintéticos curtos em cada sentido,
   mesma configuração de rádio, app iOS conectando/reconectando normalmente. Registrar
   entrega, latência e consumo. Se falhar, não instalar a ponte para tentar consertar LoRa.
2. **Instalação reversível:** cumprir [INSTALL.md](INSTALL.md), fazer backup privado,
   confirmar arquivo/placa antes de escrever, stock → ponte OFF → stock. Verificar boot,
   identidade, configurações, canal e comunicação. Se rollback falhar, interromper expansão.
3. **A:** dois iPhones sem internet, fora de alcance direto, cada um ao alcance da ponte,
   LoRa desativado. Enviar 100 mensagens por sentido; repetir ponte desligada como controle
   negativo. Sem falha no controle negativo não se comprovou o relé. Testar 1/2/3 conexões,
   fila cheia e cliente lento; registrar justiça e latência por cliente.
4. **B:** um Heltec modificado + um rádio stock, Bitchat de um lado e Meshtastic do outro.
   Confirmar que o app transmite `!lora ` literalmente; testar UTF-8 de 120/121 bytes,
   mensagens sem prefixo, DM, canal errado, primário, MQTT, PSK mudada e reinício.
   Esperar zero exportações dos casos proibidos e nenhuma truncagem silenciosa.
5. **iOS:** descoberta alternada primeiro; depois tentar simultânea. Registrar UUIDs no
   anúncio principal, MTU real, writes/notifies, restauração e p95. Estados: foreground,
   background, bloqueado, encerrado manualmente, retorno ao alcance, Bluetooth off/on,
   reboot e três clientes. Encerramento manual não tem entrega garantida. Falha de
   simultaneidade exige manter a janela Meshtastic bem sinalizada.
6. **C:** dois rádios modificados, iPhones só ao alcance da própria ponta, internet off.
   Começar por presença/`!bctun`; desligar uma ponta como controle negativo. Depois testar
   nó stock intermediário com as mesmas configurações. Sessão Noise, DM, verificação,
   grupo/convite e recibos são ensaios separados, com airtime e timeout registrados.
   Perder fragmento deve expirar sem parcial. Não há ARQ próprio neste MVP.
7. **B+C:** testar duplicação, ecos, múltiplas pontes e domínios sobrepostos. Prova de
   exportador B deve bloquear conflito visível. Não promover topologia com exportadores
   B sobrepostos apenas porque mensagens parecem corretas em um ensaio curto.
8. **Estabilidade:** 24 h inicial, depois 72 h, carga sintética limitada; anotar resets,
   mínimo heap livre, filas, descartes, travamento de BLE, alimentação e temperatura.
   Testar corte de energia controlado, bateria baixa e configuração inválida; nunca
   provocar subtensão deliberada durante gravação de firmware.

Critérios funcionais mínimos: zero DM exportada em B; zero aceitação de canal não autorizado;
zero alteração de payload assinado; zero crash/overflow detectado; todas as filas liberadas
após seus prazos; controle negativo consistente; atualização/rollback repetíveis. Metas de
entrega e energia serão definidas após a linha de base, com intervalos de confiança. Não
confundir ACK LoRa, entrada na fila ou notificação enviada com exibição no iPhone.

## Energia e desempenho

Medir stock, ponte ociosa e mesma carga em fixo/econômico, com Wi-Fi, tela e GPS identificados.
Usar medidor que capture picos, registrar alimentação, instrumento, resolução/calibração,
duração ≥30 min por estado, pelo menos 3 repetições e incerteza. Medir rádio completo,
não apenas corrente nominal do MCU. Exportar corrente média/pico, Wh, mensagens entregues,
energia por sessão/mensagem, p50/p95 de descoberta/reconexão/entrega e airtime por função.

Separar custo de presença, Noise, recibos, fragmentos e retries do custo do texto. Simular
economia por janela de botão só em fase posterior: esta versão implementa disponibilidade
contínua com anúncios mais espaçados, não janela de sono. Não usar power saving stock sem
medir, pois pode desligar BLE. Estimativa de bateria só após capacidade utilizável e perdas
do regulador medidas; nenhum número de autonomia é prometido nesta release.

Planilha mínima (sem dados pessoais):

```csv
run_id,utc,board,revision,commit,app_versions,profile,voltage_V,instrument,duration_s,repetition,mean_mA,peak_mA,energy_Wh,sent,displayed,p95_discovery_s,p95_delivery_s,airtime_s,uncertainty,notes
```

## Decisão de promoção

Concluir software/CI não conclui o produto em campo. A menor próxima etapa é leitura de
identificação/configuração e baseline stock; só depois autorizar atualização do Heltec
confirmado. Cardputer é o segundo alvo. T1000-E como ponte e T-Deck ficam para uma revisão
posterior, sem misturar novas portas com a estabilização atual.
