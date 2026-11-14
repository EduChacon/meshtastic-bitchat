# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

Ponte embarcada experimental, derivada do firmware oficial Meshtastic. O objetivo é usar
os aplicativos originais Bitchat e Meshtastic, sem computador, MQTT, Nostr ou internet na
operação. Computador continua necessário para compilar, instalar e diagnosticar.

**Correção experimental de memória: [v0.1.0-experimental.5](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.5).**
Reduz a reserva inicial que pode impedir a ativação com `Low memory: bridge OFF`.
Mantém as configurações, os limites e a proteção de memória. Veja [correção e evidência](docs/bridge/MEMORY_FIX.md)
e [início rápido](docs/bridge/DEFAULTS.md). A entrega entre aparelhos ainda precisa de aceite físico.

**A branch `main` ainda contém o firmware anterior.** Use a tag e os arquivos da release;
o merge de firmware permanece pendente. Consulte o [roteiro Windows](docs/bridge/WINDOWS_RECOVERY.md)
e o [registro da investigação](docs/bridge/BOOT_INCIDENT.md).

**Código posterior à `.5`:** a árvore contém uma revisão candidata, ainda não publicada
nem validada em aparelho, que dispensa o `!lora`, corrige perdas em conversa de grupo e
simplifica o menu, com alvos de compilação para mais oito aparelhos ESP32-S3. As instruções
desta página valem para os binários da `.5`; as diferenças estão em
[revisão candidata](docs/bridge/CANDIDATE.md).

**Estado: experimental.** Testes de software e compilação não comprovam funcionamento em
campo. Os resultados da CI da revisão e as lacunas devem ser consultados antes da instalação.

## Modos independentes

| Modo                     | Comportamento implementado                                                                                       | Rádios modificados                                                  |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| A — BLE local            | Relé de pacotes Bitchat, com limites, assinatura, TTL e deduplicação                                             | Um por área BLE; sem uso de LoRa pela função A                      |
| B — texto compartilhado  | `!lora texto` → texto nativo em canal Meshtastic dedicado; canal → Bitchat público                               | Um; os demais rádios continuam stock                                |
| C — túnel de laboratório | Pacotes preservados em porta privada 256; descoberta e `!bctun texto`; opção separada para tráfego privado opaco | Dois terminadores; intermediários stock precisam de ensaio separado |

**BLE e tradução de texto vêm ativos por padrão nesta revisão.** No primeiro boot, inclusive
ao atualizar uma revisão anterior, a ponte usa o canal já autorizado ou cria o canal público
dedicado `Bitchat` em um espaço vazio. Não seleciona nem substitui canais existentes por nome
ou por posição. O canal primário fica excluído. O [guia](docs/bridge/DEFAULTS.md) publica a chave
de interoperabilidade: ela não é secreta. Escolhas posteriores de desligar a ponte ou usar
somente BLE são preservadas. C continua sendo laboratório com ativação separada.

B não traduz DMs, sessões Noise, posição, telemetria, arquivos, voz ou comandos administrativos.
Escolher outro canal exige confirmar que seu texto ficará público no Bitchat local;
a PSK não protege o texto depois dessa exportação.

## Alvos desta entrega

- `heltec-v3-bitchat`: Heltec WiFi LoRa 32 V3, ESP32-S3/SX1262, flash de 8 MB.
- `m5stack-cardputer-adv-bitchat`: Cardputer **ADV**, com **Cap LoRa-1262** e revisão
  correspondente ao alvo upstream, flash de 8 MB. Cardputer original e outros acessórios não são equivalentes.

T1000-E, RAK4631, T-Deck, T-Echo, outras Heltec/LilyGO e demais dispositivos Meshtastic
continuam candidatos a **nós stock** do modo B, com rádio, banda, região, preset,
frequência e canal compatíveis. Isso não significa que este binário possa ser instalado neles.
O porte da própria ponte para T1000-E/nRF52 e T-Deck não está incluído nesta versão.
[Matriz completa](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/COMPATIBILITY.md).

O núcleo de protocolo é compartilhado, mas o firmware é compilado **por modelo de placa**.
Processador igual não garante pinagem, rádio, flash, partições ou bootloader iguais. Não há
binário universal ESP32 ou nRF52; instalar um arquivo de outro modelo pode impedir o boot.

## Download

Versão disponível: [**v0.1.0-experimental.5**](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.5), para teste de recuperação após
backup e preflight. Baixar o ZIP cujo nome começa pelo modelo correto e contém `.4`.

| Placa                         | Arquivo para o flasher oficial                                                                                                                                                                |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Heltec V3**                 | [**Baixar ZIP Heltec V3**](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.5/meshtastic-bitchat-heltec-v3-0.1.0-experimental.5-be61e1d.zip)             |
| Cardputer ADV + Cap LoRa-1262 | [Baixar ZIP Cardputer ADV](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.5/meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.5-be61e1d.zip) |

