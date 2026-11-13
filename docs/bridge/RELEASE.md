# Meshtastic ↔ Bitchat 0.1.0-boot-candidate.1

**Candidato de investigação, sem boot físico validado. Não usar como release de campo.**
As pré-releases anteriores permanecem suspensas após o travamento reportado em Heltec V3.
O histórico e as hipóteses refutáveis estão em [BOOT_INCIDENT](BOOT_INCIDENT.md).

A mudança adia a reserva dos buffers grandes até o Bluetooth estar iniciado e a ponte ser
ativada. Desativação libera memória; falta de espaço recusa a ativação. O início registra
somente etapas e números de memória, sem mensagens, chaves ou localização. Os diagnósticos
não desligam o watchdog. ELF e mapa correspondentes acompanham os artefatos de CI.

Os alvos continuam Heltec V3 e Cardputer ADV + Cap LoRa-1262. Base oficial 2.7.26.54e0d8d,
ponte desligada por padrão, mesmas políticas de texto e túnel. A Alpha 2.8.2 foi avaliada,
mas não foi demonstrado que suas correções resolvam este incidente.

Os testes de software incluem compilação por placa, sanitizers, protocolo, adaptador BLE
com transporte/heap substituídos e falhas de alocação, verificação de pacotes, análise
estática e recompilação idêntica no mesmo ambiente. Isso não comprova boot, entrega BLE/RF,
rollback ou consumo. Consulte os logs do commit em teste, sem presumir sucesso de jobs ausentes.

Os pacotes contêm apenas a aplicação específica da placa; não incluem apagador, bootloader,
partições ou filesystem. Backups são privados. Uma nova gravação exige confirmar aparelho,
revisão/banda, artefato, backup, consequências e rollback conforme [INSTALL](INSTALL.md).
Não repetir o procedimento que falhou nem apagar flash como tentativa de correção.

A CI de push produz candidatos e evidência, sem publicação automática. Não promover este
candidato antes do primeiro boot, preservação de configurações e recuperação serem validados.
