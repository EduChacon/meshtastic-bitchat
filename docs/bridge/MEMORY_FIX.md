# Memória da ponte — experimental .5

## Incidente e limite da conclusão

Em Heltec V3 com a `.4`, o operador relatou `Low memory: bridge OFF`, persistente após
reiniciar. Não há medição de heap desse aparelho: permanece desconhecido se falhou o total
livre, o maior bloco ou uma alocação. O código anterior reservava 66.920 bytes no modelo
host em dois grandes objetos, mesmo sem cliente conectado ou túnel ativo. A recusa também
ocorria antes da criação automática do canal. Isso explica por que insistir em criar canais
não resolve a condição de memória; não comprova a causa exata no hardware.

## Correção

- Núcleo dividido em blocos menores, sem reduzir 24 identidades ou 256 entradas de dedup.
- BLE mantém três conexões e duas posições por direção/conexão, compartilhando um conjunto
  global de oito frames. Cada frame, de até 2.048 bytes, só é alocado quando necessário.
- Uma transmissão aos três clientes compartilha um payload imutável. Cada conexão mantém
  seu próprio avanço, prazo e referência; desconectar uma não invalida as outras.
- Remontagens BLE/túnel são alocadas sob demanda e liberadas quando vazias/expiradas.
- Reserva interna de 24 KiB, bloco livre mínimo de 8 KiB, watchdog e autenticação preservados.
  Falha na inicialização libera alocações parciais. Falha durante tráfego descarta com contador;
  não transforma pacotes privados em texto nem aumenta tamanho/airtime.
- Nenhuma alteração de partições, NVS, chaves, região ou potência. Sem apagamento.

## Evidência reproduzível

`python3 tools/bridge/test_memory.py` executa os métodos reais `ensureRuntime` e
`releaseRuntime`, os layouts reais e o adaptador BLE real com heap/transporte simulados,
sob ASan/UBSan. O controle negativo preserva os layouts `.4` de `e69edd9`.

No host de 64 bits, a reserva inicial passa de 66.920 bytes (antes de overhead) para 23.392
bytes (incluindo overhead modelado). Com 64 KiB livres e blocos limitados a 16 KiB, o código
antigo é recusado e o novo inicia. Cobertura: falha em cada uma das quatro alocações iniciais,
memória total insuficiente, fragmentação, BLE indisponível e 200 ciclos de ativação,
remontagem, texto RX/TX e liberação sem vazamento. **Não são medidas do ESP32.**

`test_ble.py` cobre oito frames simultâneos, esgotamento e recuperação do conjunto global,
justiça de recepção, broadcast dividido em notificações para três conexões, desconexão no
meio de envio, expiração de fragmento abandonado e falha de alocação durante tráfego.
A compilação, análise estática, reconstrução e suítes restantes ficam nos resultados CI da
revisão; o pacote técnico guarda os logs. Nenhum ensaio de hardware foi substituído por esses testes.

## Menor teste físico

Após backup privado/preflight, atualizar somente a aplicação com o ZIP `.5` do modelo exato.
Não apagar flash nem recriar canais. Conferir se o aviso desapareceu, fornecer hora pelo
Meshtastic, abrir Bitchat em primeiro plano e conferir presença da ponte. Usar um segundo rádio
stock para texto nos dois sentidos. Repetir com dois clientes, reconexão e corte de energia
controlado; verificar preservação, consumo e memória sob carga antes de ampliar recursos.
Se a recusa persistir, o log `[bridge-memory]` informa tamanho solicitado, maior bloco e reserva;
`[bridge-boot]` informa heap livre. O resultado físico pode refutar a suficiência desta correção.

## English

Version .5 replaces eager, monolithic allocations with smaller runtime blocks, an eight-frame
on-demand BLE pool with shared broadcast payloads, and lazy reassembly storage. The 24 KiB
internal reserve, 8 KiB free-block guard, authentication, dedup capacity and watchdog remain.
The host heap model starts with 64 KiB available / 16 KiB maximum blocks where .4 is refused;
startup usage drops from 66,920 to 23,392 bytes in that model, not a physical ESP32 measurement.
Allocation failure, pool exhaustion, partial sends, disconnects and repeated lifecycle are
covered with sanitizers. No partition/configuration changes or erase. Hardware acceptance
is still required; the observed device's exact heap condition has not been captured.