1. Seguir o [roteiro Windows com backup privado](https://github.com/EduChacon/meshtastic-bitchat/blob/main/docs/bridge/WINDOWS_RECOVERY.md).
2. Conferir o download com [SHA256SUMS](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.5/SHA256SUMS).
3. No [Web Flasher oficial](https://flasher.meshtastic.org/), selecionar a placa e o ZIP,
   depois **Atualizar / Update**. Não selecionar **Apagar Flash e Instalar**.

Os dois scripts de backup/preflight e o guia continuam anexados à release. O `-update.bin`
por placa é uma alternativa ao ZIP; **Source code** não é firmware para o instalador.
Não é necessário fazer merge para instalar os arquivos da release. Se o repositório estiver privado,
abrir os links conectado à conta GitHub com acesso.
[Verificação da publicação .4](docs/bridge/RELEASE_CHECKS.md).

Para auditoria ou compilação, o [pacote técnico](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.5/technical-package-0.1.0-experimental.5.tar.gz) reúne fontes correspondentes,
dependências, licenças, símbolos ELF/mapa, manifestos, resultados de testes e os anexos
da revisão, com seus hashes. Não é um arquivo para o flasher. O histórico de código,
as tags e o [registro da investigação](docs/bridge/BOOT_INCIDENT.md) foram preservados.

## Uso

Após instalar e verificar o primeiro boot, conectar Meshtastic para fornecer a hora. Sem hora,
o rádio anuncia Meshtastic automaticamente; com hora válida, volta ao anúncio Bitchat.
Compartilhar somente o canal `Bitchat` com o rádio stock remoto e aguardar 30 segundos.
O [guia rápido](docs/bridge/DEFAULTS.md) explica a chave pública e os casos de configuração cheia.

No Meshtastic, enviar texto normal **nesse canal**. No `#mesh` do Bitchat, enviar
`!lora texto` para sair por LoRa. Manter Bitchat aberto perto da ponte no primeiro teste.
Para mudar o comportamento, abrir `System → Bitchat bridge`:

1. **Local Bitchat:** relé Bluetooth, sem tradução nem túnel LoRa da ponte.
2. **Bitchat + LoRa:** escolher um canal secundário e confirmar a publicação de seu texto
   no Bitchat local. Aguardar 30 segundos antes do primeiro envio.

**More options** reúne perfil fixo/econômico, alternância dos apps, estado e laboratório.
Os modos básicos desativam o túnel; sua ativação posterior exige escolha e consentimento próprios.

O [guia do menu](docs/bridge/MENU_FIX.md) detalha o fluxo e o [guia anterior](docs/bridge/USAGE.md)
documenta a `.3`. O padrão novo permite ativação sem OLED no mesmo alvo Heltec V3; ainda
exige pareamento Meshtastic e hora válida. Isso é implementado e testado em host, **não validado
em placa sem tela**. Não há configurador USB próprio nem promessa de descoberta simultânea.

## Construção e documentação

Base: Meshtastic **2.7.26.54e0d8d**, commit
`54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`. É a versão Beta apresentada como estável na
[página oficial de downloads](https://meshtastic.org/downloads/) consultada em 2026-11-13.
Esta entrega não usa a linha Alpha 2.8.2.
Bitchat de referência: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

Para compilar a correção, faça checkout da tag em uma cópia de trabalho limpa:

```sh
git fetch origin tag v0.1.0-experimental.5
git checkout --detach v0.1.0-experimental.5
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

Os comandos constroem software; não fazem upload. Use somente fontes e ferramentas oficiais
fixadas. As dependências e a ferramenta precisam de download na preparação.

- [Arquitetura, limites e hipóteses refutáveis](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/ARCHITECTURE.md)
- [Compatibilidade e cobertura funcional](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/COMPATIBILITY.md)
- [Instalação, verificador e recuperação](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/INSTALL.md)
- [Ameaças, privacidade e limitações](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/SECURITY.md)
- [Testes, bancada, consumo e critérios](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/TESTING.md)
- [Fontes, proveniência e licenças](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/SOURCES.md)
- [Comparação com outras pontes e limites da evidência](docs/bridge/COMPARISON.md)
- [Registro do trabalho](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.5/docs/bridge/WORKLOG.md)
- [README original do firmware](docs/bridge/README.upstream.md)

O histórico oficial e o histórico inicial do projeto foram preservados. Não é um projeto
MeshCore. Licença do firmware: [GPL-3.0](LICENSE); as dependências mantêm suas próprias licenças.
