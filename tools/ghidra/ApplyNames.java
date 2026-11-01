// Apply tools/ghidra/names.txt ("<hex addr> <name>" per line, '#' comments) to the current program:
// functions are renamed, other addresses get a primary label; a third column "fn" disassembles and
// creates a function there first (code reached only through tables).
// Usage: ghidra script run tools/ghidra/ApplyNames.java --project sheep --program sheep_game.exe -- /abs/path/names.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
public class ApplyNames extends GhidraScript {
  public void run() throws Exception {
    BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]));
    String line; int n = 0;
    while ((line = r.readLine()) != null) {
      line = line.trim();
      if (line.isEmpty() || line.startsWith("#")) continue;
      String[] p = line.split("\\s+");
      Address a = toAddr(Long.parseLong(p[0].replace("0x", ""), 16));
      Function f = getFunctionAt(a);
      if (f == null && p.length > 2 && p[2].equals("fn")) {
        disassemble(a);
        f = createFunction(a, p[1]);
      }
      if (f != null) f.setName(p[1], SourceType.USER_DEFINED);
      else createLabel(a, p[1], true, SourceType.USER_DEFINED);
      n++;
    }
    println("applied " + n);
  }
}
