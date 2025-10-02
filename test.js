"use strict";

console.log("=== Teste de importação do pacote orcaslicer-addon ===\n");

try {
  const orcaslicer = require("orcaslicer-addon");
  
  console.log("✓ Pacote importado com sucesso");
  console.log("✓ Tipo:", typeof orcaslicer);
  
  // Lista propriedades/métodos exportados
  const props = Object.getOwnPropertyNames(orcaslicer);
  console.log("✓ Propriedades/métodos exportados:", props.length);
  
  if (props.length > 0) {
    console.log("\nAPI disponível:");
    props.forEach(p => {
      const type = typeof orcaslicer[p];
      console.log(`  - ${p}: ${type}`);
    });
  }
  
  // Tenta chamar algum método se existir (exemplo: slice, configure, etc.)
  if (typeof orcaslicer.slice === "function") {
    console.log("\n✓ Método 'slice' encontrado (função)");
  }
  
  if (typeof orcaslicer.configure === "function") {
    console.log("✓ Método 'configure' encontrado (função)");
  }
  
  console.log("\n=== Teste concluído com sucesso ===");
  process.exit(0);
  
} catch (err) {
  console.error("✗ Erro ao importar ou usar o pacote:");
  console.error(err.message);
  console.error(err.stack);
  process.exit(1);
}

