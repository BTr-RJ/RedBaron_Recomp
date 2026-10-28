# Patch experimental do codegen ReXGlue

`rexglue-snoopy-fallback-v2.patch` registra o diff atual de `src/codegen/builders/context.cpp` no SDK experimental utilizado.

- Commit base do SDK: `7236893039fac7e6ce8a21351a430933899f9320`.
- Inclui resolução de destinos mesclados e fallback restrito aos três saltos cross-chunk verificados.
- Preserva saltos locais e exige as condições de agrupamento e exceções do patch.

Os locais corrigidos são `0x82362B18`, `0x82362BB4` e `0x82362C20`. A validação histórica registrou três fallbacks aplicados, zero ignorados e zero labels ausentes restantes.

Em um checkout compatível, confira primeiro:

```powershell
git -C CAMINHO_DO_SDK apply --check CAMINHO_DO_PROJETO/patches/rexglue-snoopy-fallback-v2.patch
```

Faça backup antes de aplicar. Se o teste passar, aplique o patch, recompile e instale o SDK conforme as instruções dessa versão. O SDK experimental desta máquina já contém o ajuste; não o aplique novamente nele.

Este patch é específico para os agrupamentos e endereços do XEX utilizado. Não é uma correção genérica para outros jogos ou versões. Os avisos e a licença do SDK original continuam aplicáveis.
