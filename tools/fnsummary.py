"""One line per function in re/all.c: address, name, lines, strings used, callees."""
import re
import sys

src = open(sys.argv[1] if len(sys.argv) > 1 else "re/all.c", encoding="latin1").read()
parts = re.split(r"^// ==== (\S+) @ ([0-9a-f]+)\n", src, flags=re.M)
for i in range(1, len(parts), 3):
    name, addr, body = parts[i], parts[i + 1], parts[i + 2]
    strs = sorted(set(re.findall(r"\b[su]_([A-Za-z0-9_]+?)_00[0-9a-f]{6}", body)))
    calls = sorted(set(re.findall(r"\b((?:FUN_|thunk_FUN_)?[A-Za-z_][A-Za-z0-9_]*)\(", body)) - {name, "if", "while", "for", "switch", "return", "sizeof"})
    calls = [c for c in calls if not c.startswith(("CONCAT", "SUB", "ZEXT", "SEXT", "CARRY", "SCARRY"))]
    print("%s %-28s %5d | %s | %s" % (addr[2:], name, body.count("\n"), " ".join(strs)[:160], " ".join(calls)[:200]))
