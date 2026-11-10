# .ino is C++ but no compiler takes that extension. Same step as build.sh:
#   { echo '#include "proto.h"'; cat TamaPoke.ino; } > sketch.cpp
file(READ "${INO}" body)
file(WRITE "${OUT}" "#include \"proto.h\"\n${body}")
